//==============================================================================
// Channel.cpp  —  implementation of the per-channel sampler engine.
//
// FIX: Velocity → gain on the absolute 0–127 scale (gain = velocity / 127).
//      Velocity scales amplitude continuously across the whole range, so soft
//      velocity layers stay proportionally soft and loud layers reach full only
//      near vel 127 — fixes multisampled blobs whose soft layers played too
//      loud. A one-sample drum key (full range) tracks velocity the same way.
//      Equals amp_veltrack=100. Mirrors Sample Arena's SfzPlayerProcessor.
//
// Audio path:
//   renderBlock() renders all active voices into a per-channel tempBuffer,
//   then runs click voices into the same buffer, then runs the post-mix
//   effects chain (5-band EQ → delay → reverb), and finally sums tempBuffer
//   additively into outBuffer.
//
// Per-voice DSP:
//   portamento + envelopes + LFOs, sample read (linear-interp),
//   amp gain + filter (with keytrack + env + LFO) + equal-power pan.
//
// Mono note-priority uses three INDEPENDENT switches (monoHoldStolen /
// monoRetrigNew / monoRetrigStolen — see setMonoConfig) over a held-note stack,
// following the reference SFZ player's semantics.
//
// Drum mode (isDrumChannel == true):
//   - loadDrumKit composes regions from multiple kit-blobs via DrumKitRegistry,
//     one single-key region per loaded MIDI note (lokey == hikey == note,
//     pitchKeycenter == note so no melodic transposition happens).
//   - Per-key live overrides for gain / pitch / filter cutoff / A / D / R live
//     in drumKeyStates[note]. Sentinel value -1.0f on the envelope / cutoff
//     atomics means "fall through to the channel-level atomic" so a slot
//     freshly switched into DRUMS sounds neutral until the popup wires up.
//==============================================================================

