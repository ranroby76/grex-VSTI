#pragma once
//==============================================================================
// InstrEditPanel.h
//
// Shared widget types and the per-slot parameter struct used by every tab in
// the floating InstrEditorWindow.  The old monolithic InstrEditPanel class has
// been split into per-tab panels (SynthesisPanel / ModulationPanel /
// EffectsPanel) so what stays here is just the data and the
// reusable widgets.
//
// Phase 1 addition: DrumKitParams is embedded inside SlotParams.  When the
// slot's timbre is the DRUMS category the host treats this slot as a drum
// channel; the existing fields (filter resonance/type, sustain, pan, EQ,
// reverb, delay) act as the per-slot kit-wide settings, and DrumKitParams
// carries the 128 per-key entries (sample selection + gain + pitch +
// filter cutoff + round-robin + A/D/R).
//==============================================================================

#include <JuceHeader.h>
#include "DrumElementRoles.h"

// ── Per-key drum element params ──────────────────────────────────────────────────────
//
// One of these per MIDI key (128 entries) inside a drums-mode slot.  An entry
// is "empty" (silent) when `sourceKit.isEmpty()` — the kit + role pair acts as
// a stable handle the host resolves through DrumKitRegistry::findElement()
// when the engine needs to load this key's sample.
//
// Defaults match the per-slot (melodic) defaults already in SlotParams so a
// freshly-initialised drum kit sounds neutral.
//==============================================================================
// VELOCITY CURVE — shared by the melodic editor and the drum editor.
//
// One bipolar number instead of the eight fixed curves a hardware panel shows:
// -1 is the softest (a hard player's setting), 0 is linear, +1 the hardest.
// Continuous because there is a screen here, and the eight presets on a front
// panel only exist because a knob with detents is cheaper than a display.
//
// The exponent is symmetric in LOG space, so -50 and +50 bend the line by the
// same amount in opposite directions:
//
//     gamma = kMaxGamma ^ (-curve)      curve -1 -> 2.5   0 -> 1.0   +1 -> 0.4
//     out   = 127 * (in / 127) ^ gamma
//
// WORTH KNOWING, because it decides what this feature is FOR: 0 and 127 are
// both FIXED POINTS of that expression.  A curve reshapes the DISTRIBUTION -
// it can pull the middle down and widen the gap between an ordinary hit and an
// accent - but it can never lower the ceiling.  Taming a slammed style still
// needs the Sweetener's PEAK stage or a level trim; this puts the dynamics
// back, which is a different job.
//==============================================================================
namespace VelCurve
{
    static constexpr float kMaxGamma = 2.5f;

    inline float gammaFor (float curve) noexcept
    {
        return std::pow (kMaxGamma, -juce::jlimit (-1.0f, 1.0f, curve));
    }

    /** 0..1 in, 0..1 out.  The shape, without MIDI's integer edges - the
        display draws this and apply() rounds it. */
    inline float shape (float x, float curve) noexcept
    {
        if (std::abs (curve) < 1.0e-3f) return x;
        return std::pow (juce::jlimit (0.0f, 1.0f, x), gammaFor (curve));
    }

    /** A MIDI velocity through the curve.  Never returns 0 for a note that was
        struck: velocity 0 is a note-OFF and turning a quiet hit into one would
        strand it. */
    inline int apply (int velocity, float curve) noexcept
    {
        if (velocity <= 0 || std::abs (curve) < 1.0e-3f) return velocity;
        const float out = shape ((float) velocity / 127.0f, curve) * 127.0f;
        return juce::jlimit (1, 127, (int) std::lround (out));
    }
}

//==============================================================================
// PEAK RATIO — slider position <-> compression ratio, LOGARITHMICALLY.
//
// A linear 1..20 mapping is unusable, and this is why: compression ratio is
// perceived in the low numbers.  1:1 to 2:1 is the difference between "off" and
// "obviously compressing"; 15:1 to 20:1 is inaudible.  Linear spent five
// percent of the travel on the entire useful range and the other ninety-five on
// shades of limiting, so nudging the slider off zero slammed everything.
//
//     position   linear (old)      log (now)
//        5 %         1.95              1.17
//       25 %         5.75              2.11
//       50 %        10.50              4.47
//      100 %        20.00             20.00
//==============================================================================
//==============================================================================
//  PEAK RATIO — the slider is linear in the SLOPE, not in the ratio.
//
//  It used to be ratio = 20^x, which reads like a sensible log law and is not:
//  what a compressor actually does with a ratio is remove `1 - 1/ratio` of the
//  excess, and that quantity saturates fast.  Under the old law:
//
//      ui  10  ->  1.35:1  ->  26% of the excess already gone
//      ui  20  ->  1.82:1  ->  45%
//      ui  30  ->  2.46:1  ->  59%
//
//  Half the total effect arrived by ui 22, and the whole top half of the travel
//  moved between 4.5:1 and 20:1 - two settings that sound much the same.  That
//  is why a slight push felt savage and a big push felt like nothing.
//
//  Mapping the slider to the SLOPE instead makes the number mean what a player
//  assumes it means: ui 25 removes a quarter of the excess, 50 removes half.
//  The end of the travel still reaches 20:1, so nothing is lost at the top.
//==============================================================================
namespace PeakRatio
{
    static constexpr float kMax      = 20.0f;
    static constexpr float kMaxSlope = 1.0f - 1.0f / kMax;   // 0.95 at 20:1

    inline float toNative (float ui01) noexcept          // 0..1 -> 1..20
    {
        const float slope = kMaxSlope * juce::jlimit (0.0f, 1.0f, ui01);
        return juce::jlimit (1.0f, kMax, 1.0f / juce::jmax (1.0e-4f, 1.0f - slope));
    }

    inline float toUi01 (float ratio) noexcept           // 1..20 -> 0..1
    {
        const float r = juce::jlimit (1.0f, kMax, ratio);
        return juce::jlimit (0.0f, 1.0f, (1.0f - 1.0f / r) / kMaxSlope);
    }
}

/** The curve, drawn and draggable.  Vertical drag bends it; double-click
    returns to linear.  Small enough to live in half a column. */
class VelCurveDisplay : public juce::Component
{
public:
    std::function<void(float)> onChange;