#include "Channel.h"          // pulls JuceHeader.h first
#include "PerfMonitor.h"
#include "GlobalMacros.h"
#include "DrumKitRegistry.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace Betel
{
    //==========================================================================
    // Velocity response (see startVoice).
    //
    //   kVelPeak      -- gain at velocity 127.  UNITY (1.0): at v127 the loudest
    //                    of the kit's velocity layers plays at full level, so a
    //                    style hitting v127 with the fader at unity (mixer 127/128)
    //                    reaches 0 dB.  (Was 0.70, which capped every drum ~3 dB
    //                    low no matter how hard the style hit.)
    //
    //   kDrumVelTrack -- SFZ amp_veltrack for drums, 0..100, feeding the exponent
    //                    e = 2 * track / 100:
    //                       100 = square law  (steep -- soft hits collapse)
    //                        50 = LINEAR       <-- gain = velocity / 127
    //                         0 = velocity ignored
    //                    LINEAR (50) is the documented intent (see the file
    //                    header): every step below 127 drops the level in direct
    //                    proportion, so v64 ~ half, v32 ~ a quarter, down to v1
    //                    which is barely audible -- a smooth, predictable taper
    //                    instead of the square law's cliff.  The velocity LAYERS
    //                    ride on top: velocity still selects which sample sounds
    //                    (soft / mid / loud), this only sets how loud it plays.
    //
    //   MELODIC voices do NOT use this exponent model — see the note-on block.
    //==========================================================================
    static constexpr float kVelPeak      = 1.0f;    // v127 -> unity (loudest layer, full)
    static constexpr float kDrumVelTrack = 50.0f;   // drums: linear (gain = velocity / 127)

    //==========================================================================
    // STEREO WIDTH — how much of a stereo sample's SIDE signal reaches the mix.
    //
    //     M = (L+R)/2      S = (L-R)/2 * width      out = M+S , M-S
    //
    // 1.0 = the sample exactly as recorded.  0.0 = mono sum (still every bit of
    // recorded energy, just phase-coherent and centred).
    //
    // This exists because the renderer used to read channel 0 ONLY and copy it
    // to both outputs.  That is silently a width of ZERO, and the sample library
    // was voiced against it.  Playing the real stereo image restored something
    // that had never been heard: for a kit sampled with overheads/room, every
    // hit's transient became decorrelated across the two outputs.  Total RMS
    // barely moves (which is why it doesn't read as LOUDER), but a wide,
    // decorrelated transient with the cymbal wash of the second channel restored
    // on top reads as noticeably harder and splashier — "aggressive".
    //
    // So width is a calibration dial, not a fixed truth.  Melodic voices take
    // the full image; drum and percussion channels are narrowed, because a kit
    // is the one thing in the mix that wants to glue rather than spread.
    //
    // THIS IS THE DIAL to turn if the kit still hits too hard (lower) or now
    // sounds too narrow (raise); 0.0 restores the pre-stereo character exactly,
    // only fuller, since it sums both channels instead of discarding one.
    //==========================================================================
    // REVERTED to 1.0 after the CHORD2 investigation.  It was briefly set to
    // 0.0 to test whether the true-stereo read was behind a "strange and
    // pitched" CHORD2; it was not (the cause was pruneDuplicateSources
    // discarding the style's minor recording), so the test setting had no
    // reason to stay.
    //
    // Leaving it at 0.0 had a side effect worth recording, because it is not
    // obvious: it narrows MELODIC voices to their mid signal while drums keep
    // kDrumStereoWidth, so the kit is untouched and everything else loses its
    // side energy.  Nothing gets louder — but the drums become louder
    // RELATIVE to the rest of the style, which reads as "the drums are
    // suddenly loud" on any style whose melodic samples are genuinely stereo.
    //
    // A lesson for the next diagnostic: changing one voice class and not the
    // other shifts the BALANCE, so the symptom shows up somewhere other than
    // where the change was made.
    static constexpr float kMelodicStereoWidth = 1.00f;   // melodic: as recorded
    static constexpr float kDrumStereoWidth    = 0.35f;   // kit: narrowed, glued

    //==========================================================================
    // TEMP kick-mix diagnostics -> D:\workspace\BetelgeuseArranger\grex_drum.txt
    // (same file the registry's kickmix startup log uses).  Message-thread only.
    //==========================================================================
    static void kmLog (const juce::String& line)
    {
        GrexPaths::drumLog()      // plugin root — see GrexPaths
            .appendText ("[kickmix] " + line + juce::newLine);
    }

    //==========================================================================
    // RBJ biquad coefficient setters (cookbook formulas, normalised by a0).
    //==========================================================================
    void Channel::Biquad::setPeaking (float freq, float gainDb, float Q, double sampleRate)
    {
        const float A    = std::pow (10.0f, gainDb / 40.0f);
        const float w0   = juce::MathConstants<float>::twoPi
                         * juce::jlimit (20.0f, 20000.0f, freq)
                         / (float) juce::jmax (1.0, sampleRate);
        const float cosW = std::cos (w0);
        const float sinW = std::sin (w0);
        const float alpha = sinW / (2.0f * juce::jmax (0.05f, Q));

        const float b0_ = 1.0f + alpha * A;
        const float b1_ = -2.0f * cosW;
        const float b2_ = 1.0f - alpha * A;
        const float a0_ = 1.0f + alpha / A;
        const float a1_ = -2.0f * cosW;
        const float a2_ = 1.0f - alpha / A;

        b0 = b0_ / a0_;
        b1 = b1_ / a0_;
        b2 = b2_ / a0_;
        a1 = a1_ / a0_;
        a2 = a2_ / a0_;
    }

    void Channel::Biquad::setLowShelf (float freq, float gainDb, double sampleRate)
    {
        const float A     = std::pow (10.0f, gainDb / 40.0f);
        const float w0    = juce::MathConstants<float>::twoPi
                          * juce::jlimit (20.0f, 20000.0f, freq)
                          / (float) juce::jmax (1.0, sampleRate);
        const float cosW  = std::cos (w0);
        const float sinW  = std::sin (w0);
        const float S     = 1.0f;
        const float alpha = (sinW / 2.0f)
                          * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        const float sqA2  = 2.0f * std::sqrt (A) * alpha;

        const float b0_ =        A * ((A + 1.0f) - (A - 1.0f) * cosW + sqA2);
        const float b1_ =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW);
        const float b2_ =        A * ((A + 1.0f) - (A - 1.0f) * cosW - sqA2);
        const float a0_ =            ((A + 1.0f) + (A - 1.0f) * cosW + sqA2);
        const float a1_ = -2.0f *    ((A - 1.0f) + (A + 1.0f) * cosW);
        const float a2_ =            ((A + 1.0f) + (A - 1.0f) * cosW - sqA2);

        b0 = b0_ / a0_;  b1 = b1_ / a0_;  b2 = b2_ / a0_;
        a1 = a1_ / a0_;  a2 = a2_ / a0_;
    }

    void Channel::Biquad::setHighShelf (float freq, float gainDb, double sampleRate)
    {
        const float A     = std::pow (10.0f, gainDb / 40.0f);
        const float w0    = juce::MathConstants<float>::twoPi
                          * juce::jlimit (20.0f, 20000.0f, freq)
                          / (float) juce::jmax (1.0, sampleRate);
        const float cosW  = std::cos (w0);
        const float sinW  = std::sin (w0);
        const float S     = 1.0f;
        const float alpha = (sinW / 2.0f)
                          * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        const float sqA2  = 2.0f * std::sqrt (A) * alpha;

        const float b0_ =        A * ((A + 1.0f) + (A - 1.0f) * cosW + sqA2);
        const float b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW);
        const float b2_ =        A * ((A + 1.0f) + (A - 1.0f) * cosW - sqA2);
        const float a0_ =            ((A + 1.0f) - (A - 1.0f) * cosW + sqA2);
        const float a1_ =  2.0f *    ((A - 1.0f) - (A + 1.0f) * cosW);
        const float a2_ =            ((A + 1.0f) - (A - 1.0f) * cosW - sqA2);

        b0 = b0_ / a0_;  b1 = b1_ / a0_;  b2 = b2_ / a0_;
        a1 = a1_ / a0_;  a2 = a2_ / a0_;
    }

    //==========================================================================
    void Channel::Voice::reset()
    {
        active = false;
        note = -1; srcNote = -1; fromEditor = false; extraGain = 1.0f;
        velocity = 0; regionIndex = -1;
        preset.reset();
        playPosition = 0.0;
        currentPitchSemitones  = 60.0;
        targetPitchSemitones   = 60.0;
        glideCoeff = 0.0;
        portamentoActive = false;
        velocityGain = 1.0f;
        regionGain   = 1.0f;
        kickMixGain  = 1.0f;
        // Crossfade state.  reset() runs on a STOLEN voice, which can be one
        // caught mid-fade — leaving xfadeGain at 0.3 and xfadeInc negative would
        // hand the next note a voice that fades itself to silence.
        xfadeGain    = 1.0f;
        xfadeInc     = 0.0f;
        fadingOut    = false;
        ampEnv  = {};
        filtEnv = {};
        pitchEnv = {};
        filterL.reset();
        filterR.reset();
        hpFilterL.reset();
        hpFilterR.reset();
        ampLfo  = {};
        filtLfo = {};
        pitchLfo = {};
        looping  = false;
        loopStart = 0; loopEnd = 0;
        pitchKeycenter   = 60;
        tuneCents        = 0;
        sourceSampleRate = 44100.0;
    }

    //==========================================================================
    Channel::Channel()
    {
        heldNotes.reserve(32);
        for (auto& v : voices) v.reset();

        // Saved-voice stash: nothing stashed, unity gain, no params.  Atomics in
        // an array are NOT value-initialised by the default constructor, so a
        // read before the first stash would otherwise be indeterminate — and
        // that read happens on the audio thread.
        for (int i = 0; i < kMaxPooledFlag; ++i)
        {
            pooledStashed  [(size_t) i].store (false, std::memory_order_relaxed);
            pooledHasParams[(size_t) i].store (false, std::memory_order_relaxed);
            pooledGain     [(size_t) i].store (1.0f,  std::memory_order_relaxed);
        }
    }

    Channel::~Channel() = default;

    void Channel::prepare(double sampleRate, int blockSize)
    {
        currentSampleRate = sampleRate;
        currentBlockSize  = blockSize;
        for (auto& v : voices) v.reset();
        heldNotes.clear();
        currentPitchBendValue = 0.0f;

        tempBuffer.setSize (2, blockSize, false, true, true);
        tempBuffer.clear();

        for (int i = 0; i < 5; ++i)
        {
            eqL[i].reset();
            eqR[i].reset();
            cachedEqFreq[i] = -1.0f;
            cachedEqGain[i] =  0.0f;
        }

        reverbFx.prepare (sampleRate);
        delayFx.prepare (sampleRate);
        sweetenerFx.prepare (sampleRate);

        // Drum-mode FX bus + its wet-path scratch buffer.
        drumFxBus.prepare (sampleRate, blockSize);
        drumFxScratch.setSize (2, blockSize, false, true, true);
        drumFxScratch.clear();
        drumFxDry.setSize (2, blockSize, false, true, true);
        drumFxDry.clear();

        // Sounds-path insert FX.
        chorusFx.prepare (sampleRate);
        wahFx   .prepare (sampleRate);
        phaserFx.prepare (sampleRate);
    }

    void Channel::release()
    {
        for (auto& v : voices) v.active = false;
        heldNotes.clear();
        for (auto& c : keyDownCount)  c = 0;  // piano strip: nothing is playing
        for (auto& c : editorKeyDown) c = 0;
        for (auto& L : noteLatch)     L = 0;
        for (auto& x : keyDownXpose)  x = 0;  // octave-shift latch: stale is harmless, clean is cleaner
        reverbFx.reset();
        delayFx.reset();
    }

    //==========================================================================
    double Channel::semitoneToRatio(double semitones, double sourceSR, double targetSR)
    {
        double ratio = std::pow(2.0, semitones / 12.0);
        if (targetSR > 0.0 && sourceSR > 0.0) ratio *= sourceSR / targetSR;
        return ratio;
    }

    //==========================================================================
    int Channel::findRegion(int note, int velocity) const
    {
        auto pv = std::atomic_load (&active);
        if (! pv) return -1;
        const auto& regions = pv->regions;

        auto match = [&regions, velocity] (int n) -> int
        {
            int bestIdx   = -1;
            int bestScore = INT_MAX;
            for (int i = 0; i < (int) regions.size(); ++i)
            {
                const auto& r = regions[(size_t) i];
                if (r.kickMixVariant >= 0) continue;   // KICK MIX layer: triggered explicitly, not matched
                if (n >= r.lokey && n <= r.hikey
                    && velocity >= r.lovel && velocity <= r.hivel)
                {
                    const int score = (r.hikey - r.lokey) + (r.hivel - r.lovel);
                    if (score < bestScore) { bestScore = score; bestIdx = i; }
                }
            }
            return bestIdx;
        };

        if (const int hit = match (note); hit >= 0)
            return hit;

        // ── Octave fallback — MELODIC CHANNELS ONLY ──────────────────────────
        //
        // On a melodic instrument this is correct sampler practice, and it stays:
        // a style bass asking for C1 on a blob mapped from C2 borrows the C2
        // sample and stretches it down, which beats silence every time.
        //
        // On a DRUM channel it is catastrophic, because a drum key is not a
        // PITCH — it is an INSTRUMENT SELECTOR.  Note 31 is not "G, an octave
        // below G2"; it is "whatever percussion sits on key 31".  Borrowing the
        // key an octave up and pitch-shifting it down hands back a completely
        // unrelated instrument, stretched into something that sounds nothing like
        // what the style asked for.
        //
        // That is the "gong": ModCountryBld1's RhySub plays notes 24, 31 and 32
        // (bank-126 Latin percussion, for which Grex ships no samples).  Every one
        // of them was being answered by an octave-DOWN drum —
        //     24 -> Bass Drum 1     stretched -12 st
        //     31 -> High Floor Tom  stretched -12 st   (76 hits — the loudest one)
        //     32 -> Pedal Hi-Hat    stretched -12 st
        // — i.e. a kick and a floor tom dragged a full octave down into a long,
        // deep, resonant boom.  (Note 84 went the other way: the Long Whistle,
        // stretched UP an octave.)
        //
        // A drum key with no element must simply be SILENT.  A wrong percussion
        // sound is worse than none: silence just leaves a hole in the groove,
        // whereas an octave-shifted kick actively fights the arrangement.
        if (isDrumChannel.load())
            return -1;

        // (e.g. a style bass asking for C1 on a blob mapped from C2).  Retry
        // the same pitch class in a neighbouring mapped octave INSTEAD of
        // dropping the note — strict-range silence is worse than a nearby
        // sample.  Selection only: the caller still pitches the voice from the
        // REQUESTED note via pitchKeycenter, so the note SOUNDS at the asked
        // pitch (the sample is stretched, standard sampler practice).  Prefer
        // higher-mapped samples (pitched down = cleaner) before lower ones.
        for (const int off : { +12, +24, -12, -24 })
        {
            const int n = note + off;
            if (n < 0 || n > 127) continue;
            if (const int hit = match (n); hit >= 0)
                return hit;
        }
        return -1;
    }

    //==========================================================================
    Channel::Voice* Channel::allocateVoice()
    {
        for (auto& v : voices)
            if (!v.active) return &v;

        // Pool exhausted — steal, in strict preference order:
        //   1. the OLDEST style voice already in Release (its tail is expendable)
        //   2. the OLDEST style voice
        //   3. only then an editor voice, oldest first
        //
        // The old code always took voices[0], which meant a busy style part
        // could walk straight over whatever happened to live in slot 0 —
        // including a note the user was holding on the piano strip.  Ranking by
        // startSerial makes stealing predictable, and ranking editor voices last
        // is what lets a manual note survive while the style plays.
        Voice* releasing = nullptr;
        Voice* styleV    = nullptr;
        Voice* editorV   = nullptr;

        for (auto& v : voices)
        {
            // A voice fading out of a mono crossfade is retiring, exactly like a
            // releasing one, so it ranks with them: taken before any live voice,
            // never instead of one.
            Voice** slot = v.fromEditor ? &editorV
                         : ((v.ampEnv.stage == AHDSREnvelope::Release || v.fadingOut)
                                ? &releasing
                                : &styleV);
            if (*slot == nullptr || v.startSerial < (*slot)->startSerial)
                *slot = &v;
        }

        Voice* victim = releasing != nullptr ? releasing
                      : (styleV  != nullptr ? styleV
                                            : (editorV != nullptr ? editorV
                                                                  : &voices[0]));
        victim->reset();
        return victim;
    }

    //==========================================================================
    //==========================================================================
    // ONE PLACE THAT TURNS A GLIDE TIME INTO A ONE-POLE COEFFICIENT.
    //
    // Four call sites used to each divide a distance by a sample count, which
    // meant four chances to disagree about the curve.  They now all ask this.
    //
    // The result is independent of the INTERVAL, which is the point: an RC glide
    // takes the same time to cross a semitone as an octave, and it is the
    // easing - not the duration - that makes a slide sound played rather than
    // computed.
    //==========================================================================
    static inline double glideCoeffFor (double glideMs, double sampleRate) noexcept
    {
        const double secs = juce::jmax (0.001, glideMs / 1000.0);
        const double n    = juce::jmax (1.0, secs * juce::jmax (1.0, sampleRate));
        return 1.0 - std::exp (-5.0 / n);      // ~99.3% of the way in `secs`
    }

    void Channel::startVoice(Voice* v, int note, int velocity, int regionIdx, bool glideFromCurrent)
    {
        if (v == nullptr) return;
        auto pv = std::atomic_load (&active);
        if (! pv) return;
        const auto& regions = pv->regions;
        const auto& samples = pv->samples;
        if (regionIdx < 0 || regionIdx >= (int) samples.size()) return;

        v->preset = pv;   // ring this note on its own instrument come what may
        v->startSerial = ++voiceSerialCounter;   // start order for off-pairing

        const auto& region = regions[(size_t) regionIdx];
        const auto& sample = samples[(size_t) regionIdx];

        const double oldPitchSemi = v->currentPitchSemitones;
        const bool   drumMode     = isDrumChannel.load();

        v->active           = true;
        v->note             = note;
        v->velocity         = velocity;
        v->regionIndex      = regionIdx;

        // ── Velocity → gain ──────────────────────────────────────────────────
        // DRUMS use a LINEAR law on the absolute 0..127 scale, peaking at UNITY:
        //
        //       gain = velocity / 127            (kVelPeak 1.0, kDrumVelTrack 50)
        //
        // v127 -> 1.0 (the loudest velocity layer at full level); every step
        // below scales the amplitude in direct proportion -- v64 ~ half, v32 ~ a
        // quarter, down to v1 which is barely audible.  A smooth, predictable
        // taper rather than the square law's cliff (which buried soft hits ~23 dB
        // down and, with the old 0.70 peak, capped even the hardest hit ~3 dB
        // low).  It works even when a key holds only ONE sample -- that sample
        // just gets quieter with velocity, which alone gives a kit its dynamics.
        // The velocity LAYERS (see composeDrumKit) ride on top: velocity selects
        // WHICH sample sounds (soft / mid / loud), this sets HOW loud it plays.
        //
        // MELODIC voices use the COMPRESSED curve u * (1.4 - 0.7u): it lifts
        // soft notes and flattens loud ones, which is what a layered sample
        // player wants — the velocity LAYERS already carry most of the timbral
        // dynamics, so the gain curve only has to taper.
        //
        // DO NOT "correct" this to the square law.  It was tried and reverted:
        // the square law is the GM law for CC 7 VOLUME, not for note VELOCITY —
        // two different things — and applying it here squares the response a
        // second time on top of the already-law-correct fader.  Measured on
        // 80sDisco it cost the softer parts 7..10 dB (PAD median vel 46 fell
        // 10 dB, PHRASE1 7 dB, CHORD2 8.9 dB), collapsing every melodic part
        // under the drums, which keep a linear velocity law and so didn't move.
        {
            const float u = (float) velocity / 127.0f;

            if (isDrumChannel.load())
            {
                // SFZ amp_veltrack, 0..100:  50 = linear (gain = u), 100 = square
                // law, 0 = velocity ignored.  gain = peak * u ^ (2 * track / 100).
                const float e   = 2.0f * kDrumVelTrack * 0.01f;
                v->velocityGain = kVelPeak * std::pow (u, e);
            }
            else
            {
                v->velocityGain = u * (1.4f - 0.7f * u);
            }
        }
        v->regionGain       = juce::Decibels::decibelsToGain(region.volume);
        v->kickMixGain      = 1.0f;   // KICK MIX overrides this after startVoice for kick voices

        // Crossfade state.  allocateVoice returns a FREE voice without calling
        // reset(), and a voice can go inactive mid-fade (envelope end, sample
        // end), so the only guaranteed clearing point is here — every note that
        // starts goes through startVoice.
        v->xfadeGain        = 1.0f;
        v->xfadeInc         = 0.0f;
        v->fadingOut        = false;
        v->looping          = region.hasLoop;
        v->loopStart        = region.loopStart;
        v->loopEnd          = region.loopEnd;
        v->pitchKeycenter   = region.pitchKeycenter;
        v->tuneCents        = region.tune;
        v->sourceSampleRate = sample.sampleRate;
        v->playPosition     = (double) region.offset;

        // Base pitch: melodic transposition + region fine-tune.
        // For drum regions pitchKeycenter == note (set in loadDrumKit), so
        // the (note - pitchKeycenter) term collapses to 0 and only tune + the
        // per-key drum pitch offset apply.
        v->targetPitchSemitones = (double)(note - region.pitchKeycenter)
                                  + (double) region.tune / 100.0;

        if (drumMode && note >= 0 && note < 128)
            v->targetPitchSemitones += (double) drumKeyStates[(size_t) note].userPitchSemi.load();
        else if (! drumMode)
        {
            const int pitchClass = ((note % 12) + 12) % 12;
            v->targetPitchSemitones += (double) scaleTuningCents[pitchClass].load() / 100.0;
        }

        //----------------------------------------------------------------------
        // PSEUDO ROUND ROBIN.
        //
        // startVoice is the ONE place every voice starts, which is why the
        // jitter belongs here and not at the several call sites that lead to it.
        //
        // Three deviations, drawn once and kept for the voice's life:
        //
        //   START OFFSET is first because it is the one that actually works.
        //     The ear identifies a repeat by its ATTACK, so moving where the
        //     sample begins by a millisecond or two changes the part being
        //     listened to.  Forward only, and only when there is sample left to
        //     give - walking backwards past sample 0 is silence, and a start
        //     past the transient is a different drum, not a variation.
        //
        //   PITCH is the classic one and the cheapest to hear.  A kit tolerates
        //     far more of it than a pitched instrument does.
        //
        //   GAIN is last and smallest.  It helps, but on its own it is just a
        //     volume wobble - the timbre still repeats exactly.
        //
        // Drums only.  On a sustained melodic voice a start offset skips into
        // the body and the cents budget above would sound out of tune; that case
        // wants slow per-voice drift instead, which is a different mechanism.
        //----------------------------------------------------------------------
        if (drumMode && note >= 0 && note < 128)
        {
            const float amt = (float) drumKeyStates[(size_t) note].roundRobinAmount.load() * 0.01f;

            if (amt > 0.0f)
            {
                v->targetPitchSemitones +=
                    (double) (rrNextBipolar() * amt * kRrPitchCents) / 100.0;

                // regionGain and NOT extraGain: noteOn assigns `v->extraGain =
                // extraGain` immediately AFTER startVoice returns, so anything
                // written to it here is overwritten a few lines later.  Silently.
                v->regionGain *= juce::Decibels::decibelsToGain (
                                     rrNextBipolar() * amt * kRrGainDb);

                // Unipolar: 0 .. kRrStartMs forward.  Clamped to leave most of
                // the sample ahead of the read head, so a short one-shot cannot
                // be started near its own end.
                const int   len   = sample.buffer.getNumSamples();
                const float maxSm = (float) (currentSampleRate * kRrStartMs * 0.001);
                const float room  = (float) juce::jmax (0, len - (int) region.offset) * 0.25f;
                const float shift = juce::jmin (maxSm, room)
                                    * amt * (0.5f * (rrNextBipolar() + 1.0f));

                v->playPosition += (double) shift;
            }
        }

        if (glideFromCurrent && portamentoTime.load() > 0.1f && ! drumMode)
        {
            v->currentPitchSemitones = oldPitchSemi;
            v->glideCoeff = glideCoeffFor ((double) portamentoTime.load(), currentSampleRate);
            v->portamentoActive = true;
        }
        else
        {
            v->currentPitchSemitones = v->targetPitchSemitones;
            v->portamentoActive = false;
        }

        // All four filter instances, not just the SVF pair: the band HP stage
        // was previously left carrying state from whatever note last used this
        // voice.  That mattered little when both channels always ran; with the
        // mono fast path (which leaves the R instances idle for mono sources) a
        // stale state could surface as a transient the first time a stereo
        // sample reuses the voice.  Resetting all four also removes a latent
        // click on any voice reuse.
        v->filterL.reset();
        v->filterR.reset();
        v->hpFilterL.reset();
        v->hpFilterR.reset();

        // Envelope params: drum-mode per-key overrides win over channel atomics.
        float aA = ampAttack.load();
        float aD = ampDecay.load();
        float aS = ampSustain.load();
        if (drumMode && note >= 0 && note < 128)
        {
            const auto& st = drumKeyStates[(size_t) note];
            const float ovA = st.userAttackMs.load();
            const float ovD = st.userDecayMs.load();
            const float ovS = st.userSustain.load();
            if (ovA >= 0.0f) aA = ovA;
            if (ovD >= 0.0f) aD = ovD;
            if (ovS >= 0.0f) aS = ovS;
        }

        v->ampEnv.curve = (AHDSREnvelope::Curve) ampCurve.load();
        v->ampEnv.noteOn  (aA,                 ampHold.load(),  aD,
                           aS,                  currentSampleRate);
        v->filtEnv.noteOn (filtAttack.load(),  filtHold.load(), filtDecay.load(),
                           filtSustain.load(), currentSampleRate);
        v->pitchEnv.noteOn(pitchAttack.load(), pitchHold.load(), pitchDecay.load(),
                           pitchSustain.load(), currentSampleRate);

        v->ampLfo.reset  (ampLfoRate.load(),   ampLfoDelay.load(),   currentSampleRate);
        v->filtLfo.reset (filtLfoRate.load(),  filtLfoDelay.load(),  currentSampleRate);
        v->pitchLfo.reset(pitchLfoRate.load(), pitchLfoDelay.load(), currentSampleRate);
    }

    //==========================================================================
    void Channel::noteOn(int note, int velocity, bool fromEditor, float extraGain)
    {
        if (note < 0 || note > 127) return;   // srcNote / latch tables index by it

        auto pv = std::atomic_load (&active);
        if (!ready.load() || ! pv || pv->regions.empty()) return;
        const auto& regions = pv->regions;
        const auto& samples = pv->samples;

        // ── VELOCITY CURVE, AND THIS IS THE ONLY PLACE IT IS APPLIED ─────────
        //
        // Every note reaches the engine through Channel::noteOn - the style
        // dispatcher, the player's keyboard, the editor's audition - so one
        // insertion point covers all three and they cannot disagree.
        //
        // Ahead of findRegion on purpose: velocity does not only set level, it
        // picks WHICH LAYER speaks.  Curving after layer selection would give a
        // quieter version of the same slammed sample; curving before it selects
        // the softer sample, which is the whole point on a drum kit.
        //
        // A drum channel reads the curve PER KEY, so a composed kit can carry a
        // different one on the kick than on the hats; a melodic channel has one
        // for the slot.
        {
            // ── THE KEYBOARD FIRST, THEN THE INSTRUMENT ──────────────────────
            //
            // Two curves, applied in that order and NOT summed.  The global one
            // corrects the CONTROLLER - most keyboards fall away steeply below
            // about velocity 65, so a soft passage arrives as a few notes that
            // barely speak - and the per-slot one is a voicing decision about
            // one instrument.  Correcting the input before voicing it is the
            // only order in which either means what it says.
            //
            // Summing the two curve values would look simpler and be wrong: the
            // shape is x^gamma with gamma exponential in the curve, so applying
            // them in sequence multiplies the exponents while adding the values
            // multiplies them again at a different rate.  Sequence is also what
            // matches the signal chain, which is what a reader will assume.
            //
            // apply() is a no-op at curve 0, so an install that has never
            // touched the global slider pays one atomic load and nothing else.
            velocity = VelCurve::apply (velocity, globalVelCurve.load());

            const bool isDrum = isDrumChannel.load();
            const float c = isDrum ? drumKeyStates[(size_t) note].velCurve.load()
                                   : slotVelCurve.load();
            velocity = VelCurve::apply (velocity, c);
        }

        // ── The ONE place a note's sounding pitch is decided ─────────────────
        // Octave slider + octave bias + GLOBAL TRANSPOSE, combined into a single
        // shift so every one of them inherits the per-note latch below.
        //
        // Drums are exempt from the semitone transpose: a kit's notes are KEYS,
        // not pitches, and shifting them would turn a kick into a tom.  (The
        // octave terms are left as they were — they are per-slot controls the
        // user aims deliberately, not a global key change.)
        //
        // OUT OF RANGE IS DROPPED, NOT CLAMPED.  The old jlimit meant a large
        // shift collapsed every note past the edge onto 0 or 127: several keys
        // producing one pitch, and their note-offs all aiming at that same
        // pitch.  A note shifted off the keyboard simply does not sound, which
        // is what hardware does and what makes the pairing unambiguous.
        const int octave         = octaveOffset.load() + octaveBias.load();
        const int shift          = octave * 12
                                 + (isDrumChannel.load() ? 0 : semitoneOffset.load());
        int transposedNote = note + shift;
        if (transposedNote < 0 || transposedNote > 127) return;

        //----------------------------------------------------------------------
        // LOW-KEY DRUM MIRROR — notes 12..35 play the 36..47 samples.
        //
        // Yamaha XG kits map from note 13 upward, not 35.  Everything below the
        // GM range is real, named percussion on the hardware, and on the
        // extended arranger kits it is where the extra kicks live — Yamaha's own
        // Dance Kit puts "Kick Dance 1" on note 13 and "Kick Dance 2" on 14,
        // where the Standard kit has Surdo Mute and Surdo Open.
        //
        // Grex's kits only cover 35..81, so every note under 35 hit nothing and
        // died silently (findRegion has no drum fallback by design).  In
        // DeepHousePop_T152 that was 330 hits, including the entire
        // four-on-the-floor kick sitting on note 24 — C0 in Yamaha numbering,
        // 160 strikes, completely inaudible.
        //
        // The fix mirrors upward in octaves until a mapped key is found, which
        // recovers 24 -> 36 Kick, 27 -> 39 Hand Clap, 32 -> 44 Pedal Hi-Hat.
        // ADDITIVE, never destructive: a key that IS mapped is left exactly
        // alone, so a future kit that fills 13..34 properly overrides this
        // without a code change, and nothing that plays today can change.
        //
        // Done HERE, before the piano-strip latch, rather than by widening the
        // kit: SampleData holds its AudioBuffer BY VALUE, so building mirror
        // regions would copy the audio for up to 24 keys x every velocity layer,
        // per pooled kit.  Folding the note costs nothing and has a second
        // benefit — the choke lookup below sees the FOLDED note, so a hit on 32
        // chokes the open hat through 44's group without any mirrored table.
        //----------------------------------------------------------------------
        if (isDrumChannel.load() && transposedNote < 36
            && ! hasRegionForNote (transposedNote))
        {
            for (int cand = transposedNote + 12; cand <= 47; cand += 12)
                if (hasRegionForNote (cand)) { transposedNote = cand; break; }
        }

        // ── Piano strip: KEY DOWN ────────────────────────────────────────────
        // Tracked from the MIDI event itself (see keyDownCount in Channel.h).
        // The trigger latch guarantees the note stays visible for at least one UI
        // poll — a style drum hit is often shorter than a single 30 Hz frame, so
        // without it the hit lands between polls and is never drawn at all.
        //
        // Latch the EFFECTIVE shift under the source key so the matching noteOff
        // decrements this exact index no matter where the OCTAVE slider is then.
        keyDownXpose[(size_t) note] = (int8_t) (transposedNote - note);
        {
            // Editor notes count on their OWN tally so the strip can show a key
            // you are holding independently of whatever the style is doing.
            uint8_t& c = fromEditor ? editorKeyDown[(size_t) transposedNote]
                                    : keyDownCount [(size_t) transposedNote];
            if (c < 255) ++c;
        }
        noteLatch[(size_t) transposedNote] = (int) (currentSampleRate * 0.055);   // ~55 ms

        const int regionIdx = findRegion(transposedNote, velocity);
        if (regionIdx < 0 || regionIdx >= (int) samples.size()) return;
        if (samples[(size_t) regionIdx].buffer.getNumSamples() == 0) return;

        const bool drum = isDrumChannel.load();
        // Editor notes are ALWAYS polyphonic, even on a mono slot.  The mono
        // contract ("one voice, last note wins") describes the STYLE's bass
        // line; applying it to the piano strip meant the style's next bass note
        // silently took the voice out from under a key you were holding — the
        // headline "can't play manually while the style plays" symptom, and the
        // reason the drum strip (always Poly) felt fine by comparison.
        // styleForcedPoly: the loaded style strikes chords into this slot, so a
        // mono voice would collapse them into a portamento sweep instead of
        // sounding them side by side.  See Channel::setStyleForcedPoly.
        const int  mode = (drum || fromEditor || styleForcedPoly.load())
                            ? Poly : playMode.load();

        if (mode == Poly)
        {
            // Choke: a new drum hit in a choke group fast-releases any still-
            // ringing voice from the SAME group (closed/foot hat cut the open
            // hat).  ~4 ms release, not a hard stop, to avoid a click.
            if (drum)
            {
                const int cg = drumChokeGroup[(size_t) transposedNote].load();
                if (cg != 0)
                    for (auto& v : voices)
                        if (v.active && v.note >= 0 && v.note < 128
                            && drumChokeGroup[(size_t) v.note].load() == cg)
                            v.ampEnv.noteOff (4.0f, currentSampleRate);
            }

            Voice* v = allocateVoice();
            if (v != nullptr)
            {
                startVoice(v, transposedNote, velocity, regionIdx, false);
                v->srcNote    = note;   // pairing identity — see Voice::srcNote
                v->fromEditor = fromEditor;
                v->extraGain  = extraGain;
            }

            // ── KICK MIX layering (notes 35 & 36) ────────────────────────────
            // Live, atomic-driven equal-power crossfade between the kit/low kick
            // and a supplemental EDM/WOOD kick.  The main voice is scaled by
            // cos(a·π/2); a second voice plays the tagged blend-layer region for
            // the current variant at sin(a·π/2).  No re-compose, no clearPreset;
            // survives style program changes (layers ride in the pooled voice).
            // NINE COMPOSED GM KITS ONLY -- see Channel::fullKitActive.  A
            // sampled kit has no tagged blend layers, so without this guard a
            // MIX left enabled from a composed kit would duck the sampled kick
            // and put nothing in its place.
            if (drum && v != nullptr && ! fullKitActive.load()
                && (transposedNote == 33 || transposedNote == 35 || transposedNote == 36))
            {
                auto& ks = drumKeyStates[(size_t) transposedNote];
                if (ks.kickMixEnabled.load())
                {
                    const float a     = juce::jlimit (0.0f, 1.0f, ks.kickMixAmount.load() / 100.0f);
                    // Equal-power (constant-power) crossfade: the combined kick
                    // holds a steady level across the whole sweep — no ~3 dB dip
                    // in the middle that a linear fade gives for two different
                    // kicks.  0% -> original only, 50% -> both at ~0.707,
                    // 100% -> blend kick only.
                    const float kitG  = std::cos (a * juce::MathConstants<float>::halfPi);
                    const float suppG = std::sin (a * juce::MathConstants<float>::halfPi);

                    v->kickMixGain = kitG;   // fade the kit/low kick as MIX rises

                    const int variant = juce::jlimit (0, 1, ks.kickMixVariant.load());
                    int layerIdx = -1;
                    for (int i = 0; i < (int) regions.size(); ++i)
                        if (regions[(size_t) i].kickMixVariant == variant
                            && transposedNote >= regions[(size_t) i].lokey
                            && transposedNote <= regions[(size_t) i].hikey)
                        { layerIdx = i; break; }

                    if (layerIdx >= 0 && layerIdx < (int) samples.size()
                        && samples[(size_t) layerIdx].buffer.getNumSamples() > 0)
                        if (Voice* lv = allocateVoice())
                        {
                            startVoice (lv, transposedNote, velocity, layerIdx, false);
                            lv->kickMixGain = suppG;
                            lv->srcNote     = note;   // same identity as the kit voice
                            lv->fromEditor  = fromEditor;
                            lv->extraGain   = extraGain;
                        }
                }
            }
            return;
        }

        // -------------------------------------------------------------- Mono
        // Three INDEPENDENT switches (multi-select — see setMonoConfig /
        // applyParams), not one 3-way mode:
        //   monoRetrigNew    : a newly played note restarts the sample + envelope
        //                      (articulate).  Off = legato: glide/jump the pitch
        //                      of the running voice and keep it sounding.
        //   monoHoldStolen   : on note-off, fall back to a still-held earlier
        //                      note (note priority).  Handled in noteOff().
        //   monoRetrigStolen : whether that fallback note restarts.  noteOff().
        // Instance-correct held stack: every note-on ADDS an entry, even for a
        // pitch already held (authored same-pitch overlaps).  The stack then
        // needs one note-off PER INSTANCE to empty — the old dedup collapsed an
        // overlap to a single entry, so the FIRST off emptied the stack and
        // released everything, killing the still-authored second note ("bass
        // stops randomly").  Newest instance sits at the back (last-note prio).
        //
        // The stack holds SOURCE notes (the identity domain, like Voice::srcNote)
        // — never the octave-shifted pitch — so a live OCTAVE move between a
        // note's on and off can't leak entries and hang the mono voice.
        heldNotes.push_back(note);

        Voice* existing = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.fromEditor          // editor voices aren't the mono voice
                && ! v.fadingOut                    // retiring half of a crossfade
                && v.ampEnv.stage != AHDSREnvelope::Idle
                && v.ampEnv.stage != AHDSREnvelope::Release)
            { existing = &v; break; }

        //----------------------------------------------------------------------
        // A LEGATO GLIDE MAY NOT CROSS A REGION BOUNDARY.
        //
        // The glide below moves a RUNNING voice to the new pitch and keeps its
        // sample position — that is the whole point of it.  But it also swaps
        // regionIndex, so if the new note resolves to a DIFFERENT sample the
        // voice carries on reading at the same position out of a different
        // buffer.  Two unrelated waveforms spliced at an arbitrary phase is a
        // step discontinuity: a click, as loud as the envelope happens to be at
        // that instant, and silent-to-vicious depending on where the two
        // waveforms sat — which is exactly why it is so hard to reproduce.
        //
        // It shows up on BASS because bass is the only style slot that is mono
        // AND glides (monoRetrigNew = false) AND sustains at 1.0, so every
        // splice happens at full amplitude with nothing to mask it.
        //
        // Crossing a region boundary therefore takes the fresh-note path
        // instead: the old voice fast-releases over ~5 ms and the new sample
        // starts from its own beginning.  Within a region the glide is
        // untouched, so ordinary legato still glides.
        //----------------------------------------------------------------------
        const bool crossesRegion = (existing != nullptr && existing->regionIndex != regionIdx);

        // Crossing a region while gliding: crossfade rather than splice or
        // re-attack.  Only when the player asked for a glide — with
        // monoRetrigNew set, a new note is MEANT to re-articulate and the
        // fresh-note path below is the correct answer.
        if (crossesRegion && ! monoRetrigNew.load() && kMonoXfadeMs > 0.0f)
        {
            Voice* nv = allocateVoice();

            // Under voice exhaustion allocateVoice steals, and the victim it
            // picks can be `existing` itself — which reset() has just wiped.
            // Copying from it would start the new note from a blank voice, so
            // drop to the fast-release path instead: still clickless, just
            // without the glide.
            if (nv == existing) nv = nullptr;

            auto pv = std::atomic_load (&active);

            // regionIdx came from findRegion on this same preset, but the
            // pointer can be swapped from the message thread between the two, so
            // re-validate rather than trust it.
            const bool canXfade = (nv != nullptr && pv != nullptr
                                   && regionIdx >= 0
                                   && regionIdx < (int) pv->samples.size()
                                   && regionIdx < (int) pv->regions.size());

            if (canXfade)
            {
                const auto& regs = pv->regions;
                const auto& smps = pv->samples;
                const auto& r    = regs[(size_t) regionIdx];

                // Inherit EVERYTHING, envelopes included — that is what makes
                // this a glide and not a re-attack — then re-point the copy at
                // the new sample from its own start.
                *nv = *existing;

                nv->regionIndex      = regionIdx;
                nv->note             = transposedNote;
                nv->srcNote          = note;          // identity follows the new note
                nv->regionGain       = juce::Decibels::decibelsToGain (r.volume);
                nv->looping          = r.hasLoop;
                nv->loopStart        = r.loopStart;
                nv->loopEnd          = r.loopEnd;
                nv->pitchKeycenter   = r.pitchKeycenter;
                nv->tuneCents        = r.tune;
                nv->sourceSampleRate = smps[(size_t) regionIdx].sampleRate;
                nv->playPosition     = (double) r.offset;
                nv->startSerial      = ++voiceSerialCounter;

                nv->targetPitchSemitones =
                    (double)(transposedNote - r.pitchKeycenter) + (double) r.tune / 100.0;
                if (! isDrumChannel.load())
                {
                    const int pitchClass = ((transposedNote % 12) + 12) % 12;
                    nv->targetPitchSemitones +=
                        (double) scaleTuningCents[pitchClass].load() / 100.0;
                }

                // Pitch continues from where the old voice actually was, so
                // portamento is unbroken across the boundary.
                const float glideMs = portamentoTime.load();
                if (glideMs > 0.1f)
                {
                    const double steps = currentSampleRate * (double) glideMs / 1000.0;
                    nv->glideCoeff = glideCoeffFor ((double) glideMs, currentSampleRate);
                    nv->portamentoActive = true;
                }
                else
                {
                    nv->currentPitchSemitones = nv->targetPitchSemitones;
                    nv->portamentoActive = false;
                }

                const float inc = 1.0f / juce::jmax (1.0f,
                                      (float) currentSampleRate * kMonoXfadeMs * 0.001f);

                nv->xfadeGain = 0.0f;  nv->xfadeInc = +inc;  nv->fadingOut = false;

                // The old voice keeps its own sample and fades out.  Its srcNote
                // is cleared so a note-off for this key pairs with the NEW voice
                // and never resurrects the one on its way out.
                existing->xfadeInc  = -inc;
                existing->fadingOut = true;
                existing->srcNote   = -1;
                return;
            }
            // No free voice: fall through and take the fast-release path, which
            // is still clickless — just without the glide.
        }

        if (monoRetrigNew.load() || existing == nullptr || crossesRegion)
        {
            // Fresh note: fast-release any running voice (~5 ms, no click) and
            // start a new one from the sample start with fresh envelopes — or
            // there is nothing sounding to glide from.  glideFromCurrent only
            // affects pitch (portamento); the envelope always re-attacks.
            if (existing != nullptr)
                for (auto& v : voices)
                    if (v.active && ! v.fromEditor)   // never guillotine a held strip note
                        v.ampEnv.noteOff(5.0f, currentSampleRate);

            if (Voice* v = allocateVoice())
            {
                startVoice(v, transposedNote, velocity, regionIdx, existing != nullptr);
                v->srcNote = note;   // pairing identity — see Voice::srcNote
            }
            return;
        }

        // Legato: move the running voice to the new pitch, keeping its envelope
        // and sample position (smooth, no re-attack).
        //
        // Reached only when the new note lands in the SAME region — see
        // crossesRegion above — so the sample under the read position does not
        // change and the position stays meaningful.  regionIndex is re-assigned
        // anyway to keep the voice self-consistent; it is the same value.
        {
            const auto& r = regions[(size_t) regionIdx];
            existing->note          = transposedNote;
            existing->srcNote       = note;   // identity follows the new note
            existing->regionIndex   = regionIdx;   // unchanged by construction
            existing->targetPitchSemitones =
                (double)(transposedNote - r.pitchKeycenter) + (double) r.tune / 100.0;
            if (! isDrumChannel.load())
            {
                const int pitchClass = ((transposedNote % 12) + 12) % 12;
                existing->targetPitchSemitones +=
                    (double) scaleTuningCents[pitchClass].load() / 100.0;
            }
            existing->pitchKeycenter   = r.pitchKeycenter;
            existing->tuneCents        = r.tune;
            existing->sourceSampleRate = samples[(size_t) regionIdx].sampleRate;

            const float glideMs = portamentoTime.load();
            if (glideMs > 0.1f)
            {
                existing->glideCoeff = glideCoeffFor ((double) glideMs, currentSampleRate);
                existing->portamentoActive = true;
            }
            else
            {
                existing->currentPitchSemitones = existing->targetPitchSemitones;
                existing->portamentoActive = false;
            }
        }
    }

    //==========================================================================
    void Channel::noteOff(int note, bool fromEditor)
    {
        if (note < 0 || note > 127) return;   // srcNote / latch tables index by it

        auto pv = std::atomic_load (&active);
        static const std::vector<Region> kNoRegions;
        const auto& regions = pv ? pv->regions : kNoRegions;

        // The strip index and the drum per-key lookups use the shift LATCHED at
        // this key's note-on (keyDownXpose) — NEVER the live octave atomics —
        // so an OCTAVE-slider move between on and off still lands on exactly
        // the index (and drum key) the note-on used.  Voice pairing below is by
        // srcNote and doesn't need any transposition at all.
        const int transposedNote =
            juce::jlimit (0, 127, note + (int) keyDownXpose[(size_t) note]);

        const bool drum = isDrumChannel.load();
        // Mirrors noteOn: an editor note was started polyphonically, so it must
        // be released the same way or it would never pair with a voice.
        // styleForcedPoly: the loaded style strikes chords into this slot, so a
        // mono voice would collapse them into a portamento sweep instead of
        // sounding them side by side.  See Channel::setStyleForcedPoly.
        const int  mode = (drum || fromEditor || styleForcedPoly.load())
                            ? Poly : playMode.load();

        // ── Piano strip: KEY UP ──────────────────────────────────────────────
        // This MUST run before every early-out below — above all the drum
        // full-length return, which deliberately ignores note-off for AUDIO
        // (one-shots ring to their natural end) but does NOT mean the key is
        // still down.  Skipping it there is what pinned notes on permanently.
        {
            uint8_t& c = fromEditor ? editorKeyDown[(size_t) transposedNote]
                                    : keyDownCount [(size_t) transposedNote];
            if (c > 0) --c;
        }

        if (mode == Poly)
        {
            // Instance-correct pairing: ONE note-off releases ONE voice — the
            // OLDEST still-sounding voice with this SOURCE note (lowest
            // startSerial, not already in Release).  Style recordings routinely
            // OVERLAP the same pitch; releasing every matching voice let the
            // FIRST note's off chop the newer overlapping strike ("short
            // notes").  Matching by srcNote (not a recomputed shift) is what
            // makes the pairing immune to live OCTAVE moves.
            if (drum && drumKeyStates[(size_t) transposedNote].fullLength.load())
                return;      // one-shot (full-length) drum keys ignore note-off

            // Ownership is part of the identity: a strip release must not free
            // the STYLE's voice on the same note, and vice versa — otherwise
            // playing along with the style releases its notes for it.
            Voice* target = nullptr;
            for (auto& v : voices)
                if (v.active && v.srcNote == note && v.fromEditor == fromEditor
                    && v.ampEnv.stage != AHDSREnvelope::Idle
                    && v.ampEnv.stage != AHDSREnvelope::Release)
                    if (target == nullptr || v.startSerial < target->startSerial)
                        target = &v;
            if (target == nullptr) return;   // nothing sounding — already released

            // Drum-mode per-key release override (sentinel -1 → fall through).
            // Keyed by the voice's ACTUAL sounding key, which is also immune to
            // slider moves (it was latched into the voice at note-on).
            float relMs = ampRelease.load();
            if (drum && target->note >= 0 && target->note < 128)
            {
                const float ov = drumKeyStates[(size_t) target->note].userReleaseMs.load();
                if (ov >= 0.0f) relMs = ov;
            }
            target->ampEnv.noteOff (relMs,              currentSampleRate);
            target->filtEnv.noteOff(filtRelease.load(), currentSampleRate);
            return;
        }

        // -------------------------------------------------------------- Mono
        // Was the released note the one actually sounding?  Releasing an older,
        // non-sounding held key must not disturb the current voice.  Identity
        // check, like the poly path: by SOURCE note.
        bool wasActive = false;
        for (auto& v : voices)
            if (v.active && v.srcNote == note && ! v.fromEditor
                && v.ampEnv.stage != AHDSREnvelope::Idle
                && v.ampEnv.stage != AHDSREnvelope::Release)
            { wasActive = true; break; }

        // Erase ONE instance (the oldest) — see the note-on side: overlapped
        // same-pitch notes each own an entry, so each off consumes exactly one.
        // heldNotes is in the SOURCE domain, so this erase can never miss after
        // an octave move — which is precisely what used to leak the stack and
        // hang the mono (bass) voice.
        {
            auto it = std::find (heldNotes.begin(), heldNotes.end(), note);
            if (it != heldNotes.end()) heldNotes.erase (it);
        }

        if (heldNotes.empty())
        {
            for (auto& v : voices)
                if (v.active)
                {
                    v.ampEnv.noteOff (ampRelease.load(),  currentSampleRate);
                    v.filtEnv.noteOff(filtRelease.load(), currentSampleRate);
                }
            return;
        }

        // Notes still held.  Only react if the released note was the sounding one.
        if (! wasActive) return;

        if (! monoHoldStolen.load())
        {
            // No note-priority fallback: releasing the sounding note ends it even
            // though earlier keys are still down.
            for (auto& v : voices)
                if (v.active)
                {
                    v.ampEnv.noteOff (ampRelease.load(),  currentSampleRate);
                    v.filtEnv.noteOff(filtRelease.load(), currentSampleRate);
                }
            return;
        }

        // Hold-stolen: fall back to the most recently played still-held note.
        // heldNotes is in the SOURCE domain, so re-derive the sounding pitch at
        // the CURRENT octave: the fallback is a fresh pitch target, and a fresh
        // target adopting the slider's present position is exactly the "new
        // notes pick up the new octave" rule.
        const int returnSrc    = heldNotes.back();
        const int octaveNow    = octaveOffset.load() + octaveBias.load();
        const int returnNote   = juce::jlimit (0, 127, returnSrc + octaveNow * 12);
        const int returnRegion = findRegion(returnNote, 64);
        if (returnRegion < 0) return;

        Voice* existing = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.fromEditor          // editor voices aren't the mono voice
                && ! v.fadingOut                    // retiring half of a crossfade
                && v.ampEnv.stage != AHDSREnvelope::Idle
                && v.ampEnv.stage != AHDSREnvelope::Release)
            { existing = &v; break; }
        if (existing == nullptr) return;

        // Fallback lands on the note already sounding (same-pitch overlap where
        // one instance was released) and no re-attack requested: nothing to do —
        // don't disturb the running voice's portamento/pitch state.  Identity
        // compare (source domain), matching the rest of the pairing logic.
        if (existing->srcNote == returnSrc && ! monoRetrigStolen.load())
            return;

        const auto& r = regions[(size_t) returnRegion];

        existing->note          = returnNote;
        existing->srcNote       = returnSrc;   // identity follows the fallback
        existing->regionIndex   = returnRegion;
        existing->targetPitchSemitones =
            (double)(returnNote - r.pitchKeycenter) + (double) r.tune / 100.0;

        const float glideMs = portamentoTime.load();
        if (glideMs > 0.1f)
        {
            existing->glideCoeff = glideCoeffFor ((double) glideMs, currentSampleRate);
            existing->portamentoActive = true;
        }
        else
        {
            existing->currentPitchSemitones = existing->targetPitchSemitones;
        }

        if (monoRetrigStolen.load())
        {
            // Fresh attack on the returned note (restart envelopes + sample).
            existing->ampEnv.curve = (AHDSREnvelope::Curve) ampCurve.load();
            existing->ampEnv.noteOn  (ampAttack.load(),  ampHold.load(),  ampDecay.load(),
                                      ampSustain.load(), currentSampleRate);
            existing->filtEnv.noteOn (filtAttack.load(), filtHold.load(), filtDecay.load(),
                                      filtSustain.load(), currentSampleRate);
            existing->pitchEnv.noteOn(pitchAttack.load(), pitchHold.load(), pitchDecay.load(),
                                      pitchSustain.load(), currentSampleRate);
            existing->playPosition = (double) r.offset;
        }
        // else: legato return — envelope and sample continue.
    }

    //==========================================================================
    // Re-pitch a sounding voice in place (no re-attack).  The voice is found by
    // its srcNote IDENTITY (never by recomputing the octave transform from the
    // live atomics — that lookup broke the instant the OCTAVE slider moved
    // between the note's start and this glide, failing the retune and letting
    // the StylePlayer fallback stack a fresh strike on top of a now-orphaned
    // voice).  The sounding delta is the source delta, applied to the voice's
    // OWN latched pitch, so the interval is exact and octave-invariant under
    // the (note - keycenter) pitch model.  The envelope, sample read position
    // and region are all left untouched, so the voice glides to the new chord
    // tone instead of restarting.
    bool Channel::retuneVoice(int oldNote, int newNote) noexcept
    {
        if (isDrumChannel.load()) return false;   // never re-pitch percussion
        if (oldNote < 0 || oldNote > 127 || newNote < 0 || newNote > 127)
            return false;

        for (auto& v : voices)
        {
            if (v.active && v.srcNote == oldNote && ! v.fromEditor)
            {
                const int soundingOld = v.note;
                const int soundingNew = juce::jlimit (0, 127,
                                                      soundingOld + (newNote - oldNote));

                // Piano strip: a chord change re-pitches a note that is still
                // HELD, and the style's note-off will later arrive for the NEW
                // source note (the player rewrites its tracking table).  Move
                // the key-down instance to the new sounding index — using the
                // voice's OWN old/new keys, not a recomputed transform — and
                // re-latch the shift under the new source key, so that off
                // decrements exactly what this glide left lit.
                if (soundingNew != soundingOld
                    && keyDownCount[(size_t) soundingOld] > 0)
                {
                    --keyDownCount[(size_t) soundingOld];
                    if (keyDownCount[(size_t) soundingNew] < 255)
                        ++keyDownCount[(size_t) soundingNew];
                    noteLatch[(size_t) soundingNew] =
                        juce::jmax (noteLatch[(size_t) soundingNew],
                                    (int) (currentSampleRate * 0.055));
                }
                keyDownXpose[(size_t) newNote] = (int8_t) (soundingNew - newNote);

                const double deltaSemi = (double) (soundingNew - soundingOld);
                v.targetPitchSemitones  += deltaSemi;
                v.currentPitchSemitones += deltaSemi;
                v.portamentoActive       = false;   // instant, in-tune jump
                v.note                   = soundingNew;
                v.srcNote                = newNote;

                // Keep the mono held-note stack in step with the re-pitched
                // voice.  The stack is in the SOURCE domain now, so rewrite the
                // old source note to the new one — mono noteOff keys off it to
                // decide release-vs-fallback; without this the later noteOff
                // for the new source finds the OLD one still parked here,
                // treats it as "a note ended but one's still held", and glides
                // the voice back instead of releasing it — a stuck note.  Poly
                // mode doesn't consult heldNotes for note-off, so this is inert
                // there.  In-place overwrite: no size change, no allocation,
                // audio-thread safe.
                for (auto& hn : heldNotes)
                    if (hn == oldNote) { hn = newNote; break; }

                return true;
            }
        }
        return false;
    }

    //==========================================================================
    void Channel::allNotesOff()
    {
        for (auto& v : voices)
            if (v.active) v.ampEnv.noteOff(5.0f, currentSampleRate);
        heldNotes.clear();
        // Clear the piano strip: a stop / section flush kills every note, and any
        // note-offs the style would have sent for them are never coming.  Without
        // this the keys they were holding would stay lit indefinitely.
        for (auto& c : keyDownCount)  c = 0;
        for (auto& c : editorKeyDown) c = 0;
        for (auto& L : noteLatch)     L = 0;
        for (auto& x : keyDownXpose)  x = 0;
    }

    void Channel::pitchBend(float normalized)
    {
        currentPitchBendValue = juce::jlimit(-1.0f, 1.0f, normalized);
    }

    //==========================================================================
    // applyParams — message-thread push of a ChannelParams snapshot.
    //==========================================================================
    void Channel::applyParams(const ChannelParams& p)
    {
        setSlotVelCurve (p.velCurve);
        channelPan.store (juce::jlimit (-1.0f, 1.0f, p.pan));
        // instrumentGain is deliberately absent — see its declaration.  A bulk
        // params push must not be able to move a calibration.
        ampAttack .store (p.ampAttack);
        ampHold   .store (p.ampHold);
        ampDecay  .store (p.ampDecay);
        ampSustain.store (p.ampSustain);
        ampRelease.store (p.ampRelease);
        ampCurve  .store (juce::jlimit (0, 2, p.ampCurve));

        filterType    .store (juce::jlimit (0, 3, p.filterType));
        filterCutoff  .store (juce::jlimit (20.0f, 20000.0f, p.filterCutoff));
        filterReso    .store (juce::jlimit (0.0f, 1.0f,      p.filterReson));
        filterKeytrack.store (p.filterKeytrack);
        // bandHpFreq / bandLpFreq are deliberately absent, exactly like
        // instrumentGain above.  This function runs on EVERY program change,
        // and a program change must not be able to reopen a band the player
        // set on the channel strip.  setBandFilterHz is the only way in.

        filtAttack   .store (p.filtAttack);
        filtHold     .store (p.filtHold);
        filtDecay    .store (p.filtDecay);
        filtSustain  .store (p.filtSustain);
        filtRelease  .store (p.filtRelease);
        filtEnvAmount.store (juce::jlimit (0.0f, 1.0f, p.filtEnvAmount));

        pitchAttack  .store (p.pitchAttack);
        pitchHold    .store (p.pitchHold);
        pitchDecay   .store (p.pitchDecay);
        pitchSustain .store (p.pitchSustain);
        pitchRelease .store (p.pitchRelease);
        // Pitch env removed from this instrument — force depth to 0 so the
        // render loop's depth gate skips it regardless of preset/style data.
        pitchEnvDepth.store (0.0f);

        playMode        .store (juce::jlimit (0, 1, p.playMode));
        monoHoldStolen  .store (p.monoHoldStolen);
        monoRetrigNew   .store (p.monoRetrigNew);
        monoRetrigStolen.store (p.monoRetrigStolen);
        portamentoTime  .store (juce::jmax (0.0f, p.portamentoTime));
        octaveOffset    .store (juce::jlimit (-3, 3, p.octaveOffset));

        ampLfoRate   .store (p.ampLfoRate);
        ampLfoDepth  .store (juce::jlimit (0.0f, 1.0f, p.ampLfoDepth));
        ampLfoDelay  .store (p.ampLfoDelay);
        ampLfoEnabled.store (p.ampLfoEnabled);
        filtLfoRate  .store (p.filtLfoRate);
        filtLfoDepth .store (juce::jlimit (0.0f, 1.0f, p.filtLfoDepth));
        filtLfoDelay .store (p.filtLfoDelay);
        filtLfoEnabled.store (p.filtLfoEnabled);
        pitchLfoRate .store (p.pitchLfoRate);
        // Pitch LFO removed from this instrument — force depth to 0 so the
        // render loop's depth gate skips it regardless of preset/style data.
        pitchLfoDepth.store (0.0f);
        pitchLfoDelay.store (p.pitchLfoDelay);

        for (int i = 0; i < 5; ++i)
        {
            eqGain[i].store (juce::jlimit (-24.0f, 24.0f, p.eqGain[i]));
            eqFreq[i].store (juce::jlimit ( 20.0f, 20000.0f, p.eqFreq[i]));
        }

        reverbSize.store (juce::jlimit (0.0f, 1.0f, p.reverbSize));
        reverbDamp.store (juce::jlimit (0.0f, 1.0f, p.reverbDamp));
        reverbWet .store (juce::jlimit (0.0f, 1.0f, p.reverbWet));
        reverbDry .store (juce::jlimit (0.0f, 1.0f, p.reverbDry));
        reverbTail.store (juce::jlimit (0.0f, 1.0f, p.reverbTail));
        reverbHpNorm.store (juce::jlimit (0.0f, 1.0f, p.reverbHpNorm));
        reverbLpNorm.store (juce::jlimit (0.0f, 1.0f, p.reverbLpNorm));
        reverbPreDelay.store (juce::jlimit (0.0f, 1.0f, p.reverbPreDelay));
        reverbAlgo.store (juce::jlimit (0, 1, p.reverbAlgo));

        delayTimeSig .store (juce::jlimit (0, 1, p.delayTimeSig));
        delayDiv     .store (juce::jmax  (0,    p.delayDiv));
        delayFeedback.store (juce::jlimit (0.0f, 0.95f, p.delayFeedback));
        delayWet     .store (juce::jlimit (0.0f, 1.0f,  p.delayWet));
        delayDry     .store (juce::jlimit (0.0f, 1.0f,  p.delayDry));
        // Bases go up to 2.0: the box is a calibration, and a quiet source can
        // legitimately need more than unity out of full travel.
        delayWetBase .store (juce::jlimit (0.0f, 2.0f,  p.delayWetBase));
        reverbWetBase.store (juce::jlimit (0.0f, 2.0f,  p.reverbWetBase));

        eqEnabled    .store (p.eqEnabled);
        reverbEnabled.store (p.reverbEnabled);
        delayEnabled .store (p.delayEnabled);

        // ── Sounds-path insert FX ──────────────────────────────────────────
        chorusEnabled .store (p.chorusEnabled);
        chorusRate    .store (juce::jlimit (0.0f, 1.0f,   p.chorusRate));
        chorusDepth   .store (juce::jlimit (0.0f, 1.0f,   p.chorusDepth));
        chorusMix     .store (juce::jlimit (0.0f, 1.0f,   p.chorusMix));

        wahEnabled    .store (p.wahEnabled);
        wahSensitivity.store (juce::jlimit (0.0f, 1.0f,    p.wahSensitivity));
        wahRate       .store (juce::jlimit (0.05f, 8.0f,   p.wahRate));
        wahLfoDepth   .store (juce::jlimit (0.0f, 1.0f,    p.wahLfoDepth));
        wahBaseHz     .store (juce::jlimit (80.0f, 1500.0f, p.wahBaseHz));
        wahQ          .store (juce::jlimit (0.0f, 1.0f,    p.wahQ));
        wahMix        .store (juce::jlimit (0.0f, 1.0f,    p.wahMix));

        phaserEnabled .store (p.phaserEnabled);
        phaserRate    .store (juce::jlimit (0.0f, 1.0f,   p.phaserRate));
        phaserDepth   .store (juce::jlimit (0.0f, 1.0f,   p.phaserDepth));
        phaserFeedback.store (juce::jlimit (0.0f, 1.0f,   p.phaserFeedback));
        phaserMix     .store (juce::jlimit (0.0f, 1.0f,   p.phaserMix));
    }

    //==========================================================================
    // setDrumKeyParams — live per-key edit (message thread).
    // Sentinel -1.0f means "inherit channel atomic"; callers should pass the
    // user's actual edit values (NOT -1) for the fields they want to override.
    //==========================================================================
    // LENGTH (0..100) → amp-envelope (decay ms, sustain 0..1).
    //   100 = full length: sustain holds at 1.0 so a one-shot drum plays to the
    //         sample's natural end at full level (decay time is then irrelevant).
    //   <100 = the hit fades to silence over a decay time that scales with LENGTH,
    //          giving a usable "how long does it ring" control.
    static void drumLengthToAmp (float length, float& decayMs, float& sustain) noexcept
    {
        const float L = juce::jlimit (0.0f, 100.0f, length);
        sustain = (L >= 99.5f) ? 1.0f : 0.0f;
        decayMs = juce::jmap (L, 0.0f, 100.0f, 5.0f, 6000.0f);
    }

    //==========================================================================
    void Channel::setDrumKeyParams (int midiKey, const DrumElementParams& p)
    {
        if (midiKey < 0 || midiKey >= 128) return;
        auto& st = drumKeyStates[(size_t) midiKey];
        st.userGain      .store (juce::jlimit (0.0f, 2.0f, p.gain));
        st.userPitchSemi .store (juce::jlimit (-24.0f, 24.0f, p.pitch));
        st.velCurve      .store (juce::jlimit (-1.0f, 1.0f, p.velCurve));
        st.userCutoffNorm.store (juce::jlimit (0.0f, 1.0f, p.filterCutoff));
        st.userHpNorm    .store (juce::jlimit (0.0f, 1.0f, p.filterHpNorm));
        st.userLpNorm    .store (juce::jlimit (0.0f, 1.0f, p.filterLpNorm));
        // ADSR fields stored in DrumElementParams are in SECONDS; engine wants ms.
        // Attack is fixed at the lowest level (instant) for drums, and every
        // drum key plays its full sample (one-shot, note-off ignored) — these
        // are no longer per-key editable.
        st.userAttackMs  .store (0.0f);
        // Combined LENGTH → amp decay time + sustain level.
        {
            float decMs, susLvl;
            drumLengthToAmp (p.length, decMs, susLvl);
            st.userDecayMs .store (decMs);
            st.userSustain .store (susLvl);
        }
        st.userReleaseMs .store (juce::jmax (0.0f, p.release * 1000.0f));
        st.roundRobinAmount.store (juce::jlimit (0, 100, p.roundRobinAmount));
        st.fullLength    .store (true);
        st.userFxSendNorm.store (juce::jlimit (0.0f, 1.0f, p.fxSend));

        // KICK MIX (live): amount / variant / on-off drive the supplemental
        // voice's equal-power gain at the next hit — no re-compose needed.
        st.kickMixEnabled.store (p.kickMixEnabled);
        st.kickMixAmount .store (juce::jlimit (0.0f, 100.0f, p.kickMixAmount));
        st.kickMixVariant.store (juce::jlimit (0, 1, p.kickMixVariant));

        if (midiKey == 35 || midiKey == 36)
            kmLog ("setDrumKeyParams key=" + juce::String (midiKey)
                   + " enabled=" + juce::String (p.kickMixEnabled ? 1 : 0)
                   + " amount="  + juce::String (p.kickMixAmount, 1)
                   + " variant=" + juce::String (p.kickMixVariant));
    }

    void Channel::setDrumKeyFxSend (int midiKey, float sendNorm)
    {
        if (midiKey < 0 || midiKey >= 128) return;
        drumKeyStates[(size_t) midiKey].userFxSendNorm.store (juce::jlimit (0.0f, 1.0f, sendNorm));
    }

    //==========================================================================
    // applyDrumKitFx — push kit-wide FX bus params onto the channel.
    // Atomics only; the actual coefficients are recomputed lazily on the
    // audio thread next render.
    //==========================================================================
    void Channel::applyDrumKitFx (const DrumKitFxParams& fx)
    {
        // Drum channel pan rides on the kit-FX bus (user PAN tab).
        channelPan.store (juce::jlimit (-1.0f, 1.0f, fx.pan));
        applySweetenerParams (fx.sweet);   // drum route into the shared block
        drumFxBus.eqEnabled  .store (fx.eqEnabled);
        drumFxBus.satEnabled .store (fx.satEnabled);
        drumFxBus.compEnabled.store (fx.compEnabled);
        drumFxBus.revEnabled .store (fx.revEnabled);
        drumFxBus.delEnabled .store (fx.delEnabled);

        for (int i = 0; i < 10; ++i)
            drumFxBus.eqGainDb[i].store (juce::jlimit (-60.0f, 20.0f, fx.eqGainDb[i]));

        drumFxBus.satDrive    .store (juce::jlimit (0.0f, 1.0f,  fx.satDrive));
        drumFxBus.satMix      .store (juce::jlimit (0.0f, 1.0f,  fx.satMix));

        drumFxBus.compThreshDb .store (juce::jlimit (-60.0f, 0.0f, fx.compThreshDb));
        drumFxBus.compRatio    .store (juce::jlimit (1.0f, 20.0f,  fx.compRatio));
        drumFxBus.compAttackMs .store (juce::jlimit (0.1f, 200.0f, fx.compAttackMs));
        drumFxBus.compReleaseMs.store (juce::jlimit (5.0f, 2000.0f, fx.compReleaseMs));
        drumFxBus.compMakeupDb .store (juce::jlimit (0.0f, 24.0f, fx.compMakeupDb));

        drumFxBus.revSize    .store (juce::jlimit (0.0f, 1.0f, fx.revSize));
        drumFxBus.revDamp    .store (juce::jlimit (0.0f, 1.0f, fx.revDamp));
        drumFxBus.revWet     .store (juce::jlimit (0.0f, 1.0f, fx.revWet));
        drumFxBus.revDry     .store (juce::jlimit (0.0f, 1.0f, fx.revDry));
        drumFxBus.revTail    .store (juce::jlimit (0.0f, 1.0f, fx.revTail));
        drumFxBus.revPreDelay.store (juce::jlimit (0.0f, 1.0f, fx.revPreDelay));
        drumFxBus.revHpNorm  .store (juce::jlimit (0.0f, 1.0f, fx.revHpNorm));
        drumFxBus.revLpNorm  .store (juce::jlimit (0.0f, 1.0f, fx.revLpNorm));
        drumFxBus.revAlgo    .store (juce::jlimit (0,    1,    fx.revAlgo));
        // Bases go to 2.0 for the same reason the melodic ones do - a
        // calibration, and a quiet kit can need more than unity.
        drumFxBus.revWetBase .store (juce::jlimit (0.0f, 2.0f, fx.revWetBase));
        drumFxBus.delDry     .store (juce::jlimit (0.0f, 1.0f, fx.delDry));
        drumFxBus.delWetBase .store (juce::jlimit (0.0f, 2.0f, fx.delWetBase));

        drumFxBus.delSync   .store (fx.delSync);
        drumFxBus.delTimeSig.store (juce::jlimit (0, 1, fx.delTimeSig));
        drumFxBus.delDiv    .store (juce::jmax  (0,    fx.delDiv));
        drumFxBus.delTimeMs.store (juce::jlimit (1.0f, 2000.0f, fx.delTimeMs));
        drumFxBus.delFb    .store (juce::jlimit (0.0f, 0.95f,   fx.delFb));
        drumFxBus.delWet   .store (juce::jlimit (0.0f, 1.0f,    fx.delWet));

        drumFxBus.fxWet    .store (juce::jlimit (0.0f, 1.0f,    fx.fxWet));
    }

    //==========================================================================
    // XG low-key substitution table (MIDI 25..34).
    //
    // Yamaha's XG drum map gives EVERY kit nine extra articulations below GM's
    // 35, and styles lean on them constantly:
    //
    //     25 Brush Tap        30 Castanet
    //     26 Brush Swirl      31 Snare Soft
    //     27 Brush Slap       32 Sticks
    //     28 Brush Tap Swirl  33 BASS DRUM SOFT   <-- XG's SECOND KICK
    //     29 Snare Roll       34 Open Rim Shot
    //
    // Note 33 is the one that matters.  It is not an oddity or a mis-authoring:
    // it is XG's soft kick, and whole classes of style -- brush, Latin, ballad --
    // put their ENTIRE kick pattern on it and never touch 35/36 at all.
    // PopLatinBallad_T158 is exactly that: 61 strong hits on 33, all on beats 1
    // and 3, and not a single 35 or 36 anywhere in the style.
    //
    // Grex ships no samples for these keys, so each one borrows the closest
    // instrument the kit ALREADY carries, played at its NATURAL pitch.  Because
    // the donor is the same kit, the substitution stays in character: a brush
    // kit's snare is a brush snare, a brush kit's kick is a brush kick.  No extra
    // blob to ship, and every kit gets the low keys for free.
    //
    // This replaces two worse answers:
    //   * findRegion's octave fallback, which borrowed the drum an octave UP and
    //     stretched it back down -- turning key 33 into a gong;
    //   * a separate the_first.frb, one more file to ship and keep in sync,
    //     carrying sounds the kit already has.
    //
    // `primary` is tried first, `secondary` only if the kit doesn't map it.  If
    // the kit maps neither, the key stays SILENT -- better than a wrong drum.
    //
    // ONE CRUCIAL TRIM.  Six of these keys (25..29 and 31) are XG's SOFT snare
    // articulations -- Brush Tap, Brush Swirl, Brush Slap, Brush Tap Swirl,
    // Snare Roll, Snare Soft -- but the only donor we have is the kit's FULL
    // snare.  Substituted at face value they hammer, and a style that leans on
    // them (a brush ballad's swirls, a country roll) comes out sounding like
    // MARCH music.  So anything that ends up borrowing the snare is pulled down
    // by 60% (to 0.40 gain, about -8 dB).
    //
    // The trim is keyed on the DONOR, not on the note: keys 30/32/34 normally
    // take the side stick, but they fall back to the snare on a kit that has no
    // 37 -- and when they do, they must be trimmed too.  The kick (33 -> 36)
    // is never touched: it has to stay strong, it's the style's kick.
    //==========================================================================
    struct XgLowKeySub { int primary; int secondary; };

    // Snare-derived low keys play at 40% gain -- i.e. 60% quieter.
    static constexpr float kXgSnareSubGain = 0.40f;

    static XgLowKeySub xgLowKeySubstitute (int key) noexcept
    {
        switch (key)
        {
            case 25: return { 38, 40 };   // Brush Tap       -> Snare  (else El.Snare)
            case 26: return { 38, 40 };   // Brush Swirl     -> Snare
            case 27: return { 38, 40 };   // Brush Slap      -> Snare
            case 28: return { 38, 40 };   // Brush Tap Swirl -> Snare
            case 29: return { 38, 40 };   // Snare Roll      -> Snare
            case 30: return { 37, 38 };   // Castanet        -> Side Stick (dry click)
            case 31: return { 38, 40 };   // Snare Soft      -> Snare
            case 32: return { 37, 38 };   // Sticks          -> Side Stick
            case 33: return { 36, 35 };   // BASS DRUM SOFT  -> the kit's KICK   <<<<
            case 34: return { 37, 40 };   // Open Rim Shot   -> Side Stick (else El.Snare)
            default: return { -1, -1 };
        }
    }

    //==========================================================================
    // composeDrumKit — build a PooledDrumKit (regions[] + samples[] + per-key
    // snapshot + FX) from a DrumKitParams.  HEAVY (decodes one sample per
    // mapped key) and message-thread only; performs NO member mutation.
    //
    // For each non-empty entry in `kit.keys`, resolve the source kit-blob
    // region via the registry, decode its samples, build a single-key
    // (lokey == hikey == k, pitchKeycenter == k) engine region, and record the
    // user-edit params in the matching snapshot entry.  publishDrumKit applies
    // the result to the channel.
    //==========================================================================
    Channel::PooledDrumKit Channel::composeDrumKit (const DrumKitRegistry& registry,
                                                    const DrumKitParams& kit)
    {
        // Heavy build ONLY — no member mutation.  Decodes one sample per mapped
        // key (message thread, not RT-safe) into a local PooledDrumKit that the
        // caller either pools (preloadDrumKit) or publishes (loadDrumKit /
        // selectPooledDrumKit).  The publish step is what actually swaps the
        // channel's active voice, and it is cheap.
        PooledDrumKit pk;
        pk.name = kit.lastLoadedKit;
        pk.fx   = kit.fx;

        auto kitVoice = std::make_shared<PresetVoice>();

        for (int k = 0; k < 128; ++k)
        {
            const auto& entry = kit.keys[(size_t) k];
            if (entry.sourceKit.isEmpty() || entry.sourceRoleId == 0) continue;

            // ── EVERY velocity layer for this key ────────────────────────
            // A layered blob carries several regions per key (soft / medium /
            // hard).  Build ONE engine Region per layer, each keeping the
            // layer's own velocity window, so findRegion() selects the sample the
            // incoming velocity actually asks for.
            //
            // The old code took a single element and gave it lovel 0 / hivel 127
            // -- every hit, ghost note or accent, fired the same (loudest)
            // sample.  Level moved a little; timbre never moved at all.  That is
            // what made the drums sound monotonically loud.
            auto layers = registry.findElementLayers (entry.sourceKit, entry.sourceRoleId);

            // ── Shared-kit fallback — THE SILENT-CRASH BUG ───────────────────
            //
            // The key map arriving here was built by resolveDrumKitParams from
            // getElementsForKit(), and that gather SUBSTITUTES the shared metal
            // library (kit "000") for every kit's own cymbals and hats — the
            // per-kit metal blobs have been abandoned.  appendSharedMetal then
            // RELABELS each borrowed handle with the requesting kit's name, so
            // the editor shows the metal as belonging to the kit that asked for
            // it.  Which means every metal entry on kits 008..048 says
            // sourceKit = "025" (etc.) while the samples live only under "000".
            //
            // The lookup above is a RAW per-kit search with no substitution, so
            // for those entries it came back empty, the `continue` below dropped
            // the key, and every cymbal key on every non-Standard kit composed
            // to NOTHING.  findRegion() on a drum channel deliberately has no
            // fallback (a wrong drum is worse than none), so the note was simply
            // silent — which is why the Crash tab (49 / 55 / 52 / 57 all land on
            // these keys) stayed mute through every fix aimed at the trigger
            // side: there was no sample under the trigger.
            //
            // The retry below resolves an empty lookup against the shared kit,
            // healing three producers at once: the relabelled style-load key
            // maps, per-key kit picks made in the DrumsPopup, and .drm presets
            // saved with the relabelled names.  Keys the user explicitly
            // silenced never reach here (sourceKit is EMPTY and the guard above
            // skips them), so deliberate silence is untouched.
            if (layers.empty()
                && ! entry.sourceKit.equalsIgnoreCase (DrumKitRegistry::kSharedMetalKit))
                layers = registry.findElementLayers (DrumKitRegistry::kSharedMetalKit,
                                                     entry.sourceRoleId);

            if (layers.empty()) continue;

            bool anyLayerBuilt = false;

            for (size_t li = 0; li < layers.size(); ++li)
            {
                const auto& h = layers[li];
                if (h.reader == nullptr) continue;

                // Source region (for loop / tune metadata).
                const auto& presets = h.reader->getPresets();
                if (h.presetIndex < 0 || h.presetIndex >= (int) presets.size()) continue;
                const auto& srcPreset = presets[(size_t) h.presetIndex];
                if (h.regionIndex < 0 || h.regionIndex >= (int) srcPreset.regions.size()) continue;
                const auto& srcRegion = srcPreset.regions[(size_t) h.regionIndex];

                // Decode the sample.  Done on the message thread (not RT-safe).
                auto fs = registry.getSampleData (h);
                if (fs.numFrames == 0) continue;

                // Build the engine Region.  lokey/hikey/pitchKeycenter all == k
                // so findRegion picks it exactly on note==k and startVoice does
                // no melodic transposition.
                Region reg;
                reg.lokey          = k;
                reg.hikey          = k;
                reg.pitchKeycenter = k;

                // The layer's OWN velocity window -- this is the whole point.
                int vLo = 0, vHi = 127;
                registry.velRangeOf (h, vLo, vHi);
                reg.lovel = (uint8_t) juce::jlimit (0, 127, vLo);
                reg.hivel = (uint8_t) juce::jlimit (0, 127, vHi);

                // Guarantee full 0..127 coverage.  A blob whose layers don't span
                // the whole range (softest starting at 20, say) would otherwise
                // DROP hits at the extremes -- findRegion would match nothing and
                // the note would vanish.  Stretch the softest layer down to 0 and
                // the loudest up to 127.
                if (li == 0)                  reg.lovel = 0;
                if (li == layers.size() - 1)  reg.hivel = 127;

                reg.hasLoop        = false;   // see note
                // Drums are full-length one-shots (fullLength=true below): they
                // ignore note-off and play to the sample's natural END.  A looping
                // region has no natural end, so the voice would loop forever and
                // never free -- a slow voice leak that fills the 16-voice pool and
                // starts dropping hits mid-pattern (worst in busy, ride-heavy
                // variations).  Force drums to play through once, then free.
                reg.loopStart      = srcRegion.loopStart;
                reg.loopEnd        = srcRegion.loopEnd;
                reg.volume         = -srcRegion.attenuation;
                reg.tune           = (int) srcRegion.fineTune
                                   + (int) srcRegion.coarseTune * 100;
                reg.offset         = 0;
                reg.roleId         = entry.sourceRoleId;

                // Build the SampleData (deinterleave FloatSample → per-channel buffers).
                SampleData sd;
                fillSampleData (sd, fs.data.data(), fs.channels, fs.numFrames, fs.sampleRate);

                kitVoice->regions.push_back (reg);
                kitVoice->samples.push_back (std::move (sd));
                anyLayerBuilt = true;
            }

            if (! anyLayerBuilt) continue;   // nothing decoded -> key stays unmapped

            // Per-key user state SNAPSHOT — copied into drumKeyStates atomics by
            // publishDrumKit.  DrumElementParams uses SECONDS for ADSR; engine
            // wants ms.
            auto& st = pk.keys[(size_t) k];
            st.gain       = juce::jlimit (0.0f, 2.0f, entry.gain);
            st.pitchSemi  = juce::jlimit (-24.0f, 24.0f, entry.pitch);
            st.cutoffNorm = juce::jlimit (0.0f, 1.0f, entry.filterCutoff);
            // Attack fixed at lowest (instant) and full-sample one-shot for all
            // drum keys — no longer per-key editable.
            st.attackMs   = 0.0f;
            {
                float decMs, susLvl;
                drumLengthToAmp (entry.length, decMs, susLvl);
                st.decayMs = decMs;
                st.sustain = susLvl;
            }
            st.releaseMs      = juce::jmax (0.0f, entry.release * 1000.0f);
            st.roundRobinAmount = juce::jlimit (0, 100, entry.roundRobinAmount);
            st.fxSendNorm     = juce::jlimit (0.0f, 1.0f, entry.fxSend);
            st.fullLength     = true;
        }

        // ── NOTE 35 IS THE KIT'S OWN SUB-KICK NOW ───────────────────────────
        //
        // This is where the shared low_kick.frb used to be forced onto MIDI 35,
        // deleting whatever the kit had there first.  The re-sampled kick blobs
        // carry note 35 and note 36 together in one SFZ, so 35 arrives through
        // the ordinary element catalog above, in its own kit's character, and is
        // swapped when the kick is swapped.
        //
        // Deleting the force is the whole fix: the kit's 35 was always being
        // loaded, then immediately overwritten.
        //
        // ── XG low keys (25..34): substitute from the kit's OWN sounds ───────
        // See xgLowKeySubstitute() above.  Gap-fill only -- a real kit element
        // always wins.  Key 33 (XG's soft kick) donates from 36; it used to be
        // able to fall through to the forced low_kick on 35 as well, which is
        // gone -- but that path is no longer needed, because a kick blob that
        // carries a 35 carries a 36 beside it, and one with neither has no
        // kick to clone either way.
        for (int key = 25; key <= 34; ++key)
        {
            bool alreadyMapped = false;
            for (const auto& r : kitVoice->regions)
                if (key >= r.lokey && key <= r.hikey) { alreadyMapped = true; break; }
            if (alreadyMapped) continue;               // the kit maps it for real

            const auto sub = xgLowKeySubstitute (key);

            // Find the donor.  Collect EVERY region on the donor key -- a layered
            // kit has one per velocity layer, and the substitute has to clone them
            // ALL or it would be stuck on a single sample and stop tracking
            // velocity, which is the very thing we're fixing.  Indices are
            // gathered BEFORE any push_back, so growth can't disturb them.
            std::vector<int> donorIdx;
            int donorKey = -1;
            for (const int want : { sub.primary, sub.secondary })
            {
                if (want < 0) continue;
                for (size_t i = 0; i < kitVoice->regions.size(); ++i)
                    if (want >= kitVoice->regions[i].lokey
                        && want <= kitVoice->regions[i].hikey)
                        donorIdx.push_back ((int) i);
                if (! donorIdx.empty()) { donorKey = want; break; }
            }
            if (donorIdx.empty()) continue;            // kit has neither -> stay silent

            const bool fromSnare = (donorKey == 38 || donorKey == 40);

            for (const int si : donorIdx)
            {
                // Copy by value (the index stays valid across reallocation).
                // pitchKeycenter is pinned to the key itself, so the drum plays at
                // its NATURAL pitch -- no transposition, no gong.  The donor's
                // VELOCITY WINDOW is kept as-is: that is what lets the substituted
                // key follow velocity exactly the way the donor does.
                Region     reg = kitVoice->regions[(size_t) si];
                SampleData sd  = kitVoice->samples[(size_t) si];

                reg.lokey          = key;
                reg.hikey          = key;
                reg.pitchKeycenter = key;

                // Borrowed the SNARE?  Then this key stands in for one of XG's soft
                // articulations, and the full snare would turn the groove into a
                // march.  Trim it 60% (0.40 gain), added to the donor's own volume
                // in dB so the kit's calibration is preserved, not discarded.
                if (fromSnare)
                    reg.volume += juce::Decibels::gainToDecibels (kXgSnareSubGain);

                kitVoice->regions.push_back (reg);
                kitVoice->samples.push_back (std::move (sd));
            }
        }

        // ── Global CLAP on MIDI note 39 ─────────────────────────────────────
        // A single clap, shared by every kit, decoded once from
        // <root>/sounds/drums/clap/clap.frb.  Force it onto note 39 regardless
        // of what the kit mapped there (drop any existing 39 region first so the
        // parallel regions[]/samples[] arrays stay index-aligned).
        if (registry.hasGlobalClap())
        {
            const auto& fs = registry.getGlobalClapSample();
            if (fs.numFrames > 0)
            {
                for (size_t i = 0; i < kitVoice->regions.size(); )
                {
                    if (kitVoice->regions[i].lokey == 39 && kitVoice->regions[i].hikey == 39)
                    {
                        kitVoice->regions.erase (kitVoice->regions.begin() + (long) i);
                        kitVoice->samples.erase (kitVoice->samples.begin() + (long) i);
                    }
                    else ++i;
                }

                Region reg;
                reg.lokey          = 39;
                reg.hikey          = 39;
                reg.pitchKeycenter = 39;
                reg.lovel          = 0;
                reg.hivel          = 127;
                reg.hasLoop        = false;
                reg.loopStart      = 0;
                reg.loopEnd        = 0;
                reg.volume         = 0.0f;
                reg.tune           = 0;
                reg.offset         = 0;
                reg.roleId         = 5;            // DrumElementRole::HandClap (MIDI 39)

                SampleData sd;
                fillSampleData (sd, fs.data.data(), fs.channels, fs.numFrames, fs.sampleRate);

                kitVoice->regions.push_back (reg);
                kitVoice->samples.push_back (std::move (sd));
            }
        }

        // ── KICK MIX layers on MIDI notes 35 & 36 ───────────────────────────
        // Add the supplemental EDM and WOOD kicks as TAGGED blend-layer regions
        // on each kick key.  BOTH are the kit's own now -- 35 the sub-kick and 36
        // the main kick, out of the same re-sampled blob -- where 35 used to be
        // the shared forced low_kick.
        // They are excluded from findRegion; noteOn triggers the one matching
        // the live kickMixVariant atomic as a second voice, gained by the equal-
        // power supplemental side of kickMixAmount.  Because the layers live
        // inside the composed (and pooled) voice while the blend is driven by
        // per-key atomics, MIX / EDM-WOOD / on-off are fully live — no
        // re-compose, no clearPreset — and survive style program changes.
        // KEY 33 IS IN THIS LIST, and that is the fix for "the blend does not
        // work on kicks below note 35".
        //
        // 33 is XG's BASS DRUM SOFT and the ONLY kick under 35 - every other low
        // key substitutes from a snare or a stick (see xgLowKeySubstitute).  It
        // is built as a CLONE of the kit's kick, so a style that writes its kick
        // there got the right sample and no blend at all, while the identical
        // sample on 36 blended fine.
        //
        // Its settings are INHERITED, never its own: 33 is the same drum as 36,
        // so asking the user to dial MIX twice for one kick - on a key the
        // editor deliberately does not show MIX for - would be a trap.  It
        // follows 36, or 35 when the kit has no 36 to clone.
        for (int kk : { 33, 35, 36 })
        {
            const int srcKey = (kk == 33)
                                 ? (kit.keys[(size_t) 36].sourceRoleId != 0 ? 36 : 35)
                                 : kk;

            // Snapshot the per-key blend controls (restored into the drumKeyState
            // atomics by publishDrumKit).
            const auto& e = kit.keys[(size_t) srcKey];
            pk.keys[(size_t) kk].kickMixEnabled = e.kickMixEnabled;
            pk.keys[(size_t) kk].kickMixAmount  = juce::jlimit (0.0f, 100.0f, e.kickMixAmount);
            pk.keys[(size_t) kk].kickMixVariant = juce::jlimit (0, 1, e.kickMixVariant);

            // Only add layers if the key has a real base kick region to blend
            // against.  Both keys now get theirs from the kit's kick blob, so a
            // kit whose blob carries no 35 simply gets no blend there rather
            // than blending against a sub-kick borrowed from another kit.
            bool   hasBase = false;
            double baseSR  = 0.0;
            for (size_t i = 0; i < kitVoice->regions.size(); ++i)
                if (kitVoice->regions[i].lokey == kk && kitVoice->regions[i].hikey == kk
                    && kitVoice->regions[i].kickMixVariant < 0)
                { hasBase = true; baseSR = kitVoice->samples[i].sampleRate; break; }

            kmLog ("composeDrumKit kick " + juce::String (kk)
                   + ": base=" + juce::String (hasBase ? "yes" : "NO")
                   + " baseSR=" + juce::String ((int) baseSR)
                   + " enabled=" + juce::String (e.kickMixEnabled ? 1 : 0)
                   + " amount="  + juce::String (e.kickMixAmount, 1)
                   + " variant=" + juce::String (e.kickMixVariant)
                   + " hasEDM="  + juce::String (registry.hasKickMix (0) ? 1 : 0)
                   + " hasWOOD=" + juce::String (registry.hasKickMix (1) ? 1 : 0));

            if (! hasBase) continue;

            for (int variant = 0; variant < 2; ++variant)   // 0 = EDM, 1 = WOOD
            {
                if (! registry.hasKickMix (variant)) continue;   // EDM/WOOD.frb not loaded
                const auto& layers = registry.getKickMixLayers (variant);
                if (layers.empty()) continue;

                // Baked layer can't velocity-switch, so use the top-velocity one.
                const BlobReader::FloatSample* fs = &layers.back().sample;
                for (const auto& L : layers)
                    if (L.meta.velRangeHigh >= 127) { fs = &L.sample; break; }
                if (fs->numFrames <= 0) continue;

                kmLog ("  + layer key=" + juce::String (kk)
                       + " variant=" + juce::String (variant)
                       + " SR=" + juce::String ((int) fs->sampleRate)
                       + " ch=" + juce::String ((int) fs->channels)
                       + " frames=" + juce::String (fs->numFrames));

                Region reg;
                reg.lokey          = kk;
                reg.hikey          = kk;
                reg.pitchKeycenter = kk;           // == key -> no pitch shift
                reg.lovel          = 0;
                reg.hivel          = 127;
                reg.hasLoop        = false;
                reg.loopStart      = 0;
                reg.loopEnd        = 0;
                reg.volume         = 0.0f;
                reg.tune           = 0;
                reg.offset         = 0;
                reg.roleId         = 0;
                reg.kickMixVariant = variant;      // tag: blend layer (excluded from findRegion)

                SampleData sd;
                fillSampleData (sd, fs->data.data(), (int) fs->channels,
                                fs->numFrames, fs->sampleRate);

                kitVoice->regions.push_back (reg);
                kitVoice->samples.push_back (std::move (sd));
            }
        }

        kitVoice->computeKeyRange();   // mapped register — see PresetVoice::loKey

        pk.voice = kitVoice;
        return pk;
    }

    //==========================================================================
    // publishDrumKit — apply a composed kit to this channel.  RT-SAFE when
    // syncName == false: atomics + a bounded 128-key state copy + an atomic
    // pointer swap of `active`.  Does NOT clearPreset(): every sounding voice
    // captures its own PresetVoice, so hits ringing on the previous kit finish
    // on it while new hits use the new kit — a graceful, click-free swap.  Pass
    // syncName == true ONLY from the message thread (it touches juce::String).
    //==========================================================================
    void Channel::publishDrumKit (const PooledDrumKit& pk, int poolKey, bool syncName)
    {
        isDrumChannel.store (true);
        fullKitActive.store (false);   // a composed kit: kick mix applies again

        // Default choke groups: closed/foot/open hi-hats share group 1.  Reset
        // every key first so the swap starts clean.
        for (auto& g : drumChokeGroup) g.store (0);
        drumChokeGroup[42].store (1);   // HH Closed
        drumChokeGroup[44].store (1);   // HH Foot Close
        drumChokeGroup[46].store (1);   // HH Open

        // Per-key state from the snapshot (all 128 keys, so any previous kit's
        // per-key edits on keys this kit doesn't use are reset to neutral).
        for (int k = 0; k < 128; ++k)
        {
            const auto& s  = pk.keys[(size_t) k];
            auto&       st = drumKeyStates[(size_t) k];
            st.userGain      .store (s.gain);
            st.userPitchSemi .store (s.pitchSemi);
            st.userCutoffNorm.store (s.cutoffNorm);
            st.userAttackMs  .store (s.attackMs);
            st.userDecayMs   .store (s.decayMs);
            st.userSustain   .store (s.sustain);
            st.userReleaseMs .store (s.releaseMs);
            st.roundRobinAmount.store (juce::jlimit (0, 100, s.roundRobinAmount));
            st.userFxSendNorm.store (s.fxSendNorm);
            st.fullLength    .store (s.fullLength);
        }

        // KICK MIX (notes 35/36) is a persistent, kit-independent setting: only
        // an EXPLICIT user kit load (syncName == true) restores the saved
        // amount/variant/on-off.  Style-driven pool swaps (syncName == false)
        // leave the user's live mix untouched, so a style/kit change never
        // resets the blend.  The blend LAYERS themselves ride inside every
        // composed voice, so the crossfade keeps working across the swap.
        if (syncName)
            for (int kk : { 35, 36 })
            {
                auto& st = drumKeyStates[(size_t) kk];
                const auto& s = pk.keys[(size_t) kk];
                st.kickMixEnabled.store (s.kickMixEnabled);
                st.kickMixAmount .store (s.kickMixAmount);
                st.kickMixVariant.store (s.kickMixVariant);
            }

        applyDrumKitFx (pk.fx);                 // atomics — RT-safe
        std::atomic_store (&active, pk.voice);  // the swap
        ready.store (true);
        activeDrumPoolKey.store (poolKey);

        if (syncName)                           // message thread only
        {
            loadedDrumKitName = pk.name;
            presetName = "Drum Kit: " + (pk.name.isEmpty() ? juce::String ("Custom")
                                                            : pk.name);
        }
    }

    //==========================================================================
    // loadDrumKit — compose + publish in one step (hand-edited kits from the
    // Kitton popup / Sounds tab).  clearPreset() first to stop any current
    // voices, matching the previous behaviour for an explicit user load.
    //==========================================================================
    void Channel::loadDrumKit (const DrumKitRegistry& registry, const DrumKitParams& kit)
    {
        // A kit with no mapped keys is "no kit", never "an empty kit": composing
        // it would clear the preset and publish silence.  Every other caller
        // already tests hasMappedKeys() before getting here; making the guard
        // unconditional means no path — set restore, editor reopen, favourite
        // load — can silently mute a drum channel by handing over a shell.
        if (! kit.hasMappedKeys()) return;

        clearPreset();
        publishDrumKit (composeDrumKit (registry, kit), -1, /*syncName*/ true);
    }

    //==========================================================================
    // preloadDrumKit — compose a kit into the pool ONCE (message thread).  No
    // publish, no clearPreset, no audio impact.  Skips work if already pooled.
    //==========================================================================
    void Channel::preloadDrumKit (const DrumKitRegistry& registry,
                                  const DrumKitParams& kit, int poolKey)
    {
        if (poolKey < 0) return;
        if (drumKitPool.find (poolKey) != drumKitPool.end()) return;  // already warm
        drumKitPool[poolKey] = composeDrumKit (registry, kit);
    }

    //==========================================================================
    // selectPooledDrumKit — switch to a pooled kit by pointer swap.  Returns
    // false (no-op) if the kit isn't pooled.
    //==========================================================================
    bool Channel::selectPooledDrumKit (int poolKey, bool syncName)
    {
        auto it = drumKitPool.find (poolKey);
        if (it == drumKitPool.end()) return false;
        publishDrumKit (it->second, poolKey, syncName);
        return true;
    }

    //==========================================================================
    // renderBlock — voices + click → tempBuffer → EQ → delay → reverb → outBuffer
    //==========================================================================
    void Channel::renderBlock(juce::AudioBuffer<float>& outBuffer, int numSamples, double hostBPM)
    {
        if (outBuffer.getNumChannels() < 1) return;

        if (tempBuffer.getNumSamples() < numSamples)
            tempBuffer.setSize (2, numSamples, false, false, true);

        tempBuffer.clear (0, 0, numSamples);
        tempBuffer.clear (1, 0, numSamples);

        auto activePreset = std::atomic_load (&active);
        const bool haveContent = ready.load() && activePreset && ! activePreset->regions.empty();
        const bool drumMode    = isDrumChannel.load();

        // Drum mode runs a kit-wide FX bus in parallel with the dry mix.
        // Make sure its wet-path scratch is sized and zeroed before voices
        // write into it.
        if (drumMode)
        {
            if (drumFxScratch.getNumSamples() < numSamples)
                drumFxScratch.setSize (2, numSamples, false, false, true);
            drumFxScratch.clear (0, 0, numSamples);
            drumFxScratch.clear (1, 0, numSamples);
        }

        if (haveContent)
        {
            // Snapshot per-block params (channel-wide).
            const float fCutoffChannel = filterCutoff.load();
            const float fReso          = filterReso.load();
            const int   fType          = filterType.load();
            const float fKeytrack      = filterKeytrack.load();
            const float fAmount        = filtEnvAmount.load();

            // Band filter: HP(low thumb) -> LP(high thumb) in series, replacing
            // the classic single filter.  Used by every MELODIC voice — the
            // style's melodic slots (3..7) AND the solo slots (16+) — so the
            // style and solo sound editors offer the same controls for the same
            // kind of sound.  Bass (2) and drums (0/1) keep the classic path
            // below: the bass wants its cutoff sweep, and a kit is not a
            // melodic voice in this sense.
            //
            // This test MUST stay in step with SoundsTab's setBandMode() call,
            // or the editor shows thumbs that move nothing.
            const bool  useBand   = (channelIndex >= 3 && channelIndex <= 7)
                                 || (channelIndex >= kFirstSoloChannel);
            const float bandHpHz  = bandHpFreq.load();
            const float bandLpHz  = bandLpFreq.load();
            const float bendRange      = pitchBendRange.load();
            const float bendValue      = currentPitchBendValue;
            const float pEnvDepth      = pitchEnvDepth.load();
            const float aLfoDepth      = ampLfoEnabled.load()  ? ampLfoDepth.load()  : 0.0f;
            const float fLfoDepth      = filtLfoEnabled.load() ? filtLfoDepth.load() : 0.0f;
            const float pLfoDepth      = pitchLfoDepth.load();
            // fader x expression x style ride x per-sound calibration trim.
            // The trim rides with channelVolume (and so survives chain bypass
            // for the same reason the fader does): with it bypassed you could
            // not balance instruments against each other, which is the one
            // thing bypass must not break.
            const float chVol          = channelVolume.load() * channelExpression.load()
                                       * channelAutoLevel.load() * instrumentGain.load();
            const float chPan          = juce::jlimit(-1.0f, 1.0f, channelPan.load());

            const float panAngle = (chPan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
            const float panL = std::cos(panAngle);
            const float panR = std::sin(panAngle);

            // Snapshot the global "bypass instrument audio chain" toggle once
            // per block.  When set, gates EVERY per-voice and per-channel DSP
            // stage that isn't the amp ADSR — EXCEPT channel volume, which
            // stays connected so the mixer faders (and CC 7) keep working.
            // Without that exception you couldn't balance the instruments
            // against each other while bypass is on.  Specifically suppressed:
            //   • Filter (SVF, keytrack, filter ENV, filter LFO)
            //   • Amp LFO (tremolo) — gain multiplier forced to 1.0
            //   • Pitch ENV and pitch LFO — modulation contributions ignored
            //   • Velocity gain — forced to 1.0 (every note plays as if vel=127)
            //   • Channel pan — recomputed as if chPan == 0 (equal-power
            //     center, natural stereo content of samples preserved)
            //   • Post-mix chain (EQ, Chorus, Wah, Phaser, Delay, Reverb)
            //   • Drum FX bus (per-key dry/wet routing forced to 100% dry)
            // The note transport (pitch bend, portamento, sample read with
            // interpolation, region gain, channel volume × expression, and
            // the amp ADSR itself) still runs.  See the atomic's declaration
            // in Channel.h.
            const bool chainBypass = chainBypassed.load();
            const float panLEff    = chainBypass ? 0.7071068f : panL;
            const float panREff    = chainBypass ? 0.7071068f : panR;

            for (auto& voice : voices)
            {
                if (!voice.active) continue;

                // Each voice plays from the instrument it started on, so a
                // program change can't pull the sample out from under it.
                const auto& vsamples = voice.preset ? voice.preset->samples
                                                     : activePreset->samples;
                const int ri = voice.regionIndex;
                if (ri < 0 || ri >= (int) vsamples.size()) { voice.active = false; continue; }

                auto& sample = vsamples[(size_t) ri];
                if (sample.buffer.getNumSamples() == 0) { voice.active = false; continue; }

                const int sampleLen = sample.buffer.getNumSamples();
                // TRUE STEREO input stage.  A sample stored with two channels is
                // read as two channels — channel 0 to the left output, channel 1
                // to the right — so the image the sample was recorded with
                // survives into the pan/FX/mix chain instead of being thrown
                // away.  (The old stage read channel 0 only and set sR = sL,
                // which discarded every stereo blob's right channel while still
                // paying full RAM to store it.)
                //
                // A one-channel sample takes the MONO FAST PATH: read once, and
                // downstream run the filters once and copy, instead of running a
                // second identical filter over an identical signal.  Together
                // with the dual-mono collapse in fillSampleData that makes mono
                // material cheaper than it was before, not just correct.
                //
                // Constant for the life of a voice: regionIndex is fixed at
                // note-on, so this can be hoisted out of the sample loop.
                const bool stereoSrc = (sample.numChannels >= 2
                                        && sample.buffer.getNumChannels() >= 2);

                // Per-role stereo width (see the calibration block at the top of
                // this file).  narrowSrc: the mid/side stage has work to do.
                // dualPath: left and right can still differ after it, so the
                // filters genuinely need two instances — at width 0 they cannot,
                // so a narrowed-to-mono kit takes the same one-filter fast path
                // as a mono sample and costs no more than it did before.
                const float stereoW   = drumMode ? kDrumStereoWidth
                                                 : kMelodicStereoWidth;
                const bool  narrowSrc = stereoSrc && (stereoW < 0.999f);
                const bool  dualPath  = stereoSrc && (stereoW > 0.001f);

                // ── Per-voice drum overrides (snapshot once per block) ───────
                // Drum-mode per-key sliders re-bind the filter cutoff and add a
                // gain multiplier on top of the region's stored volume.
                float voiceFCutoff  = fCutoffChannel;
                float voiceGainMult = 1.0f;
                float voiceHpHz     = 20.0f;      // per-key low-cut  (open)
                float voiceLpHz     = 20000.0f;   // per-key high-cut (open)
                if (drumMode && voice.note >= 0 && voice.note < 128)
                {
                    const auto& st = drumKeyStates[(size_t) voice.note];
                    const float ovCN = st.userCutoffNorm.load();
                    if (ovCN >= 0.0f)
                        voiceFCutoff = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, ovCN));
                    voiceGainMult = juce::jmax (0.0f, st.userGain.load());

                    // Per-key band edges, same 20 * 1000^norm law as everywhere
                    // else.  Left fully open they cost one comparison below.
                    voiceHpHz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f,
                                                              st.userHpNorm.load()));
                    voiceLpHz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f,
                                                              st.userLpNorm.load()));
                }

                // The per-voice multiplier the note-on asked for.  Defaults to
                // 1.0, so every ordinary note is untouched; the crash uses it to
                // set its own level without moving the kit it shares a channel
                // with.
                voiceGainMult *= juce::jmax (0.0f, voice.extraGain);

                // ── Filter bypass test (per voice, per block) ─────────────────
                //
                // The SVF runs std::tan(π·cutoff/sr) on every sample; near the
                // Nyquist asymptote (cutoff ≥ ~18 kHz at 44.1 kHz) numerical
                // precision degrades and the integrator path leaves audible
                // fingerprints on what should be a transparent low-pass.  Skip
                // the call entirely when the filter is at its "off" defaults:
                //   • type = LowPass (0)
                //   • cutoff ≥ 18 kHz (effectively above audible)
                //   • resonance ≤ 0.01
                //   • no envelope / LFO / keytrack modulation
                // Same thresholds the modulation branches below use to decide
                // whether to apply their respective effects.  Drum and bass
                // slots at default settings (the user's typical case) hit this
                // bypass and the filter touches their audio zero times.
                // Per-block "is filter audibly off" decision.  The local
                // bypass triggers when the filter's settings can't audibly
                // colour the signal at all:
                //   • type == LowPass (0) — the only type whose "off" state
                //     means "transparent" at high cutoff
                //   • cutoff ≥ 18 kHz
                //   • resonance ≤ 0.01
                //   • no envelope / LFO / keytrack modulation
                // Same thresholds the modulation branches below use to decide
                // whether to apply their respective effects.  Drum and bass
                // slots at default settings (the user's typical case) hit this
                // bypass and the filter touches their audio zero times.
                //
                // OR-in the global chain-bypass flag: when the user has
                // toggled "bypass instrument audio chain" in Global Settings,
                // we elide the SVF regardless of the per-channel filter
                // settings.
                const bool filterBypass = chainBypass
                                       || ((fType == 0)
                                           && voiceFCutoff       >= 18000.0f
                                           && fReso              <= 0.01f
                                           && std::abs(fKeytrack) < 0.1f
                                           && std::abs(fAmount)   < 0.001f
                                           && fLfoDepth           < 0.001f);

                for (int i = 0; i < numSamples; ++i)
                {
                    // ── PORTAMENTO: ONE-POLE GLIDE IN THE PITCH (LOG) DOMAIN ──
                    //
                    // One multiply-add per sample, and the shape every analogue
                    // mono synth has: it leaves fast and eases in.  The old
                    // linear ramp travelled at one speed and stopped at a
                    // corner, which no instrument does.
                    //
                    // The 0.001-semitone floor ends the glide rather than
                    // letting an exponential creep asymptotically forever: a
                    // thousandth of a semitone is roughly a tenth of a cent,
                    // inaudible, and stopping there frees the branch and pins
                    // the pitch exactly in tune.
                    if (voice.portamentoActive)
                    {
                        const double diff = voice.targetPitchSemitones - voice.currentPitchSemitones;

                        if (std::abs (diff) < 0.001)
                        {
                            voice.currentPitchSemitones = voice.targetPitchSemitones;
                            voice.portamentoActive      = false;
                        }
                        else
                        {
                            voice.currentPitchSemitones += diff * voice.glideCoeff;
                        }
                    }

                    const float ampLevel      = voice.ampEnv.process();
                    const float filtLevel     = voice.filtEnv.process();
                    const float pitchEnvLevel = voice.pitchEnv.process();
                    const float aLfo = voice.ampLfo.process();
                    const float fLfo = voice.filtLfo.process();
                    const float pLfo = voice.pitchLfo.process();

                    if (!voice.ampEnv.isActive()) { voice.active = false; break; }

                    double pitchSemitones = voice.currentPitchSemitones;
                    if (std::abs(bendValue) > 0.001f)                  pitchSemitones += (double) bendValue * (double) bendRange;
                    if (! chainBypass && std::abs(pEnvDepth) > 0.01f)  pitchSemitones += (double) pitchEnvLevel * (double) pEnvDepth;
                    if (! chainBypass && pLfoDepth > 0.001f)           pitchSemitones += (double) pLfo * (double) pLfoDepth;

                    float sL = 0.0f, sR = 0.0f;

                    {
                        const double effectivePitch =
                            semitoneToRatio(pitchSemitones, voice.sourceSampleRate, currentSampleRate);

                        int64_t pos0 = (int64_t) voice.playPosition;
                        const float frac = (float)(voice.playPosition - (double) pos0);

                        if (pos0 >= 0 && pos0 < sampleLen - 1)
                        {
                            // ── Sample read ─────────────────────────────────
                            //
                            // Three-tier path:
                            //   1) frac ≈ 0  → exact integer read, no interp.
                            //      Hit when the pitch ratio is 1.0 (drum at
                            //      sample-root key, melodic note at root, etc.)
                            //      — clean and the cheapest path.
                            //   2) Interior (pos0 ≥ 1 && pos0 < sampleLen − 2)
                            //      → 4-point Hermite cubic.  Substantially
                            //      cleaner than linear: no HF roll-off (linear
                            //      loses ~4-6 dB approaching Nyquist/4, which
                            //      "filters" drum transients and bass bite),
                            //      and far less aliasing on pitch-up.
                            //   3) Boundary (start / end window too narrow for
                            //      cubic) → 2-point linear, same as before.
                            const bool exact    = (frac < 1.0e-6f);
                            const bool cubicOk  = (pos0 >= 1) && (pos0 < (int64_t) sampleLen - 2);

                            auto readCubic = [frac] (float y0, float y1,
                                                     float y2, float y3) noexcept -> float
                            {
                                // Catmull-Rom-style 4-point Hermite.  Horner form
                                // for cheap evaluation.
                                const float c0 = y1;
                                const float c1 = 0.5f * (y2 - y0);
                                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                                return ((c3 * frac + c2) * frac + c1) * frac + c0;
                            };

                            // One reader, applied per SOURCE channel — the
                            // three-tier choice above is identical for both, so
                            // left and right always interpolate the same way.
                            auto readChan = [&] (int ch) noexcept -> float
                            {
                                if (exact)
                                    return sample.buffer.getSample (ch, (int) pos0);

                                if (cubicOk)
                                    return readCubic (sample.buffer.getSample (ch, (int)(pos0 - 1)),
                                                      sample.buffer.getSample (ch, (int)(pos0)),
                                                      sample.buffer.getSample (ch, (int)(pos0 + 1)),
                                                      sample.buffer.getSample (ch, (int)(pos0 + 2)));

                                // Boundary fallback - pos0 == 0 or pos0 == sampleLen-2.
                                // Linear interp avoids stepping outside the buffer.
                                const float s0 = sample.buffer.getSample (ch, (int) pos0);
                                const float s1 = sample.buffer.getSample (ch, (int)(pos0 + 1));
                                return s0 + (s1 - s0) * frac;
                            };

                            sL = readChan (0);
                            if (stereoSrc)
                            {
                                sR = readChan (1);
                                if (narrowSrc)
                                {
                                    const float m = (sL + sR) * 0.5f;
                                    const float sd = (sL - sR) * 0.5f * stereoW;
                                    sL = m + sd;
                                    sR = m - sd;
                                }
                            }
                            else sR = sL;
                        }
                        else if (pos0 >= sampleLen - 1)
                        {
                            if (voice.looping && voice.loopEnd > voice.loopStart)
                            {
                                voice.playPosition = (double) voice.loopStart;
                                pos0 = voice.loopStart;
                                sL = sample.buffer.getSample (0, (int) pos0);
                                if (stereoSrc)
                                {
                                    sR = sample.buffer.getSample (1, (int) pos0);
                                    if (narrowSrc)
                                    {
                                        const float m = (sL + sR) * 0.5f;
                                        const float sd = (sL - sR) * 0.5f * stereoW;
                                        sL = m + sd;
                                        sR = m - sd;
                                    }
                                }
                                else sR = sL;
                            }
                            else
                            {
                                voice.active = false; break;
                            }
                        }

                        voice.playPosition += effectivePitch;

                        if (voice.looping && voice.loopEnd > voice.loopStart)
                            if (voice.playPosition >= (double) voice.loopEnd)
                                voice.playPosition = (double) voice.loopStart
                                                   + (voice.playPosition - (double) voice.loopEnd);
                    }

                    // Advance the mono region crossfade.  One compare per sample
                    // for every voice that is not fading, which is nearly all of
                    // them.  A voice that has faded to silence is retired here
                    // rather than left running under a zero gain.
                    if (voice.xfadeInc != 0.0f)
                    {
                        voice.xfadeGain += voice.xfadeInc;

                        if (voice.xfadeGain >= 1.0f)        // fade-in complete
                        {
                            voice.xfadeGain = 1.0f;
                            voice.xfadeInc  = 0.0f;
                        }
                        else if (voice.xfadeGain <= 0.0f)   // fade-out complete
                        {
                            // Back to unity, not zero: allocateVoice hands out a
                            // free voice WITHOUT calling reset(), so anything
                            // left here is what the next note inherits.
                            voice.xfadeGain = 1.0f;
                            voice.xfadeInc  = 0.0f;
                            voice.active    = false;
                            voice.fadingOut = false;
                            break;                          // this voice is done
                        }
                    }

                    float ampMod = 1.0f;
                    if (! chainBypass && aLfoDepth > 0.001f)
                        ampMod = 1.0f - aLfoDepth * 0.5f + aLfoDepth * 0.5f * aLfo;

                    // In chain-bypass mode the gain still flows through
                    // channel volume × expression so the mixer faders (and
                    // any CC 7 / CC 11 automation) keep balancing the
                    // instruments.  Only velocity and the amp LFO get
                    // neutralised; regionGain (sample calibration) and
                    // voiceGainMult (drum per-key override) are intrinsic to
                    // the instrument and survive.
                    const float velGain  = chainBypass ? 1.0f : voice.velocityGain;
                    const float gain     = velGain * voice.regionGain * voiceGainMult
                                         * voice.kickMixGain * ampLevel * chVol * ampMod
                                         * voice.xfadeGain;

                    // Filter modulation uses voiceFCutoff so the drum per-key
                    // override (if set) participates in keytrack + env + LFO.
                    float modCutoff = voiceFCutoff;
                    if (std::abs(fKeytrack) > 0.1f)
                    {
                        const float semiFromC4 = (float)(voice.note - 60);
                        const float trackRatio = std::pow(2.0f, semiFromC4 * fKeytrack / 1200.0f);
                        modCutoff *= trackRatio;
                    }
                    if (std::abs(fAmount) > 0.001f)
                        modCutoff += filtLevel * fAmount * 10000.0f;
                    if (fLfoDepth > 0.001f)
                        modCutoff *= (1.0f + fLfo * fLfoDepth);
                    modCutoff = juce::jlimit(20.0f, 20000.0f, modCutoff);

                    if (useBand && ! chainBypass)
                    {
                        // Two-thumb LOW-CUT / HIGH-CUT band (style slots 3..7):
                        // a high-pass at the low thumb in series with a low-pass
                        // at the high thumb, clean (no resonance).  A stage that
                        // sits fully open is skipped so it stays bypass-
                        // transparent (a 20 Hz HP / 20 kHz LP would still colour
                        // the extremes); if the thumbs meet (hp >= lp) the pass-
                        // band is zero -> silence, which is the intended behaviour.
                        if (bandHpHz >= bandLpHz)
                        {
                            sL = 0.0f;
                            sR = 0.0f;
                        }
                        else
                        {
                            // Mono source: the R filter would see an identical
                            // input and identical state, so it can only produce
                            // an identical output — run one and copy.
                            if (bandHpHz > 20.01f)   // low-cut engaged
                            {
                                sL = voice.hpFilterL.process (sL, bandHpHz, 0.0f, HighPass, currentSampleRate);
                                sR = dualPath
                                       ? voice.hpFilterR.process (sR, bandHpHz, 0.0f, HighPass, currentSampleRate)
                                       : sL;
                            }
                            if (bandLpHz < 19999.0f) // high-cut engaged
                            {
                                sL = voice.filterL.process (sL, bandLpHz, 0.0f, LowPass, currentSampleRate);
                                sR = dualPath
                                       ? voice.filterR.process (sR, bandLpHz, 0.0f, LowPass, currentSampleRate)
                                       : sL;
                            }
                        }
                    }
                    else if (drumMode && ! chainBypass
                             && (voiceHpHz > 20.01f || voiceLpHz < 19999.0f))
                    {
                        // ── PER-ELEMENT BAND (drums) ──────────────────────────
                        //
                        // The same two-stage low-cut / high-cut the melodic band
                        // above runs, but driven from the KEY rather than the
                        // channel - because a kit is a dozen instruments sharing
                        // one channel, and one band across all of them would be
                        // useless.  voice.hpFilterL/R already existed for the
                        // melodic path; drums were simply never routed into it.
                        //
                        // Guarded on the edges being off their stops, so an
                        // untouched kit skips the whole stage and is bit-identical
                        // to before.
                        if (voiceHpHz >= voiceLpHz)
                        {
                            sL = 0.0f;
                            sR = 0.0f;
                        }
                        else
                        {
                            if (voiceHpHz > 20.01f)
                            {
                                sL = voice.hpFilterL.process (sL, voiceHpHz, 0.0f, HighPass, currentSampleRate);
                                sR = dualPath
                                       ? voice.hpFilterR.process (sR, voiceHpHz, 0.0f, HighPass, currentSampleRate)
                                       : sL;
                            }
                            if (voiceLpHz < 19999.0f)
                            {
                                sL = voice.filterL.process (sL, voiceLpHz, 0.0f, LowPass, currentSampleRate);
                                sR = dualPath
                                       ? voice.filterR.process (sR, voiceLpHz, 0.0f, LowPass, currentSampleRate)
                                       : sL;
                            }
                        }
                    }
                    else if (! filterBypass)
                    {
                        // Classic single filter (bass / solo / drums).
                        // The SVF is a linear TPT, so functionally identical
                        // whether placed before or after the gain stage; keep
                        // it before the gain to match the prior signal chain
                        // 1:1.  The branch above the inner loop tests for the
                        // "off" defaults and elides this entire stage in that
                        // case (drum / bass at default settings).
                        sL = voice.filterL.process (sL, modCutoff, fReso, fType, currentSampleRate);
                        sR = dualPath
                               ? voice.filterR.process (sR, modCutoff, fReso, fType, currentSampleRate)
                               : sL;   // mono (or fully narrowed): one filter, copied
                    }

                    sL *= gain;
                    sR *= gain;

                    // Drum-mode kit FX is now an INSERT, not a per-key send: the
                    // entire kit mix is summed into drumFxScratch and run through
                    // the rack after the voice loop, then blended back via the
                    // master wet/dry.  Melodic slots (and any drum slot with the
                    // chain bypassed) write straight to the dry mix as before.
                    if (drumMode && ! chainBypass)
                    {
                        drumFxScratch.addSample (0, i, sL * panLEff);
                        drumFxScratch.addSample (1, i, sR * panREff);
                    }
                    else
                    {
                        tempBuffer.addSample (0, i, sL * panLEff);
                        tempBuffer.addSample (1, i, sR * panREff);
                    }
                }
            }

        }

        // ── Drum FX rack (insert) ───────────────────────────────────────────
        // drumFxScratch holds the whole kit mix.  Blend the dry mix with the
        // rack-processed output per the master wet/dry, then sum into tempBuffer.
        // Skipped when not in drum mode and when chainBypass is on (the bypass
        // toggle covers drums as well as melodic slots).
        const bool chainBypassPostMix = chainBypassed.load();
        if (drumMode && ! chainBypassPostMix)
        {
            const float wet = juce::jlimit (0.0f, 1.0f, drumFxBus.fxWet.load());

            // SWEETEN THE KIT FIRST, OUTSIDE THE RACK ENTIRELY.
            //
            // Above the wet/dry test on purpose.  It used to sit inside the
            // else-branch, which meant pulling the rack's WET fader to zero
            // silently took the sweetener with it - the exact opposite of the
            // intent, since the rack is the advanced page and defaults off
            // while the sweetener has to work whether or not it was opened.
            if (sweetEnabled.load())
            {
                float* sL = drumFxScratch.getWritePointer (0);
                float* sR = drumFxScratch.getNumChannels() > 1
                                ? drumFxScratch.getWritePointer (1) : sL;

                Betel::SweetenerFx::Params sp;
                sp.enabled     = true;
                sp.mix         = sweetMix        .load();
                sp.softenOn    = sweetSoftenOn   .load();
                sp.softenDepth = sweetSoftenDepth.load();
                sp.softenMs    = sweetSoftenMs   .load();
                sp.peakOn      = sweetPeakOn     .load();
                sp.peakCeilDb  = sweetPeakCeilDb .load();
                sp.peakRatio   = sweetPeakRatio  .load();
                sp.tameOn      = sweetTameOn     .load();
                sp.tameDepthDb = sweetTameDepthDb.load();
                sp.tameFreqHz  = sweetTameFreqHz .load();
                sp.roundOn     = sweetRoundOn    .load();
                sp.roundDrive  = sweetRoundDrive .load();
                sp.roundMix    = sweetRoundMix   .load();
                sweetenerFx.process (sL, sR, numSamples, sp);
            }

            if (wet < 1.0e-4f)
            {
                // Rack fully bypassed — sum the dry kit mix straight in.
                tempBuffer.addFrom (0, 0, drumFxScratch, 0, 0, numSamples);
                tempBuffer.addFrom (1, 0, drumFxScratch, 1, 0, numSamples);
            }
            else
            {
                // Keep a dry copy, process the rack in place, blend wet/dry.
                if (drumFxDry.getNumSamples() < numSamples)
                    drumFxDry.setSize (2, numSamples, false, false, true);
                drumFxDry.copyFrom (0, 0, drumFxScratch, 0, 0, numSamples);
                drumFxDry.copyFrom (1, 0, drumFxScratch, 1, 0, numSamples);

                drumFxBus.process (drumFxScratch, numSamples, hostBPM);

                const float dry = 1.0f - wet;
                tempBuffer.addFrom (0, 0, drumFxDry,     0, 0, numSamples, dry);
                tempBuffer.addFrom (1, 0, drumFxDry,     1, 0, numSamples, dry);
                tempBuffer.addFrom (0, 0, drumFxScratch, 0, 0, numSamples, wet);
                tempBuffer.addFrom (1, 0, drumFxScratch, 1, 0, numSamples, wet);
            }
        }
        else if (! drumMode && ! chainBypassPostMix)
        {
            // Channel-level post-mix only runs for melodic slots; drum slots
            // use the FX bus above instead.
            // Chain order: SWEETEN → EQ → Chorus → Wah → Phaser → Delay → Reverb
            //
            // The sweetener goes FIRST and the time-based stages come after it,
            // so the dynamics treat the raw instrument and the reverb / delay
            // tails are never pumped by it.  EQ then shapes what the sweetener
            // handed over rather than fighting it for the same peaks.
            applySweetenerInPlace   (tempBuffer, numSamples);
            if (eqEnabled.load())     applyEqInPlace     (tempBuffer, numSamples);
            applyChorusInPlace      (tempBuffer, numSamples);
            applyWahInPlace         (tempBuffer, numSamples);
            applyPhaserInPlace      (tempBuffer, numSamples);
            if (delayEnabled.load())  applyDelayInPlace  (tempBuffer, numSamples, hostBPM);
            if (reverbEnabled.load()) applyReverbInPlace (tempBuffer, numSamples);
        }

        const int outChannels = juce::jmin (2, outBuffer.getNumChannels());
        for (int c = 0; c < outChannels; ++c)
            outBuffer.addFrom (c, 0, tempBuffer, c, 0, numSamples);

        // ── Publish the notes the channel is PLAYING (editor's piano strip) ──
        // Straight from the MIDI note events (keyDownCount, maintained in
        // noteOn / noteOff / retuneVoice), plus a short trigger latch so a hit
        // shorter than one UI frame is still drawn.
        //
        // NOT derived from the voice pool: a voice tells you whether something is
        // audible, not whether the key is down.  Full-length drum keys never
        // release their envelope at all, so voice-derived state left them lit
        // until the sample ran out — and a voice that misses its note-off stays
        // lit forever.  See keyDownCount in Channel.h.
        //
        // Runs at the end of the block, on every path — an early-out here would
        // leave the last published mask frozen on screen.
        {
            uint32_t m[4] = { 0u, 0u, 0u, 0u };

            for (int n = 0; n < 128; ++n)
            {
                bool lit = (keyDownCount[(size_t) n] > 0)
                        || (editorKeyDown[(size_t) n] > 0);

                int& L = noteLatch[(size_t) n];
                if (L > 0) { L -= numSamples; lit = true; }

                if (lit)
                    m[(size_t) (n >> 5)] |= (1u << (n & 31));
            }

            for (int i = 0; i < 4; ++i)
                soundingMask[(size_t) i].store (m[i], std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // 5-band EQ
    //==========================================================================
    void Channel::applyEqInPlace (juce::AudioBuffer<float>& buf, int numSamples)
    {
        bool anyActive = false;
        for (int i = 0; i < 5; ++i)
        {
            const float g = eqGain[i].load();
            const float f = eqFreq[i].load();
            if (std::abs (g - cachedEqGain[i]) > 1e-4f
                || std::abs (f - cachedEqFreq[i]) > 1e-2f)
            {
                cachedEqGain[i] = g;
                cachedEqFreq[i] = f;
                if      (i == 0) { eqL[i].setLowShelf  (f, g,        currentSampleRate);
                                   eqR[i].setLowShelf  (f, g,        currentSampleRate); }
                else if (i == 4) { eqL[i].setHighShelf (f, g,        currentSampleRate);
                                   eqR[i].setHighShelf (f, g,        currentSampleRate); }
                else             { eqL[i].setPeaking   (f, g, 1.0f,  currentSampleRate);
                                   eqR[i].setPeaking   (f, g, 1.0f,  currentSampleRate); }
            }
            if (std::abs (cachedEqGain[i]) > 0.05f) anyActive = true;
        }

        if (! anyActive) return;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        const bool stereo = (R != L);

        for (int i = 0; i < numSamples; ++i)
        {
            float l = L[i];
            l = eqL[0].process (l); l = eqL[1].process (l); l = eqL[2].process (l);
            l = eqL[3].process (l); l = eqL[4].process (l);
            L[i] = l;
            if (stereo)
            {
                float r = R[i];
                r = eqR[0].process (r); r = eqR[1].process (r); r = eqR[2].process (r);
                r = eqR[3].process (r); r = eqR[4].process (r);
                R[i] = r;
            }
        }
    }

    //==========================================================================
    // Stereo delay
    //==========================================================================
    void Channel::applyDelayInPlace (juce::AudioBuffer<float>& buf, int numSamples, double hostBPM)
    {
        // Slider x base = the gain that actually reaches the DSP.
        const float wet = juce::jlimit (0.0f, 1.0f, delayWet.load() * delayWetBase.load());
        const float dry = delayDry.load();
        const float fb  = delayFeedback.load();

        // THE DRY TEST IS NOT OPTIONAL.  A silent wet used to mean "nothing to
        // do", and with a blended mix that was true.  Now dry is its own
        // control, so a user who pulls DRY down with WET at zero is asking for
        // attenuation - returning early would ignore them.
        if (wet < 1e-4f && fb < 1e-4f && dry > 0.9999f) return;

        // Musical-time → delay length in samples (kept from the original UX).
        static constexpr float kBeats44 [5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
        static constexpr float kBeats34 [4] = { 3.0f, 1.0f, 0.5f, 0.25f };

        const int   ts  = delayTimeSig.load();
        const int   div = delayDiv.load();
        const float beats =
            (ts == 0)
                ? kBeats44[(size_t) juce::jlimit (0, 4, div)]
                : kBeats34[(size_t) juce::jlimit (0, 3, div)];

        const float bpm = (float) juce::jmax (20.0, hostBPM);
        const float delaySamples = beats * (60.0f / bpm) * (float) currentSampleRate;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;

        // StereoDelayFx is a ping-pong delay that does its own dry/wet mix.
        delayFx.process (L, R, numSamples, delaySamples, fb, wet, currentSampleRate, dry);
    }

    //==========================================================================
    // Reverb
    //==========================================================================
    void Channel::applyReverbInPlace (juce::AudioBuffer<float>& buf, int numSamples)
    {
        // Slider x base, as on the delay.  reverbWetBase defaults to 1.0, so
        // this is the identity until someone opens the box.
        const float wet = juce::jlimit (0.0f, 1.0f, reverbWet.load() * reverbWetBase.load());
        if (wet < 1e-4f) return;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;

        // 0 = HALL/ROOM (diffuser + Householder FDN), 1 = PLATE (Dattorro tank).
        // ReverbFx swaps engines on the audio thread and clears the outgoing
        // one's frozen tail itself, so this is safe to set every block.
        reverbFx.setAlgorithm (reverbAlgo.load());

        // ReverbFx does its own dry/wet mix in place.
        reverbFx.process (L, R, numSamples,
                          reverbSize.load(), reverbDamp.load(), wet,
                          reverbPreDelay.load(), currentSampleRate,
                          reverbDry.load(), reverbTail.load(),
                          reverbHpNorm.load(), reverbLpNorm.load());
    }

    //==========================================================================
    // Sounds-path insert FX (melodic channels only).  Each reads its atomics,
    // bypasses cheaply when disabled or fully dry, and edits the stereo buffer
    // in place via the engine in SoundsFx.h.
    //==========================================================================
    void Channel::applyWahInPlace (juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (! wahEnabled.load()) return;
        const float mix = wahMix.load();
        if (mix <= 0.0001f || buf.getNumChannels() < 1) return;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        wahFx.process (L, R, numSamples,
                       wahSensitivity.load(), wahRate.load(), wahLfoDepth.load(),
                       wahBaseHz.load(), wahQ.load(), mix, currentSampleRate);
    }

    void Channel::applyPhaserInPlace (juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (! phaserEnabled.load()) return;
        const float mix = phaserMix.load();
        if (mix <= 0.0001f || buf.getNumChannels() < 1) return;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        phaserFx.process (L, R, numSamples,
                          phaserRate.load(), phaserDepth.load(),
                          phaserFeedback.load(), mix, currentSampleRate);
    }

    void Channel::applyChorusInPlace (juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (! chorusEnabled.load()) return;
        const float mix = chorusMix.load();
        if (mix <= 0.0001f || buf.getNumChannels() < 1) return;

        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        chorusFx.process (L, R, numSamples,
                          chorusRate.load(), chorusDepth.load(),
                          mix, currentSampleRate);
    }

    //==========================================================================
    // DrumFxBus implementation
    //
    // The signal flow inside process() is fixed at EQ → Sat → Comp → Reverb →
    // Delay.  Each stage is bypassed when its key parameter sits at neutral
    // (e.g. EQ skips when every band is within 0.05 dB of unity, Saturation
    // skips when drive < 1e-4, etc.) so an empty FX configuration costs only
    // a handful of atomic loads per block.
    //==========================================================================
    void Channel::DrumFxBus::prepare (double sr, int blockSize)
    {
        sampleRate = sr;

        for (int i = 0; i < 10; ++i)
        {
            eqL[i].reset();
            eqR[i].reset();
            cachedEqGain[i] = 0.0f;
        }

        compEnv = 0.0f;

        // Both engines own their own storage, so the scratch buffer, the
        // size/damp cache and the hand-rolled ring buffer are all gone.
        juce::ignoreUnused (blockSize);
        reverbFx.prepare (sr);
        delayFx .prepare (sr);
    }

    void Channel::DrumFxBus::reset()
    {
        for (int i = 0; i < 10; ++i) { eqL[i].reset(); eqR[i].reset(); }
        compEnv = 0.0f;
        reverbFx.reset();
        delayFx .reset();
    }

    void Channel::DrumFxBus::process (juce::AudioBuffer<float>& buf, int numSamples,
                                      double hostBPM)
    {
        if (buf.getNumChannels() < 1 || numSamples <= 0) return;

        const bool stereo = buf.getNumChannels() > 1;
        float* L = buf.getWritePointer (0);
        float* R = stereo ? buf.getWritePointer (1) : L;

        // ── 1. 10-band EQ (peaking biquads at fixed ISO frequencies) ─────────
        {
            bool anyActive = false;
            for (int i = 0; i < 10; ++i)
            {
                const float g = eqGainDb[i].load();
                if (std::abs (g - cachedEqGain[i]) > 1e-3f)
                {
                    cachedEqGain[i] = g;
                    // Q ~ 1.4 → roughly one-octave bandwidth per band.
                    eqL[i].setPeaking (kEqFreqs[i], g, 1.4f, sampleRate);
                    eqR[i].setPeaking (kEqFreqs[i], g, 1.4f, sampleRate);
                }
                if (std::abs (cachedEqGain[i]) > 0.05f) anyActive = true;
            }
            if (eqEnabled.load() && anyActive)
            {
                for (int n = 0; n < numSamples; ++n)
                {
                    float l = L[n];
                    for (int i = 0; i < 10; ++i) l = eqL[i].process (l);
                    L[n] = l;
                    if (stereo)
                    {
                        float r = R[n];
                        for (int i = 0; i < 10; ++i) r = eqR[i].process (r);
                        R[n] = r;
                    }
                }
            }
        }

        // ── 2. Saturation (tanh soft-clip, gain-compensated) ─────────────────
        {
            const float drive = satDrive.load();
            const float mix   = satMix.load();
            if (satEnabled.load() && drive > 1e-4f && mix > 1e-4f)
            {
                // Drive [0..1] → pre-gain [1..20], compensate output by 1/preGain
                // so the band-level stays roughly constant as the user turns up.
                const float preGain = 1.0f + drive * 19.0f;
                const float postGain = 1.0f / juce::jmax (1.0f, std::tanh (preGain) / 0.762f);
                const float dryAmt = 1.0f - mix;
                for (int n = 0; n < numSamples; ++n)
                {
                    const float dl = L[n];
                    const float wl = std::tanh (dl * preGain) * postGain;
                    L[n] = dl * dryAmt + wl * mix;
                    if (stereo)
                    {
                        const float dr = R[n];
                        const float wr = std::tanh (dr * preGain) * postGain;
                        R[n] = dr * dryAmt + wr * mix;
                    }
                }
            }
        }

        // ── 3. Compressor (feed-forward peak detector) ───────────────────────
        {
            const float ratio = compRatio.load();
            if (compEnabled.load() && ratio > 1.001f)
            {
                const float threshDb  = compThreshDb.load();
                const float thresh    = juce::Decibels::decibelsToGain (threshDb);
                const float attMs     = juce::jmax (0.1f, compAttackMs.load());
                const float relMs     = juce::jmax (1.0f, compReleaseMs.load());
                const float makeup    = juce::Decibels::decibelsToGain (compMakeupDb.load());

                const float attCoef = std::exp (-1.0f / ((float) sampleRate * attMs * 0.001f));
                const float relCoef = std::exp (-1.0f / ((float) sampleRate * relMs * 0.001f));
                const float invRatio = 1.0f / ratio;

                for (int n = 0; n < numSamples; ++n)
                {
                    const float lin = stereo ? juce::jmax (std::abs (L[n]), std::abs (R[n]))
                                             : std::abs (L[n]);
                    // Envelope follower: attack when input > env, release otherwise.
                    const float coef = (lin > compEnv) ? attCoef : relCoef;
                    compEnv = lin + coef * (compEnv - lin);

                    float gainRed = 1.0f;
                    if (compEnv > thresh && compEnv > 1e-6f)
                    {
                        const float overDb  = juce::Decibels::gainToDecibels (compEnv / thresh);
                        const float redDb   = overDb * (1.0f - invRatio);
                        gainRed = juce::Decibels::decibelsToGain (-redDb);
                    }
                    const float g = gainRed * makeup;
                    L[n] *= g;
                    if (stereo) R[n] *= g;
                }
            }
        }

        // ── 4. Reverb — Betel::ReverbFx, identical to the melodic channels ───
        //
        // Was juce::Reverb into a scratch buffer, then a manual dry gain and an
        // addFrom to sum the wet back.  ReverbFx does the whole dry/wet sum in
        // place, so all of that goes: copy, process, attenuate, add becomes one
        // call on the buffer itself.
        {
            // Slider x base, exactly as on the melodic side.
            const float wet = juce::jlimit (0.0f, 1.0f, revWet.load() * revWetBase.load());
            const float dry = revDry.load();

            // DRY IS PART OF THE TEST.  Pulling DRY down with WET at zero is a
            // real request; the old block could ignore it because its dry gain
            // was applied inside the wet branch.
            if (revEnabled.load() && (wet > 1e-4f || dry < 0.9999f))
            {
                float* rL = buf.getWritePointer (0);
                float* rR = stereo ? buf.getWritePointer (1) : rL;

                // ReverbFx swaps engines on the audio thread and cross-fades the
                // outgoing tail itself, so setting this every block is safe.
                reverbFx.setAlgorithm (revAlgo.load());

                reverbFx.process (rL, rR, numSamples,
                                  revSize.load(), revDamp.load(), wet,
                                  revPreDelay.load(), sampleRate,
                                  dry, revTail.load(),
                                  revHpNorm.load(), revLpNorm.load());
            }
        }

        // ── 5. Delay — Betel::StereoDelayFx, identical to the melodic channels ─
        //
        // Was a hand-rolled ring buffer with a single tap and shared feedback.
        // StereoDelayFx is the ping-pong the melodic slots run, so a 1/8 on a
        // kit and a 1/8 on a guitar are now the same effect and not merely the
        // same number.
        //
        // BOTH TIME SOURCES KEPT.  The engine takes a length in samples, so
        // free-ms versus musical division is decided here, before the DSP - the
        // swap costs nothing that already worked.
        {
            const float wet = juce::jlimit (0.0f, 1.0f, delWet.load() * delWetBase.load());
            const float dry = delDry.load();
            const float fb  = delFb.load();

            if (delEnabled.load() && (wet > 1e-4f || fb > 1e-4f || dry < 0.9999f))
            {
                // Same division tables as Channel::applyDelayInPlace.
                static constexpr float kBeats44 [5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
                static constexpr float kBeats34 [4] = { 3.0f, 1.0f, 0.5f, 0.25f };

                float tSec;
                if (delSync.load())
                {
                    const int   ts  = delTimeSig.load();
                    const int   div = delDiv.load();
                    const float beats = (ts == 0)
                        ? kBeats44[(size_t) juce::jlimit (0, 4, div)]
                        : kBeats34[(size_t) juce::jlimit (0, 3, div)];
                    const float bpm = (float) juce::jmax (20.0, hostBPM);
                    tSec = beats * (60.0f / bpm);
                }
                else
                {
                    tSec = delTimeMs.load() * 0.001f;
                }

                float* dL = buf.getWritePointer (0);
                float* dR = stereo ? buf.getWritePointer (1) : dL;

                delayFx.process (dL, dR, numSamples,
                                 tSec * (float) sampleRate,
                                 fb, wet, sampleRate, dry);
            }
        }
    }

    //==========================================================================
    // Click library DSP retired.  These two entry points remain as inert no-ops
    // so SamplePlayerEngine forwarders and saved state/presets still link.
    //==========================================================================
    bool Channel::loadClickSample (const juce::File&) { return false; }
    void Channel::clearClickSample() {}

    //==========================================================================
    // Preset loading (message thread)
    //==========================================================================
    void Channel::clearPreset()
    {
        ready.store(false);
        for (auto& v : voices) v.reset();
        heldNotes.clear();
        std::atomic_store (&active, std::shared_ptr<PresetVoice>{});
        presetName.clear();
        loadedDrumKitName.clear();
        currentInstrumentFlag.store (-1);

        // BOTH parked sources go too.  clearPreset is the "this channel is being
        // rebuilt" call - a style change or an EMPTY - and a blob voice left
        // behind from the previous style would reappear the moment an SFZ was
        // switched off, which is not the sound the new style asked for.
        blobVoice.reset();
        sfzVoice.reset();
        sfzSelected.store (false);
    }

    // Decode a blob preset into the pool once (message thread only).  Indexed by
    // blob preset index, which is stable for the life of the engine's blob.
    void Channel::preloadPreset(BlobReader& reader, int presetIndex)
    {
        if (presetIndex < 0) return;
        if (presetPool.find (presetIndex) != presetPool.end()) return;   // already pooled

        const auto& presets = reader.getPresets();
        if (presetIndex >= (int) presets.size()) return;
        const auto& preset = presets[(size_t) presetIndex];

        // Spike diagnostics: decoding a preset is the single heaviest
        // message-thread operation in the plugin, and the one most likely to
        // stall the process.  Timed and byte-counted so a log line can be lined
        // up against an audio spike in the same second.
        Betel::PerfMonitor::Scoped perfScope ("DECODE",
            "preset " + juce::String (presetIndex) + " ch" + juce::String (channelIndex));
        int64_t decodedBytes = 0;

        auto pv = std::make_shared<PresetVoice>();
        for (const auto& blobRegion : preset.regions)
        {
            auto fs = reader.getSampleDataFloat(blobRegion);
            if (fs.numFrames == 0) continue;

            Region reg;
            reg.lokey          = blobRegion.keyRangeLow;
            reg.hikey          = blobRegion.keyRangeHigh;
            reg.pitchKeycenter = blobRegion.rootKey;
            reg.lovel          = blobRegion.velRangeLow;
            reg.hivel          = blobRegion.velRangeHigh;
            reg.hasLoop        = blobRegion.loopEnabled > 0;
            reg.loopStart      = blobRegion.loopStart;
            reg.loopEnd        = blobRegion.loopEnd;
            reg.volume         = -blobRegion.attenuation;
            reg.tune           = (int) blobRegion.fineTune
                               + (int) blobRegion.coarseTune * 100;
            reg.offset         = 0;
            reg.roleId         = blobRegion.elementRoleId;  // 0 for non-drum blobs

            SampleData sd;
            fillSampleData (sd, fs.data.data(), fs.channels, fs.numFrames, fs.sampleRate);
            decodedBytes += (int64_t) sd.buffer.getNumChannels()
                          * (int64_t) sd.buffer.getNumSamples() * (int64_t) sizeof (float);

            pv->regions.push_back(reg);
            pv->samples.push_back(std::move(sd));
        }

        presetPool[presetIndex] = std::move (pv);
        Betel::PerfMonitor::get().addSampleBytes (decodedBytes, 1);
    }

    void Channel::loadPreset(BlobReader& reader, int presetIndex)
    {
        clearPreset();

        const auto& presets = reader.getPresets();
        if (presetIndex < 0 || presetIndex >= (int) presets.size()) return;

        preloadPreset (reader, presetIndex);            // decode into pool if needed
        auto it = presetPool.find (presetIndex);
        if (it == presetPool.end()) return;

        std::atomic_store (&active, it->second);        // publish as the active voice
        blobVoice = it->second;                        // the source an SFZ sleeps back to
        presetName = juce::String (presets[(size_t) presetIndex].name);
        ready.store(true);
    }

    // Audio-thread-safe instrument switch: just republish a pooled preset.  If
    // it was never preloaded this does nothing (it must NEVER decode here).
    void Channel::selectPooledPreset(int presetIndex)
    {
        auto it = presetPool.find (presetIndex);
        if (it == presetPool.end()) return;

        // AUDIO-THREAD SAFE, and it must stay that way: assigning blobVoice here
        // would touch a shared_ptr the message thread also writes.  Only publish.
        // The parked source is set by the message-thread load paths.
        std::atomic_store (&active, it->second);
        currentInstrumentFlag.store (presetIndex);
        ready.store (true);

        //----------------------------------------------------------------------
        // Restore this flag's saved voice — and when it HAS none, restore the
        // neutral voice rather than leaving the previous instrument's in place.
        //
        // That last part is the whole point.  A sound whose preset is gain-only
        // (or absent) used to inherit whatever the outgoing sound had applied:
        // save a full voice on the french horn, let the style switch back to the
        // flute, and the flute played through the horn's envelopes, filter and
        // FX because nothing ever cleared them.  Gain was already reset here for
        // exactly this reason; the params were not.
        //
        // Neutral means ChannelParams' own defaults — a stack struct with
        // in-class initialisers, so this allocates nothing and stays RT-safe.
        //
        // Consequence worth knowing: a sound with only a gain-only preset now
        // plays neutral on a program change instead of inheriting. Giving it a
        // voice of its own is what SAVE AS DEFAULT is for.
        //----------------------------------------------------------------------
        //----------------------------------------------------------------------
        // IGNORE PRESET CHANGES - the runtime program-change path honours it too.
        //
        // This is the OTHER door a saved voice comes through: selectChannelPreset
        // handles a fresh load, this handles a program change to a flag already
        // pooled.  Gating only the first would mean a frozen slot survived a set
        // load and was then flattened by the next PC the style sent.
        //
        // Everything above this point still runs: the SAMPLES are swapped by
        // selectPooled below, which is exactly what the freeze is for - a new
        // sound wearing the voicing you already dialled in.
        // The samples are ALREADY published above (active / currentInstrumentFlag
        // / ready), so returning here keeps the new sound and skips only the
        // saved settings - which is precisely the contract.
        if (ignorePresetParams.load())
            return;

        const auto applyNeutralVoice = [this]
        {
            applyParams (ChannelParams{});
            instrumentGain.store (1.0f);
        };

        if (presetIndex < 0 || presetIndex >= kMaxPooledFlag)
        {
            applyNeutralVoice();                  // off the stash grid
            return;
        }
        const auto i = (size_t) presetIndex;

        if (! pooledStashed[i].load (std::memory_order_acquire))
        {
            applyNeutralVoice();                  // no saved voice at all
            return;
        }

        // One load of the flag, one branch: a gain-only stash means the voice is
        // neutral, a full stash means it is the saved one.  Reading the atomic
        // twice would let the message thread change the answer between them.
        if (pooledHasParams[i].load (std::memory_order_acquire))
            applyParams (pooledParams[i]);
        else
            applyParams (ChannelParams{});

        instrumentGain.store (pooledGain[i].load (std::memory_order_relaxed));
    }
} // namespace Betel