    void setCurve (float c, juce::NotificationType n = juce::sendNotification)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, c);
        if (std::abs (v - curve) < 1.0e-4f) return;
        curve = v;
        repaint();
        if (n == juce::sendNotification && onChange) onChange (curve);
    }
    float getCurve() const noexcept { return curve; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().reduced (2).toFloat();
        g.setColour (juce::Colour (0xFF141414));
        g.fillRoundedRectangle (b, 3.0f);

        // Grid + the linear diagonal, so the bend is readable against a
        // reference rather than as an abstract line.
        g.setColour (juce::Colour (0xFF2E2E2E));
        for (int i = 1; i < 4; ++i)
        {
            const float t = (float) i / 4.0f;
            g.drawHorizontalLine ((int) (b.getY() + t * b.getHeight()), b.getX(), b.getRight());
            g.drawVerticalLine   ((int) (b.getX() + t * b.getWidth()),  b.getY(), b.getBottom());
        }
        g.setColour (juce::Colour (0xFF3A3A3A));
        g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getY(), 1.0f);

        juce::Path path;
        const int steps = juce::jmax (16, (int) b.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const float y = VelCurve::shape (x, curve);
            const float px = b.getX() + x * b.getWidth();
            const float py = b.getBottom() - y * b.getHeight();
            if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }
        g.setColour (juce::Colour (0xFFCC6600));
        g.strokePath (path, juce::PathStrokeType (2.0f));

        g.setColour (juce::Colour (0xFFC2C2C2));
        g.setFont (11.0f);
        g.drawText (label(), getLocalBounds().removeFromBottom (14),
                    juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override { dragFrom = curve; lastY = e.position.y; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float dy = lastY - e.position.y;          // up = harder
        setCurve (dragFrom + dy / juce::jmax (40.0f, (float) getHeight()));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { setCurve (0.0f); }

private:
    juce::String label() const
    {
        if (std::abs (curve) < 0.02f) return "LINEAR";
        const int pct = (int) std::lround (std::abs (curve) * 100.0f);
        return (curve < 0.0f ? "SOFT " : "HARD ") + juce::String (pct);
    }

    float curve = 0.0f, dragFrom = 0.0f, lastY = 0.0f;
};

struct DrumElementParams
{
    juce::String sourceKit;                    // display name of the source kit-blob; "" = empty key
    uint8_t      sourceRoleId    = 0;          // DrumElementRole enum value
    float        gain            = 1.0f;       // linear, 0..2 (0 dB = 1.0)
    float        pitch           = 0.0f;       // semitones, -24..+24
    // Velocity curve for THIS element.  Per-key, so the drum editor's
    // keysToEdit() gives it the behaviour Rob asked for with no branching:
    // a composed kit writes it across the selected COMPONENT's keys (per
    // category), a sampled kit writes all 128 (global for the kit).
    float        velCurve        = 0.0f;       // -1 soft .. 0 linear .. +1 hard
    float        filterCutoff    = 1.0f;       // 0..1 normalised; kept for back-compat
                                               // (the new DrumsPopup omits the FILTER
                                               // slider in favour of FX SEND, so this
                                               // stays at 1.0f for newly-loaded kits)
    // ── TWO-HANDLE BAND FILTER, per element ──────────────────────────────
    // The same low-cut / high-cut pair the melodic slots have.  A fast way to
    // thin a boomy kick or take the fizz off a hat without opening the EQ and
    // spending five bands on what two thumbs do in one gesture.
    //   filterHpNorm 0.0 = 20 Hz   (low-cut fully open, no effect)
    //   filterLpNorm 1.0 = 20 kHz  (high-cut fully open, no effect)
    // Shared frequency law with the engine and the melodic band: 20 * 1000^norm.
    float        filterHpNorm    = 0.0f;
    float        filterLpNorm    = 1.0f;

    int          roundRobinAmount = 0;         // 0..100 pseudo-RR depth (per-hit jitter)
    float        attack          = 0.001f;     // seconds — Kitton "STRIKE"
    float        length          = 100.0f;     // 0..100 combined LENGTH (decay+sustain).
                                               // 100 = full length: the hit plays to the
                                               // sample natural end at full level; below
                                               // 100 it fades to silence over a decay time
                                               // that scales with LENGTH.
    float        release         = 0.05f;      // seconds — Kitton "RELEASE"
    float        fxSend          = 0.0f;       // RETIRED — per-key FX send is no
                                               // longer used for routing (the kit
                                               // FX rack is now a global insert).
                                               // Field kept so older saved kits
                                               // load without losing data.
    bool         fullLength      = true;       // one-shot: ignore note-off, play
                                               // sample start→end on every trigger.
                                               // When false, the RELEASE slider's
                                               // note-off fade applies as normal.

    // Kick MIX (notes 35 & 36 only): equal-power crossfade between the kit's
    // own kick and a supplemental EDM or WOOD kick (loaded once from
    // <drums>/kickmix/).  Per-note, saved with the kit.  Ignored on all other
    // keys.
    //
    // Default: OFF.  SamplePlayerEngine::resolveDrumKitParams() builds EVERY
    // style-loaded kit from these defaults, so defaulting this on would blend
    // the (tonal, pitched) EDM kick into every style's kick and change the kit's
    // character before the user asks for it.  Opt-in only.
    bool         kickMixEnabled  = false;      // on/off toggle
    float        kickMixAmount   = 50.0f;      // 0..100, 101 steps; 0 = kit kick, 100 = supplemental
    int          kickMixVariant  = 0;          // 0 = EDM, 1 = WOOD
};

// ── SWEETENER parameters ──────────────────────────────────────────────────────────────
//
// One block, three stages, carried IDENTICALLY by a melodic slot (SlotParams)
// and a drum slot (DrumKitFxParams) so both drive the same SweetenerFx and
// sound the same.  See SweetenerFx.h for what each stage does and why it is a
// ratio rather than a threshold.
//
// Default OFF everywhere.  SoundsTab turns it ON for the DRUMS and PERC slots,
// because that is where converted styles actually hurt, and a default that
// changes every melodic sound the moment a user upgrades would be a surprise.
struct SweetenerParams
{
    bool  enabled = false;
    float mix     = 1.0f;          // 0..1  parallel blend of the whole block

    bool  softenOn    = true;
    float softenDepth = 0.35f;     // -1..+1   negative softens, positive sharpens
    float softenMs    = 8.0f;      // 1..150   how much of the hit is treated

    // PEAK — the reducer.  Auto-threshold: CEILING is dB above the material's
    // own running body, so "savage" means savage FOR THIS PART.
    bool  peakOn     = true;
    float peakCeilDb = 12.0f;      // 3..24
    float peakRatio  = 4.0f;       // 1..20   1 = off, 20 = limiting

    bool  tameOn      = true;
    float tameDepthDb = 4.0f;      // 0..12   maximum duck of the harsh band
    float tameFreqHz  = 4000.0f;   // 1500..8000

    bool  roundOn    = true;
    float roundDrive = 0.25f;      // 0..1
    float roundMix   = 1.0f;       // 0..1
};

// ── Kit-wide FX bus parameters ────────────────────────────────────────────────────────
//
// One shared FX chain per drum slot, ordered EQ → Saturation → Compressor →
// Reverb → Delay.  The rack is an INSERT: the entire kit mix is summed and run
// through the chain, then blended back into the dry mix via the master `fxWet`
// (the DrumsPopup "WET" fader).  1.0 = fully wet (whole mix through the rack —
// transparent when all stages are off), 0.0 = rack bypassed.
//
// The 10-band EQ uses standard ISO frequencies; gain values are stored
// directly in dB.  Slider mapping (UI side):  0.0 → -60 dB (silence),
// 0.5 → 0 dB (unity), 1.0 → +20 dB (max boost).
struct DrumKitFxParams
{
    // Per-stage on/off (drum kit FX bus).  Default OFF (sound calibration) so a
    // freshly-loaded kit starts clean; the DrumsPopup exposes a toggle per stage
    // and SoundsTab::applySoundCalibration re-asserts these off on every load.
    bool  eqEnabled   = false;
    bool  satEnabled  = false;
    bool  compEnabled = false;
    bool  revEnabled  = false;
    bool  delEnabled  = false;
    // 10-band EQ — peaking filters at 31 / 62 / 125 / 250 / 500 / 1k / 2k / 4k / 8k / 16k Hz.
    // gainDb[i] in [-60..+20] dB, neutral at 0 dB.
    float eqGainDb [10] = { 0,0,0,0,0,0,0,0,0,0 };

    // Saturation (tanh soft clipping).
    float satDrive   = 0.0f;          // 0..1  (0 = bypass, 1 = ~20× pre-gain)
    float satMix     = 1.0f;          // 0..1  wet mix

    // Compressor (feed-forward, peak detector).
    float compThreshDb = 0.0f;        // -60..0  dB
    float compRatio    = 1.0f;        // 1..20
    float compAttackMs = 5.0f;        // 0.1..200 ms
    float compReleaseMs= 50.0f;       // 5..2000 ms
    float compMakeupDb = 0.0f;        // 0..24 dB

    // Reverb.  THE SAME ENGINE THE MELODIC CHANNELS RUN - Betel::ReverbFx, an
    // FDN with a Dattorro plate alternative - not juce::Reverb any more.  Every
    // field below is the drum-side name for a SlotParams reverb field, and the
    // two must keep the same meanings or the racks drift apart again.
    float revSize = 0.5f;             // 0..1   <-> reverbSize
    float revDamp = 0.5f;             // 0..1   <-> reverbDamp
    float revWet  = 0.0f;             // 0..1   <-> reverbWet
    float revDry  = 1.0f;             // 0..1   <-> reverbDry
    float revTail     = 0.5f;         // RT60,  <-> reverbTail
    float revPreDelay = 0.0f;         // 0..1 -> 0..200 ms, <-> reverbPreDelay
    float revHpNorm   = 0.2917f;      // send band low  edge, <-> reverbHpNorm
    float revLpNorm   = 1.0f;         // send band high edge, <-> reverbLpNorm
    int   revAlgo     = 0;            // 0 = HALL/ROOM (FDN), 1 = PLATE
    float revWetBase  = 1.0f;         // what full WET travel is worth

    // Delay.  Two time sources, matching the melodic delay in EffectsPanel:
    //   delSync == false -> free running time, delTimeMs
    //   delSync == true  -> musical division, (delTimeSig, delDiv) x host BPM
    // The free time is kept rather than replaced so switching SYNC off returns
    // to exactly the ms the user had dialled.
    bool  delSync    = false;         // false = free ms, true = tempo-synced
    int   delTimeSig = 0;             // 0 = 4/4, 1 = 3/4
    int   delDiv     = 2;             // index into the active division table
    float delTimeMs = 250.0f;         // 1..2000 ms (free mode)
    float delFb     = 0.3f;           // 0..0.95
    float delWet    = 0.0f;           // 0..1
    // The rack now runs Betel::StereoDelayFx - the melodic ping-pong - so DRY
    // is an explicit control rather than the implicit 1.0 the old hand-rolled
    // ring buffer summed against, and WET has the same base multiplier.
    float delDry     = 1.0f;          // 0..1   <-> delayDry
    float delWetBase = 0.5f;          // <-> delayWetBase, same 0.5 and same reason

    // Drum channel stereo pan, -1..+1 (0 = centre).  Set in the DrumsPopup PAN
    // tab; carried on the kit-FX bus so it persists and applies with the kit.
    float pan = 0.0f;

    // SWEETENER — the per-slot dynamics block.  Deliberately NOT part of the
    // rack below and NOT governed by fxWet: it is applied to the summed kit mix
    // BEFORE the rack splits into dry and wet, so it treats the kit whether or
    // not the user has opened the advanced rack at all.
    SweetenerParams sweet;

    // Global rack wet/dry.  The whole kit mix is fed through the FX rack as an
    // insert; this is the master blend between the dry kit and the processed
    // output.  1.0 = fully wet (entire mix through the rack — transparent when
    // all stages are off), 0.0 = rack bypassed.  Replaces the old per-key send.
    float fxWet = 1.0f;               // 0..1
};

// ── Per-slot drum-kit container ──────────────────────────────────────────────────────
//
// Holds the 128 per-key entries + the kit-name that was last loaded as a
// whole into this slot (so the popup's KIT dropdown shows the right choice
// on reopen).  When `lastLoadedKit` is empty the slot is in "custom" mode
// (user has hand-mixed elements from multiple kits).
struct DrumKitParams
{
    juce::String                                 lastLoadedKit;   // "" = custom hybrid
    std::array<DrumElementParams, 128>           keys {};
    DrumKitFxParams                              fx;              // shared FX bus for this kit

    /** True if at least one key carries a source sample (non-empty sourceKit).
        A default ".drm" marker has none, so callers fall back to the registry
        build instead of loading an empty (silent) kit. */
    bool hasMappedKeys() const
    {
        for (const auto& k : keys)
            if (k.sourceKit.isNotEmpty()) return true;
        return false;
    }
};

// ── Per-slot instrument params ────────────────────────────────────────────────────────
struct SlotParams
{
    // Amp envelope (ADSR, ms / 0..1)
    // A 0 · D 7 s (slider 100, REAL time-to-silence) · S 0 · R 0.45 s (slider
    // 15 — generic; GM picks override per family, see SoundsTab).
    float attack  = 0.0f, decay  = 7.0f, sustain = 0.0f, release = 0.45f;
    int   ampCurve = 0;   // amp decay/release shape: 0=Exp 1=Lin 2=Log

    // Stereo pan, -1 (full left) .. +1 (full right), 0 = centre.  User-set in
    // the sound editor's PAN tab; the style never touches it.
    float pan = 0.0f;

    //==========================================================================
    // PER-SOUND GAIN TRIM (dB).  0 = unmodified.
    //
    // This is calibration, not performance: the amount THIS instrument (or kit)
    // needs to sit level with every other sound in the library.  A grand piano
    // that arrives hotter than the strings gets its correction here, once, and
    // carries it everywhere it is ever loaded.
    //
    // Deliberately NOT in the FX rack and NOT on the mixer:
    //   * outside the rack, so a global macro (Big Drums / Funkey) can never
    //     swap a calibration away while replacing effects;
    //   * outside the mixer, so it composes with the fader instead of fighting
    //     it — the style still states its own CC 7 and the fader still reads
    //     the composer's number.
    //
    // Engine gain = fader x expression x autoLevel x THIS.  It lives in
    // SlotParams, so an .ins / .drm preset and a saved set both carry it with
    // no extra plumbing, and selectChannelPreset re-applies it on every style
    // program change that loads the sound.
    //
    // SCALE: 0..200, where 100 IS UNITY — the same "unity" the mixer fader
    // means at CC 7 = 127.  So a sound left at 100 plays at exactly the level
    // the style asked for, 0 is silence, and 200 is twice unity (+6 dB).  The
    // law is linear: engine gain = gainPercent / 100.
    //
    // Percent rather than dB precisely so the two controls agree: the fader
    // states the composer's level, and this states how the SOUND sits against
    // that base.  Reading "100" on both means "nothing added, nothing taken".
    //==========================================================================
    float gainPercent = 100.0f;

    //==========================================================================
    // BASE UNITY (dB).  DEVELOPER-SIDE, one value per instrument, forever.
    //
    // gainPercent is the user's trim and 100 is its middle.  THIS is what that
    // middle MEANS: the correction the sample itself needs before anyone trims
    // it.  Engine gain = dbToGain(baseUnityDb) x (gainPercent / 100), so 100
    // always lands exactly on the base no matter how far the base moved.
    //
    // Why it exists separately: gainPercent is linear 0..200, so an instrument
    // needing -20 dB sits at 10 and there is almost no resolution left to trim
    // with.  A dB base underneath restores the whole 0..200 span around
    // whatever the correct level for that sample turns out to be.
    //
    // Reached by left double-click on a GAIN handle, written to BOTH the .ins
    // and the .sins for that flag, and applied wherever the instrument loads.
    // Range -40..+40 dB.
    //==========================================================================
    float baseUnityDb = 0.0f;

    // Filter — encoding now matches the engine: 0=LP, 1=HP, 2=BP, 3=Notch
    float filterCutoff   = 1.0f;
    float filterReson    = 0.0f;
    int   filterType     = 0;
    float filterKeytrack = 0.0f;   // cents per semitone (0..100)

    // Band filter (non-bass style instruments): low-cut / high-cut edges as
    // 0..1 log-norms mapped to 20 Hz .. 20 kHz (20 * 1000^norm).  0 = 20 Hz,
    // 1 = 20 kHz, so hp=0 / lp=1 is fully OPEN (transparent).  Bass + solo
    // instruments ignore these and use filterCutoff/reson/type above.
    float filterHpNorm   = 0.0f;   // low-cut (high-pass edge),  0 = 20 Hz open
    float filterLpNorm   = 1.0f;   // high-cut (low-pass edge),  1 = 20 kHz open

    // Filter envelope (extended with amount)
    float fEnvA = 0.01f, fEnvD = 0.2f, fEnvS = 0.5f, fEnvR = 0.3f;
    float fEnvAmount = 0.0f;       // 0..1 (scaled to ~10 kHz at the engine)

    // Mono / portamento  (from the shared sampler — drum channels ignore these)
    int   playMode       = 0;      // 0=Poly, 1=Mono
    // Mono note-priority: three INDEPENDENT switches (multi-select).
    bool  monoHoldStolen   = true;   // note-off falls back to a still-held note
    bool  monoRetrigNew    = false;  // a new note restarts sample + envelope
    bool  monoRetrigStolen = false;  // the fallback note restarts
    float portamentoTime = 100.0f; // ms

    // Per-instrument octave shift, -3..+3 octaves (applied at note-on as
    // note + octave*12).  Lets each instrument sit in the right register —
    // useful for Yamaha style parts written very low (e.g. bass around C1).
    int   octaveOffset   = 0;      // -3..+3

    // Allowed-notes window (style melodic channels only).  When enabled, the
    // engine folds incoming notes into the [noteRangeLo, noteRangeHi] MIDI
    // window so a part can't stray out of its intended register.  Set per slot
    // from the Sounds tab; bass (slot 2) defaults ON to a 12-note window,
    // other melodic channels default OFF.  Drums and solo slots ignore these.
    bool  noteRangeOn = false;     // master on/off for the window
    int   noteRangeLo = 42;        // low MIDI note (inclusive) — default F#2
    int   noteRangeHi = 45;        // high MIDI note (inclusive)

    // Amp LFO  (enabled = explicit on/off toggle, default OFF)
    bool  ampLfoEnabled = false;
    float ampLfoRate   = 0.0f, ampLfoDepth   = 0.0f, ampLfoDelay   = 0.0f;
    // Filter LFO  (enabled = explicit on/off toggle, default OFF)
    bool  filtLfoEnabled = false;
    float filtLfoRate  = 0.0f, filtLfoDepth  = 0.0f, filtLfoDelay  = 0.0f;
    // Pitch LFO
    float pitchLfoRate = 0.0f, pitchLfoDepth = 0.0f, pitchLfoDelay = 0.0f;

    // Pitch envelope
    float pEnvA = 0.0f, pEnvD = 0.0f, pEnvS = 0.0f, pEnvR = 0.0f;
    float pEnvDepth = 0.0f;        // semitones at full env level

    // 5-band EQ
    bool  eqEnabled = false;       // melodic insert-FX on/off (default OFF)
    float eqGain[5] = {};
    float eqFreq[5] = { 200.f, 600.f, 1500.f, 5000.f, 12000.f };

    // Reverb
    bool  reverbEnabled = false;   // melodic insert-FX on/off (default OFF)
    float reverbSize = 0.5f, reverbDamp = 0.5f, reverbWet = 0.0f;
    // DRY is its own control now: 1.0 = the source untouched, which is what a
    // send-style reverb has to default to.  WET only adds the tail.
    float reverbDry  = 1.0f;
    // TAIL — decay length (RT60), independent of SIZE.  Loads with reverbSize
    // as its fallback so a set written before it existed keeps its character.
    float reverbTail = 0.5f;
    // Two-handle band on the reverb SEND — wet only, dry untouched.
    // 0.2917 maps to 150 Hz, the constant the left handle replaces.
    float reverbHpNorm = 0.2917f;
    float reverbLpNorm = 1.0f;
    float reverbPreDelay = 0.0f;                    // 0..1 -> 0..200 ms

    // Delay
    bool  delayEnabled = false;    // melodic insert-FX on/off (default OFF)
    int   delayTimeSig = 0;        // 0=4/4, 1=3/4
    int   delayDiv = 2;
    float delayFeedback = 0.3f, delayWet = 0.0f;
    // DRY is its own control, exactly as it is on the reverb above: 1.0 = the
    // source untouched, and WET only adds repeats on top of it.
    float delayDry = 1.0f;

    // ── WET BASE GAINS - what "100" on a WET slider is worth ─────────────────
    //
    // Same idea as baseUnityDb on the GAIN handle, and reached the same way (a
    // LEFT double-click on the slider), but a MULTIPLIER rather than dB: 1.0 =
    // the slider means what it says, 0.5 = full travel is half gain.
    //
    // The delay's default is 0.5 and that is deliberate compensation, not a
    // taste call.  Now that dry sits at 1.0 independently, a wet that also
    // reached 1.0 would add a second full-level signal on top of an untouched
    // source.  Half keeps the loudest setting musical.
    //
    // The reverb's default is 1.0 - NOT 0.5.  Its dry/wet were already
    // independent, so every reverb ever saved was voiced against a wet that
    // means what it says.  Halving it here would quietly re-voice all of them.
    float delayWetBase  = 0.5f;
    float reverbWetBase = 1.0f;

    // Velocity curve for the whole slot - see the VelCurve namespace.
    float velCurve = 0.0f;         // -1 soft .. 0 linear .. +1 hard

    // SWEETENER — see SweetenerParams.  First in the melodic insert chain, so
    // it treats the raw instrument and the EQ shapes what comes out of it.
    SweetenerParams sweet;

    // Click library (transient attack-layer fired on every noteOn)
    bool         clickEnabled  = false;
    float        clickVolume   = 0.5f;     // 0..1
    float        clickDecayMs  = 150.0f;   // 0..1500
    juce::String clickFilePath;            // absolute path; empty = no sample loaded

    // NOTE: the per-slot Arabic scale tuning was REMOVED.  The oriental scale
    // is a GLOBAL feature (left panel), applied to the sounding solo channels
    // via SamplePlayerEngine::setChannelScaleTuningCents, so a per-slot copy
    // only ever fought it.  The engine-side API stays; this field is gone.

    // ── Sounds-path insert FX (melodic slots only) ───────────────────────
    // Chorus (normalised 0..1 controls)
    bool  chorusEnabled  = false;
    float chorusRate     = 0.5f;
    float chorusDepth    = 0.5f;
    float chorusMix      = 0.0f;
    // Auto-wah
    bool  wahEnabled     = false;
    float wahSensitivity = 0.5f;
    float wahRate        = 1.0f;
    float wahLfoDepth    = 0.0f;
    float wahBaseHz      = 400.0f;
    float wahQ           = 0.6f;
    float wahMix         = 0.0f;
    // Phaser (normalised 0..1 controls)
    bool  phaserEnabled  = false;
    float phaserRate     = 0.4f;
    float phaserDepth    = 0.8f;
    float phaserFeedback = 0.5f;
    float phaserMix      = 0.0f;

    // ── Drum-kit data (only used when slot is in DRUMS timbre) ───────────
    // The above per-slot fields (filter resonance + type, sustain, pan-via-
    // panL/panR baked into the engine, EQ, reverb, delay) double
    // as the kit-wide settings shared across all 128 keys.  The drums-only
    // per-key state (sample, gain, pitch, filter cutoff, A/D/R, RR) lives in
    // `drumKit`. ~6 KB per SlotParams; negligible.
    DrumKitParams drumKit;
};

// ── GoldSlider ────────────────────────────────────────────────────────────────────────
//
// All slider visuals are LOCKED to fixed pixel sizes — handle diameter is 4× the
// track width regardless of how wide or how packed the parent frame is, so a
// 5-slider row looks the same as a 3-slider row.
//
// Visual layers (drawn bottom-up):
//   1. Medium-gray rounded backdrop                  (one per slider)
//   2. Black-gradient track (the "railway")
//   3. Bronze fill from handle position downward
//   4. Black-gradient outer handle (40 px diameter)
//   5. Bronze inner circle (14 px diameter, smaller than the handle)
//   6. Value text on top, label text on bottom
//
//   ┌─────────┐
//   │  VALUE  │   <- 14 pt
//   │ ░░░░░░░ │   <- medium-gray backdrop
//   │ ░ ███ ░ │   <- 10 px black railway / bronze fill
//   │ ░ ███ ░ │
//   │ ░ ◉◯ ░ │   <- 40 px black handle w/ bronze dot
//   │ ░ ███ ░ │
//   │ ░ ███ ░ │
//   │ ░░░░░░░ │
//   │  LABEL  │   <- 16 pt bold
//   └─────────┘
class GoldSlider : public juce::Component
{
public:
    // ── Locked geometry ───────────────────────────────────────────────────────
    static constexpr float kTrackWidth     = 10.0f;
    static constexpr float kOuterRadius    = 20.0f;   // 40 px black handle (4 × track width)
    static constexpr float kInnerRadius    = 7.0f;    // 14 px bronze inner dot
    static constexpr float kValueFont      = 14.0f;
    static constexpr float kLabelFont      = 16.0f;
    static constexpr float kValueAreaH     = 20.0f;
    static constexpr float kLabelAreaH     = 24.0f;
    static constexpr float kHandleClearance = 2.0f;   // gap between handle and value/label areas
    static constexpr float kVerticalReserved =
        kValueAreaH + kLabelAreaH + 2.0f * kOuterRadius + 2.0f * kHandleClearance;
    // Horizontal mode reserves only handle margin at each end; value text /
    // label live in dedicated top / bottom bands and don't consume track length.
    static constexpr float kHorizontalReserved =
        2.0f * kOuterRadius + 2.0f * kHandleClearance;

    std::function<void(float)> onChange;

    GoldSlider(const juce::String& lbl, float minV, float maxV, float defV,
               const juce::String& unitStr_ = "")
        : label(lbl), unit(unitStr_), minVal(minV), maxVal(maxV), value(defV), defVal(defV) {}

    /** Set a snap step (e.g. 1.0f for integer 0..100 sliders).  step <= 0
        means continuous (default).  Existing value is re-quantised. */
    void setStep(float s)
    {
        step = juce::jmax(0.0f, s);
        if (step > 0.0f) value = quantise(value);
        repaint();
    }
    float getStep() const { return step; }

    /** Switch to horizontal track layout (default is vertical).  Used by the
        portamento and octave sliders in SynthesisPanel where the host frame
        is short and wide and 7 discrete octave steps need horizontal room to
        be hit-targetable. */
    void setHorizontal (bool h) noexcept { horizontal = h; repaint(); }
    bool isHorizontal() const noexcept   { return horizontal; }

    void setValue(float v, bool notify = false)
    {
        const float clamped = juce::jlimit(minVal, maxVal, v);
        const float snapped = (step > 0.0f) ? quantise(clamped) : clamped;
        if (snapped == value && ! notify) return;
        value = snapped;
        repaint();
        if (notify && onChange) onChange(value);
    }
    float getValue() const { return value; }

    void paint(juce::Graphics& g) override
    {
        if (horizontal) paintHorizontal(g);
        else            paintVertical  (g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragX = e.x;
        dragY = e.y;
        dragVal = value;
        setMouseCursor(horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                  : juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;      // right button = reset only
        if (horizontal)
        {
            const float trkW = (float) getWidth() - kHorizontalReserved;
            if (trkW > 0.0f)
                setValue(dragVal + ((float)(e.x - dragX) / trkW) * (maxVal - minVal), true);
        }
        else
        {
            const float trkH = (float) getHeight() - kVerticalReserved;
            if (trkH > 0.0f)
                setValue(dragVal + ((float)(dragY - e.y) / trkH) * (maxVal - minVal), true);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    { setMouseCursor(juce::MouseCursor::NormalCursor); }

    //==========================================================================
    // DOUBLE-CLICK GESTURES
    //
    //   RIGHT double-click -> reset to default.  (Moved here from the left
    //                         button so the left one is free.)
    //   LEFT  double-click -> onLeftDoubleClick, if the owner set one.  Only the
    //                         GAIN sliders do; everywhere else it is inert.
    //
    // A right DRAG no longer moves the value either — otherwise a slightly
    // sloppy right double-click would nudge the very number it is resetting.
    //==========================================================================
    std::function<void()> onLeftDoubleClick;

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) { setValue(defVal, true); return; }
        if (onLeftDoubleClick) onLeftDoubleClick();
    }

private:
    void paintVertical(juce::Graphics& g)
    {
        const float W = (float) getWidth();
        const float H = (float) getHeight();
        if (W < 4.0f || H < 8.0f) return;

        const float cx = W * 0.5f;
        const float trkX = cx - kTrackWidth * 0.5f;
        const float trkT = kValueAreaH + kOuterRadius + kHandleClearance;
        const float trkB = H - kLabelAreaH - kOuterRadius - kHandleClearance;
        const float trkH = juce::jmax(8.0f, trkB - trkT);

        // ── Medium-gray rounded backdrop (one card per slider) ────────────────
        {
            const float inset  = 2.0f;
            const float corner = 6.0f;
            juce::Rectangle<float> bg(inset, inset, W - inset * 2.0f, H - inset * 2.0f);

            juce::ColourGradient bgGrad(juce::Colour(0xFF454545), bg.getX(), bg.getY(),
                                        juce::Colour(0xFF323232), bg.getX(), bg.getBottom(), false);
            g.setGradientFill(bgGrad);
            g.fillRoundedRectangle(bg, corner);

            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawRoundedRectangle(bg, corner, 1.0f);
        }

        // ── Recessed "valley" railway ─────────────────────────────────────────
        // Symmetric cross-section: the two outer edges are lighter (light from
        // above catches the lips of the groove), and the centre is darkest
        // (the floor of the U-shaped valley sits in shadow). This reads as a
        // channel cut INTO the panel, the opposite of a raised cylinder.
        juce::ColourGradient trackGrad(juce::Colour(0xFF2E2E2E), trkX - 1.0f, 0.0f,
                                       juce::Colour(0xFF2E2E2E), trkX + kTrackWidth + 1.0f, 0.0f, false);
        trackGrad.addColour(0.5, juce::Colour(0xFF020202));
        g.setGradientFill(trackGrad);
        g.fillRoundedRectangle(trkX, trkT, kTrackWidth, trkH, kTrackWidth * 0.5f);

        // Thin black outline reads as the groove's hard edge against the panel
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.85f));
        g.drawRoundedRectangle(trkX + 0.5f, trkT + 0.5f,
                               kTrackWidth - 1.0f, trkH - 1.0f, kTrackWidth * 0.5f - 0.5f, 0.8f);

        // ── Bronze fill from handle position down ─────────────────────────────
        const float norm   = (maxVal > minVal) ? (value - minVal) / (maxVal - minVal) : 0.0f;
        const float thumbY = trkB - norm * trkH;

        juce::Path pillPath;
        pillPath.addRoundedRectangle(trkX, trkT, kTrackWidth, trkH, kTrackWidth * 0.5f);
        g.saveState();
        g.reduceClipRegion(pillPath);

        juce::ColourGradient bronzeGrad(juce::Colour(0xFFF0B265), trkX - 1.0f, 0.0f,
                                        juce::Colour(0xFF6E4419), trkX + kTrackWidth + 1.0f, 0.0f, false);
        bronzeGrad.addColour(0.5, juce::Colour(0xFFB87A36));
        g.setGradientFill(bronzeGrad);
        g.fillRect(juce::Rectangle<float>(trkX, thumbY, kTrackWidth, trkB - thumbY));
        g.restoreState();

        // ── Outer BLACK gradient handle (40 px diameter) ──────────────────────
        // Radial highlight in the upper-left, falling off to black at the rim.
        juce::ColourGradient outerGrad(juce::Colour(0xFF6A6A6A),
                                       cx - kOuterRadius * 0.4f,
                                       thumbY - kOuterRadius * 0.4f,
                                       juce::Colour(0xFF000000),
                                       cx + kOuterRadius,
                                       thumbY + kOuterRadius, true);
        outerGrad.addColour(0.45, juce::Colour(0xFF2E2E2E));
        outerGrad.addColour(0.85, juce::Colour(0xFF0A0A0A));
        g.setGradientFill(outerGrad);
        g.fillEllipse(cx - kOuterRadius, thumbY - kOuterRadius,
                      kOuterRadius * 2.0f, kOuterRadius * 2.0f);

        // Subtle bevel rim + dark inset
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawEllipse(cx - kOuterRadius + 0.6f, thumbY - kOuterRadius + 0.6f,
                      kOuterRadius * 2.0f - 1.2f, kOuterRadius * 2.0f - 1.2f, 0.9f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.drawEllipse(cx - kOuterRadius + 0.2f, thumbY - kOuterRadius + 0.2f,
                      kOuterRadius * 2.0f - 0.4f, kOuterRadius * 2.0f - 0.4f, 0.8f);

        // ── Inner BRONZE circle (the "dot") ───────────────────────────────────
        juce::ColourGradient innerGrad(juce::Colour(0xFFF8C078),
                                       cx - kInnerRadius * 0.35f,
                                       thumbY - kInnerRadius * 0.35f,
                                       juce::Colour(0xFF7A4A1E),
                                       cx + kInnerRadius,
                                       thumbY + kInnerRadius, true);
        innerGrad.addColour(0.5, juce::Colour(0xFFC4843D));
        g.setGradientFill(innerGrad);
        g.fillEllipse(cx - kInnerRadius, thumbY - kInnerRadius,
                      kInnerRadius * 2.0f, kInnerRadius * 2.0f);

        // Thin dark rim around the bronze dot so it pops against the black handle
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawEllipse(cx - kInnerRadius + 0.2f, thumbY - kInnerRadius + 0.2f,
                      kInnerRadius * 2.0f - 0.4f, kInnerRadius * 2.0f - 0.4f, 0.7f);

        // ── Value text (top) ─────────────────────────────────────────────────
        g.setFont(juce::Font(kValueFont));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawFittedText(formatVal(), 0, 2, (int) W, (int)(kValueAreaH - 2),
                         juce::Justification::centred, 1);

        // ── Label (bottom) ───────────────────────────────────────────────────
        g.setFont(juce::Font(kLabelFont, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawFittedText(label, 0, (int)(H - kLabelAreaH), (int) W, (int) kLabelAreaH,
                         juce::Justification::centred, 1);
    }

    // ── Horizontal mode ──────────────────────────────────────────────────────
    // Mirrors paintVertical: same value-top / label-bottom bands, same handle
    // sizes, same bronze + black palette.  Only difference is that the track
    // runs LEFT to RIGHT through the middle of the component, the bronze fill
    // grows from the left edge toward the handle, and the U-channel gradient
    // runs perpendicular (top→middle→bottom) so the recessed cross-section
    // reads the same way.
    void paintHorizontal(juce::Graphics& g)
    {
        const float W = (float) getWidth();
        const float H = (float) getHeight();
        if (W < 8.0f || H < 4.0f) return;

        // The middle band sits between the value text (top) and label (bottom).
        const float cy   = kValueAreaH + kOuterRadius + kHandleClearance;
        const float trkY = cy - kTrackWidth * 0.5f;
        const float trkL = kOuterRadius + kHandleClearance;
        const float trkR = W - kOuterRadius - kHandleClearance;
        const float trkW = juce::jmax(8.0f, trkR - trkL);

        // Background card
        {
            const float inset  = 2.0f;
            const float corner = 6.0f;
            juce::Rectangle<float> bg(inset, inset, W - inset * 2.0f, H - inset * 2.0f);

            juce::ColourGradient bgGrad(juce::Colour(0xFF454545), bg.getX(), bg.getY(),
                                        juce::Colour(0xFF323232), bg.getX(), bg.getBottom(), false);
            g.setGradientFill(bgGrad);
            g.fillRoundedRectangle(bg, corner);

            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawRoundedRectangle(bg, corner, 1.0f);
        }

        // Recessed "valley" track — gradient runs vertically across the track
        // (light at top/bottom edges, dark at centre) for the same U-channel
        // read as the vertical case.
        juce::ColourGradient trackGrad(juce::Colour(0xFF2E2E2E), 0.0f, trkY - 1.0f,
                                       juce::Colour(0xFF2E2E2E), 0.0f, trkY + kTrackWidth + 1.0f, false);
        trackGrad.addColour(0.5, juce::Colour(0xFF020202));
        g.setGradientFill(trackGrad);
        g.fillRoundedRectangle(trkL, trkY, trkW, kTrackWidth, kTrackWidth * 0.5f);

        g.setColour(juce::Colour(0xFF000000).withAlpha(0.85f));
        g.drawRoundedRectangle(trkL + 0.5f, trkY + 0.5f,
                               trkW - 1.0f, kTrackWidth - 1.0f, kTrackWidth * 0.5f - 0.5f, 0.8f);

        // Bronze fill from LEFT edge to handle x
        const float norm   = (maxVal > minVal) ? (value - minVal) / (maxVal - minVal) : 0.0f;
        const float thumbX = trkL + norm * trkW;

        juce::Path pillPath;
        pillPath.addRoundedRectangle(trkL, trkY, trkW, kTrackWidth, kTrackWidth * 0.5f);
        g.saveState();
        g.reduceClipRegion(pillPath);

        juce::ColourGradient bronzeGrad(juce::Colour(0xFFF0B265), 0.0f, trkY - 1.0f,
                                        juce::Colour(0xFF6E4419), 0.0f, trkY + kTrackWidth + 1.0f, false);
        bronzeGrad.addColour(0.5, juce::Colour(0xFFB87A36));
        g.setGradientFill(bronzeGrad);
        g.fillRect(juce::Rectangle<float>(trkL, trkY, thumbX - trkL, kTrackWidth));
        g.restoreState();

        // Outer BLACK handle centred at (thumbX, cy)
        juce::ColourGradient outerGrad(juce::Colour(0xFF6A6A6A),
                                       thumbX - kOuterRadius * 0.4f,
                                       cy     - kOuterRadius * 0.4f,
                                       juce::Colour(0xFF000000),
                                       thumbX + kOuterRadius,
                                       cy     + kOuterRadius, true);
        outerGrad.addColour(0.45, juce::Colour(0xFF2E2E2E));
        outerGrad.addColour(0.85, juce::Colour(0xFF0A0A0A));
        g.setGradientFill(outerGrad);
        g.fillEllipse(thumbX - kOuterRadius, cy - kOuterRadius,
                      kOuterRadius * 2.0f, kOuterRadius * 2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawEllipse(thumbX - kOuterRadius + 0.6f, cy - kOuterRadius + 0.6f,
                      kOuterRadius * 2.0f - 1.2f, kOuterRadius * 2.0f - 1.2f, 0.9f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.drawEllipse(thumbX - kOuterRadius + 0.2f, cy - kOuterRadius + 0.2f,
                      kOuterRadius * 2.0f - 0.4f, kOuterRadius * 2.0f - 0.4f, 0.8f);

        // Inner BRONZE dot
        juce::ColourGradient innerGrad(juce::Colour(0xFFF8C078),
                                       thumbX - kInnerRadius * 0.35f,
                                       cy     - kInnerRadius * 0.35f,
                                       juce::Colour(0xFF7A4A1E),
                                       thumbX + kInnerRadius,
                                       cy     + kInnerRadius, true);
        innerGrad.addColour(0.5, juce::Colour(0xFFC4843D));
        g.setGradientFill(innerGrad);
        g.fillEllipse(thumbX - kInnerRadius, cy - kInnerRadius,
                      kInnerRadius * 2.0f, kInnerRadius * 2.0f);

        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawEllipse(thumbX - kInnerRadius + 0.2f, cy - kInnerRadius + 0.2f,
                      kInnerRadius * 2.0f - 0.4f, kInnerRadius * 2.0f - 0.4f, 0.7f);

        // Value text (top), label (bottom) — same bands as the vertical layout.
        g.setFont(juce::Font(kValueFont));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawFittedText(formatVal(), 0, 2, (int) W, (int)(kValueAreaH - 2),
                         juce::Justification::centred, 1);

        g.setFont(juce::Font(kLabelFont, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawFittedText(label, 0, (int)(H - kLabelAreaH), (int) W, (int) kLabelAreaH,
                         juce::Justification::centred, 1);
    }

    juce::String formatVal() const
    {
        // Integer-valued sliders (step == 1) display without decimals — used
        // for the 0..100 normalised sliders in DrumsPopup.
        if (step >= 1.0f && unit.isEmpty())
            return juce::String((int) std::round(value));

        if (unit == "s")   return value < 1.0f ? juce::String((int)(value*1000)) + "ms"
                                               : juce::String(value, 1) + "s";
        if (unit == "ms")  return juce::String((int)value) + "ms";
        if (unit == "Hz")  return juce::String(value, 2) + "Hz";
        if (unit == "dB")  return juce::String(value, 1) + "dB";
        if (unit == "st")  return juce::String(value, 1) + "st";
        if (unit == "BPM") return juce::String((int)value) + " BPM";
        return juce::String(value, 2) + unit;
    }

    float quantise(float v) const
    {
        if (step <= 0.0f) return v;
        const float n = std::round((v - minVal) / step);
        return juce::jlimit(minVal, maxVal, minVal + n * step);
    }

    juce::String label, unit;
    float minVal, maxVal, value, defVal;
    float step = 0.0f;     // 0 = continuous
    bool  horizontal = false;
    int   dragX = 0;
    int   dragY = 0;
    float dragVal = 0.f;
};

//==============================================================================
// BandFilterSlider — VERTICAL two-thumb low-cut / high-cut band control.
//
// Frequency runs bottom (20 Hz) to top (20 kHz), log-mapped with 20 * 1000^norm
// (the same law the engine uses for its cutoff), so each octave gets equal
// travel.  The BOTTOM thumb is the LOW-CUT (high-pass edge, BLUE); the TOP thumb
// is the HIGH-CUT (low-pass edge, RED).  The bronze fill between them is the
// pass-band.  The thumbs cannot cross (low <= high) but MAY meet — a zero-width
// band is silence, by design.  Each thumb prints its own cut frequency inside a
// rounded-rectangular handle ("20hz", "2.5khz").  Reuses GoldSlider's recessed
// bronze track look.  onChange fires with both normalised edges (low, high).
//==============================================================================
class BandFilterSlider : public juce::Component
{
public:
    static constexpr float kTrackWidth  = 10.0f;
    static constexpr float kHandleW     = 54.0f;
    static constexpr float kHandleH     = 22.0f;
    static constexpr float kHandleRad   = 6.0f;
    static constexpr float kLabelAreaH  = 24.0f;
    static constexpr float kTopPad      = kHandleH * 0.5f + 4.0f;
    static constexpr float kBotPad      = kLabelAreaH + kHandleH * 0.5f + 4.0f;
    static constexpr float kValueFont   = 13.0f;
    static constexpr float kLabelFont   = 16.0f;

    // Fired with the two normalised cut positions (0..1 each, low <= high).
    std::function<void(float loNorm, float hiNorm)> onChange;

    explicit BandFilterSlider(const juce::String& lbl) : label(lbl) {}

    // ── Frequency mapping (shared law with the engine: 20 * 1000^norm) ─────────
    static float normToHz(float n)  { return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, n)); }
    static float hzToNorm(float hz) { return juce::jlimit(0.0f, 1.0f,
                                          std::log(juce::jmax(20.0f, hz) / 20.0f) / std::log(1000.0f)); }

    /** Set both edges.  Order is enforced (low <= high); they may be equal. */
    void setValues(float loN, float hiN, bool notify = false)
    {
        loN = juce::jlimit(0.0f, 1.0f, loN);
        hiN = juce::jlimit(0.0f, 1.0f, hiN);
        if (loN > hiN) { const float t = loN; loN = hiN; hiN = t; }
        if (loN == loNorm && hiN == hiNorm && ! notify) return;
        loNorm = loN; hiNorm = hiN;
        repaint();
        if (notify && onChange) onChange(loNorm, hiNorm);
    }
    float getLo() const { return loNorm; }
    float getHi() const { return hiNorm; }

    void paint(juce::Graphics& g) override
    {
        const float W = (float) getWidth(), H = (float) getHeight();
        if (W < 8.0f || H < 24.0f) return;

        const float cx   = W * 0.5f;
        const float trkX = cx - kTrackWidth * 0.5f;
        const float trkT = kTopPad;
        const float trkB = H - kBotPad;
        const float trkH = juce::jmax(8.0f, trkB - trkT);

        // ── Backdrop card (matches GoldSlider) ────────────────────────────────
        {
            const float inset = 2.0f, corner = 6.0f;
            juce::Rectangle<float> bg(inset, inset, W - inset * 2.0f, H - inset * 2.0f);
            juce::ColourGradient bgGrad(juce::Colour(0xFF454545), bg.getX(), bg.getY(),
                                        juce::Colour(0xFF323232), bg.getX(), bg.getBottom(), false);
            g.setGradientFill(bgGrad);
            g.fillRoundedRectangle(bg, corner);
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawRoundedRectangle(bg, corner, 1.0f);
        }

        // ── Recessed valley track (matches GoldSlider) ────────────────────────
        juce::ColourGradient trackGrad(juce::Colour(0xFF2E2E2E), trkX - 1.0f, 0.0f,
                                       juce::Colour(0xFF2E2E2E), trkX + kTrackWidth + 1.0f, 0.0f, false);
        trackGrad.addColour(0.5, juce::Colour(0xFF020202));
        g.setGradientFill(trackGrad);
        g.fillRoundedRectangle(trkX, trkT, kTrackWidth, trkH, kTrackWidth * 0.5f);
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.85f));
        g.drawRoundedRectangle(trkX + 0.5f, trkT + 0.5f,
                               kTrackWidth - 1.0f, trkH - 1.0f, kTrackWidth * 0.5f - 0.5f, 0.8f);

        // norm 0 -> bottom, norm 1 -> top.  yHi is above yLo (high >= low).
        const float yLo = trkB - loNorm * trkH;   // low-cut  (blue, bottom)
        const float yHi = trkB - hiNorm * trkH;   // high-cut (red, top)

        // ── Bronze pass-band between the thumbs ───────────────────────────────
        {
            juce::Path clip;
            clip.addRoundedRectangle(trkX, trkT, kTrackWidth, trkH, kTrackWidth * 0.5f);
            g.saveState();
            g.reduceClipRegion(clip);
            juce::ColourGradient bronze(juce::Colour(0xFFF0B265), trkX - 1.0f, 0.0f,
                                        juce::Colour(0xFF6E4419), trkX + kTrackWidth + 1.0f, 0.0f, false);
            bronze.addColour(0.5, juce::Colour(0xFFB87A36));
            g.setGradientFill(bronze);
            g.fillRect(juce::Rectangle<float>(trkX, yHi, kTrackWidth, juce::jmax(0.0f, yLo - yHi)));
            g.restoreState();
        }

        // ── Handles: bottom = blue (low cut), top = red (high cut) ────────────
        drawHandle(g, cx, yLo, juce::Colour(0xFF2F78E6), juce::Colour(0xFF0B3F86), freqText(loNorm));
        drawHandle(g, cx, yHi, juce::Colour(0xFFE24B4A), juce::Colour(0xFF791F1F), freqText(hiNorm));

        // ── Bottom label ──────────────────────────────────────────────────────
        g.setFont(juce::Font(kLabelFont, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawFittedText(label, 0, (int) (H - kLabelAreaH), (int) W, (int) kLabelAreaH,
                         juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        activeThumb   = pickThumb((float) e.y);
        dragStartY    = (float) e.y;
        dragStartNorm = (activeThumb == 1) ? loNorm : hiNorm;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;      // right button = reset only
        if (activeThumb == 0) return;
        const float delta = (dragStartY - (float) e.y) / trackH();   // drag up = increase
        const float n     = juce::jlimit(0.0f, 1.0f, dragStartNorm + delta);
        if (activeThumb == 1) setValues(juce::jmin(n, hiNorm), hiNorm, true);   // low, capped at high
        else                  setValues(loNorm, juce::jmax(n, loNorm), true);   // high, floored at low
    }
    void mouseUp(const juce::MouseEvent&) override
    { activeThumb = 0; setMouseCursor(juce::MouseCursor::NormalCursor); }

    // RIGHT double-click anywhere resets to fully open (20 Hz .. 20 kHz =
    // transparent).  Same relocation as GoldSlider, so one rule covers every
    // control in the editor rather than two that differ by widget.
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) setValues(0.0f, 1.0f, true);
    }

private:
    float trackTop() const { return kTopPad; }
    float trackBot() const { return (float) getHeight() - kBotPad; }
    float trackH()   const { return juce::jmax(8.0f, trackBot() - trackTop()); }
    float normToY(float n) const { return trackBot() - n * trackH(); }

    // 1 = low-cut thumb, 2 = high-cut thumb.  Nearest to the click; when the two
    // coincide, pick by which side of them the click lands (below -> low).
    int pickThumb(float y) const
    {
        const float yLo = normToY(loNorm), yHi = normToY(hiNorm);
        if (std::abs(loNorm - hiNorm) < 1.0e-4f) return (y >= yLo) ? 1 : 2;
        return (std::abs(y - yLo) <= std::abs(y - yHi)) ? 1 : 2;
    }

    void drawHandle(juce::Graphics& g, float cx, float cy,
                    juce::Colour fill, juce::Colour edge, const juce::String& txt)
    {
        const float w = juce::jmin(kHandleW, (float) getWidth() - 6.0f);
        juce::Rectangle<float> r(cx - w * 0.5f, cy - kHandleH * 0.5f, w, kHandleH);
        g.setColour(fill);
        g.fillRoundedRectangle(r, kHandleRad);
        g.setColour(juce::Colours::white.withAlpha(0.22f));           // top bevel
        g.drawRoundedRectangle(r.reduced(1.2f), kHandleRad - 1.0f, 0.8f);
        g.setColour(edge);                                            // dark rim
        g.drawRoundedRectangle(r, kHandleRad, 1.5f);
        g.setFont(juce::Font(kValueFont, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawFittedText(txt, r.toNearestInt(), juce::Justification::centred, 1);
    }

    static juce::String freqText(float norm)
    {
        const float hz = normToHz(norm);
        if (hz < 1000.0f) return juce::String(juce::roundToInt(hz)) + "hz";
        const float k = hz / 1000.0f;
        return (k < 10.0f ? juce::String(k, 1) : juce::String(juce::roundToInt(k))) + "khz";
    }

    juce::String label;
    float loNorm = 0.0f;    // low cut  (20 Hz  = fully open low)
    float hiNorm = 1.0f;    // high cut (20 kHz = fully open high)
    int   activeThumb = 0;  // 0 none, 1 low, 2 high
    float dragStartY = 0.0f, dragStartNorm = 0.0f;
};

// ── FreqLabel (double-click to type Hz, drag to scrub) ───────────────────────────────
class FreqLabel : public juce::Label
{
public:
    std::function<void(float)> onFreqChanged;

    explicit FreqLabel(float initHz = 1000.f) : hz(initHz)
    {
        setEditable(false, true);
        setJustificationType(juce::Justification::centred);
        setFont(juce::Font(11.0f, juce::Font::bold));
        setColour(juce::Label::textColourId, juce::Colour(0xFFFFCC44));
        setColour(juce::Label::backgroundColourId, juce::Colour(0xFF1A1A1A));
        setColour(juce::Label::textWhenEditingColourId, juce::Colours::white);
        setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xFF333300));
        updateText();
    }

    void setHz(float v) { hz = juce::jlimit(20.f, 20000.f, v); updateText(); }
    float getHz() const { return hz; }

    void textWasEdited() override
    {
        const float v = parseHz(getText());
        hz = (v >= 20.f && v <= 20000.f) ? v : hz;
        updateText();
        if (onFreqChanged) onFreqChanged(hz);
    }

    void mouseDown(const juce::MouseEvent& e) override
    { dragY = e.y; dragHz = hz; juce::Label::mouseDown(e); }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float logMin = std::log10(20.f), logMax = std::log10(20000.f);
        const float dy = (float)(dragY - e.y) / 80.0f;
        const float logHz = juce::jlimit(logMin, logMax,
                                         std::log10(juce::jmax(20.f, dragHz)) + dy * (logMax - logMin));
        hz = std::pow(10.f, logHz);
        updateText();
        if (onFreqChanged) onFreqChanged(hz);
    }

private:
    void updateText() { setText(formatHz(hz), juce::dontSendNotification); }

    static juce::String formatHz(float v)
    { return v >= 1000.f ? juce::String(v / 1000.f, 1) + "k" : juce::String((int)v) + "Hz"; }
    static float parseHz(const juce::String& s)
    {
        auto t = s.trim().toLowerCase();
        const bool kilo = t.containsChar('k');
        const float v = t.retainCharacters("0123456789.").getFloatValue();
        return kilo ? v * 1000.f : v;
    }

    float hz, dragHz = 1000.f;
    int dragY = 0;
};

// ── Section-style helpers ─────────────────────────────────────────────────────────────
namespace InstrEditStyle
{
    //==========================================================================
    // PANEL LABEL COLOUR — #C2C2C2, and the ONE place it is decided.
    //
    // The left panel's captions and separator lines used to be pure #FFFFFF,
    // while the two macro triggers beside them rendered at #C2C2C2 (a plain
    // TextButton goes through the LookAndFeel, which does not draw at full
    // white).  Measured off a screenshot, that is a 61-unit gap on every
    // channel — enough that the panel read as two different families of control
    // sitting in one frame.
    //
    // Everything on that panel now uses this.  Pure white stays where it earns
    // its contrast: the amber-lit states, the LED speculars, and the tab row.
    //==========================================================================
    inline const juce::Colour kPanelLabel { 0xFFC2C2C2 };

    constexpr float kFrameCorner    = 20.0f;
    constexpr int   kFrameSidePad   = 14;
    constexpr int   kFrameTopPad    = 8;
    constexpr int   kFrameTitleH    = 22;
    constexpr int   kFrameTitleGap  = 6;
    constexpr int   kFrameBottomPad = 14;

    inline void paintComponentFrame(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const juce::String& title)
    {
        const auto fb = bounds.toFloat();

        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle(fb, kFrameCorner);

        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(fb.reduced(0.5f), kFrameCorner, 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour(juce::Colour(0xFFCC6600));
            g.setFont(juce::Font(15.0f, juce::Font::bold));
            g.drawFittedText(title,
                             bounds.getX() + 12, bounds.getY() + kFrameTopPad,
                             bounds.getWidth() - 24, kFrameTitleH,
                             juce::Justification::centred, 1);
        }
    }

    inline juce::Rectangle<int> componentFrameContent(juce::Rectangle<int> bounds)
    {
        const int top = kFrameTopPad + kFrameTitleH + kFrameTitleGap;
        return { bounds.getX() + kFrameSidePad,
                 bounds.getY() + top,
                 bounds.getWidth()  - kFrameSidePad * 2,
                 bounds.getHeight() - top - kFrameBottomPad };
    }

    inline void styleSquareButton(juce::TextButton& b, bool active)
    {
        const auto fill = active ? juce::Colour(0xFFCC6600) : juce::Colour(0xFF2A2A2A);

        // BOTH ids, and that is the whole fix for "my toggles never go orange".
        // LookAndFeel_V4::drawButtonBackground looks up buttonOnColourId when the
        // button's TOGGLE STATE is set and buttonColourId when it is not — so a
        // plain button styled here went amber correctly, while any button with
        // setClickingTogglesState(true) fell through to the untouched default the
        // moment it was switched on.
        b.setColour(juce::TextButton::buttonColourId,   fill);
        b.setColour(juce::TextButton::buttonOnColourId, fill);
        // Black on the amber fill, white on the inert one - the label has to
        // read against whichever background it just got.
        const auto text = active ? juce::Colours::black : juce::Colours::white;
        b.setColour(juce::TextButton::textColourOffId, text);
        b.setColour(juce::TextButton::textColourOnId,  text);
    }

    inline void drawSectionHeader(juce::Graphics& g, const juce::String& label,
                                  int x, int y, int w, int h = 14)
    {
        g.setColour(juce::Colour(0xFFCC6600));
        g.setFont(juce::Font((float) juce::jmin(h - 2, 11), juce::Font::bold));
        g.drawText(label, x, y, w, h, juce::Justification::centred, false);
    }
    inline void drawDivider(juce::Graphics& g, int x, int yTop, int yBot)
    {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawVerticalLine(x, (float) yTop, (float) yBot);
    }
}

//==============================================================================
//  PianoStrip — the keyboard along the bottom of the sound editors.
//
//  Shared by InstrEditorWindow (melodic slots) and DrumsPopup (drum/perc slots),
//  which is why it lives here rather than inside either one.
//
//  Range is MIDI 24 (C1) .. 96 (C7).  It starts at 24 on purpose: the drum
//  editor's own key-select keyboard begins at 35, so notes 24..34 are invisible
//  there — and that gap is exactly where a style's lowest drum / percussion
//  hits land.
//
//  Two jobs at once:
//    * PLAY  — click or drag sounds the slot being edited (a real held note, so
//              melodic voices sustain until the key is released; drag = gliss).
//    * WATCH — every note the channel is playing lights up RED, polled at 30 Hz.
//              On a style slot that's what the STYLE is playing; on a solo slot
//              it's the incoming MIDI.
//
//  The host's mask reports KEY-DOWN notes (voices in release are excluded, or a
//  long-release patch would leave keys lit for seconds).  Because a drum hit can
//  be shorter than one 30 Hz poll, each note is LATCHED lit for ~100 ms once
//  seen — otherwise fast hits would flicker or be missed entirely.
//==============================================================================
class PianoStrip : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kFirstNote  = 24;   // C1
    static constexpr int kLastNote   = 96;   // C7
    static constexpr int kLatchTicks = 2;    // ~66 ms at 30 Hz (anti-flicker only;
                                             // the ENGINE latches the trigger so a
                                             // sub-frame hit can never be missed)

    // Velocity for a strip key press (mouse has no velocity of its own).  100 =
    // a firm hit, so auditioning a drum or note in the editor is clearly
    // audible.  The drum velocity->gain law is LINEAR at unity peak (see
    // Channel.cpp), so this previews at 100/127 ~ 0.79 of full -- loud and
    // representative, while a soft style hit still tapers down proportionally.
    static constexpr int kPreviewVelocity = 100;

    std::function<void(int note, int velocity)> onNoteOn;
    std::function<void(int note)>               onNoteOff;
    /** Host fills a 4 x uint32 (128-bit) mask of the notes currently held on
        the channel being edited. */
    std::function<void(uint32_t*)>              onQuerySounding;

    PianoStrip()
    {
        setOpaque(true);
        startTimerHz(30);
    }

    ~PianoStrip() override { releaseHeld(); }

    void resized() override { rebuildKeys(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF0E0E0E));

        const juce::Colour litWhite(0xFFE02020);   // playing — white key
        const juce::Colour litBlack(0xFFB01818);   // playing — black key

        for (const auto& k : whiteKeys)
        {
            g.setColour(isLit(k.note) ? litWhite : juce::Colour(0xFFE8E8E8));
            g.fillRect(k.r.reduced(0.5f, 0.0f));
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawRect(k.r, 1.0f);
        }

        // Black keys sit on top of the whites, so they're drawn second.
        for (const auto& k : blackKeys)
        {
            g.setColour(isLit(k.note) ? litBlack : juce::Colour(0xFF141414));
            g.fillRect(k.r);
            g.setColour(juce::Colours::black);
            g.drawRect(k.r, 1.0f);
        }

        g.setFont(juce::Font(9.0f));
        for (const auto& k : whiteKeys)
        {
            if (k.note % 12 != 0) continue;              // label every C
            g.setColour(isLit(k.note) ? juce::Colours::white
                                      : juce::Colours::black.withAlpha(0.60f));
            g.drawText("C" + juce::String(k.note / 12 - 1),
                       k.r.toNearestInt().removeFromBottom(13),
                       juce::Justification::centred, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override { playAt(e.position); }
    void mouseDrag(const juce::MouseEvent& e) override { playAt(e.position); }
    void mouseUp  (const juce::MouseEvent&)   override { releaseHeld(); }
    void mouseExit(const juce::MouseEvent&)   override { releaseHeld(); }

private:
    struct Key { int note = -1; juce::Rectangle<float> r; };

    static bool isBlackKey(int midi) noexcept
    {
        const int pc = ((midi % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    bool isLit(int n) const noexcept
    {
        if (n < kFirstNote || n > kLastNote) return false;
        return latch[(size_t) n] > 0;
    }

    void rebuildKeys()
    {
        whiteKeys.clear();
        blackKeys.clear();

        const float W = (float) getWidth();
        const float H = (float) getHeight();
        if (W <= 1.0f || H <= 1.0f) return;

        int numWhite = 0;
        for (int n = kFirstNote; n <= kLastNote; ++n)
            if (! isBlackKey(n)) ++numWhite;
        if (numWhite <= 0) return;

        const float ww = W / (float) numWhite;   // white key width
        const float bw = ww * 0.62f;             // black key width
        const float bh = H  * 0.62f;             // black key height ("short")

        int wi = 0;
        for (int n = kFirstNote; n <= kLastNote; ++n)
        {
            if (! isBlackKey(n))
            {
                whiteKeys.push_back({ n, { (float) wi * ww, 0.0f, ww, H } });
                ++wi;
            }
            else
            {
                // A black key straddles the seam between the white key just
                // placed and the next one — that seam sits at wi * ww.
                blackKeys.push_back({ n, { (float) wi * ww - bw * 0.5f, 0.0f, bw, bh } });
            }
        }
    }

    int noteAt(juce::Point<float> p) const
    {
        // Black keys are drawn on top, so they must be hit-tested first.
        for (const auto& k : blackKeys) if (k.r.contains(p)) return k.note;
        for (const auto& k : whiteKeys) if (k.r.contains(p)) return k.note;
        return -1;
    }

    void playAt(juce::Point<float> p)
    {
        const int n = noteAt(p);
        if (n == heldNote) return;        // still inside the same key
        releaseHeld();                    // gliss: let the previous one go
        if (n >= 0)
        {
            heldNote = n;
            if (onNoteOn) onNoteOn(n, kPreviewVelocity);
        }
    }

    void releaseHeld()
    {
        if (heldNote >= 0 && onNoteOff) onNoteOff(heldNote);
        heldNote = -1;
    }

    void timerCallback() override
    {
        uint32_t m[4] = { 0u, 0u, 0u, 0u };
        if (onQuerySounding) onQuerySounding(m);

        bool changed = false;
        for (int n = kFirstNote; n <= kLastNote; ++n)
        {
            const bool on = (m[(size_t) (n >> 5)] & (1u << (n & 31))) != 0u;
            auto& t = latch[(size_t) n];
            const bool wasLit = (t > 0);

            if      (on)    t = kLatchTicks;   // (re)arm — holds a short hit visible
            else if (t > 0) --t;               // decay

            if ((t > 0) != wasLit) changed = true;
        }

        if (changed) repaint();                // only when something actually moved
    }

    std::vector<Key> whiteKeys, blackKeys;
    int              latch[128] {};            // per-note lit countdown, in ticks
    int              heldNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoStrip)
};



