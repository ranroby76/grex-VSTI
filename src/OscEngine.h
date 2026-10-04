#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

//==============================================================================
//  OscEngine.h  -  GREX'S BUILT-IN SYNTH VOICE (the SOUND ENGINE tab).
//
//  The two-oscillator engine of Bambino / Fanan Synth, brought into Grex for
//  the programs no sample set does justice to: GM 38 SYNTH BASS 1 and GM 39
//  SYNTH BASS 2.
//
//      OSC 1 --+-- mix (levels, ring) --+-- sub, noise --> DRIVE --> LADDER --> DC block --> out
//      OSC 2 --+  (FM into osc 1, hard sync to osc 1)                (4-pole, own envelope)
//
//  It is the SOUND SOURCE of a voice: the voice's own chain - the amp envelope,
//  the window filter, glide, mono / portamento, the LFOs, sends and inserts -
//  shapes what comes out of it exactly as it shapes a sample.  So nothing a
//  sample gets is lost, and the tab only holds what a sample cannot have.
//
//  Pure C++, no JUCE: the audio thread renders it, the editor edits it and the
//  test harness proves it, all from the same code.  No allocation anywhere on
//  the render path.
//==============================================================================
namespace oscengine
{
    enum Wave { Saw = 0, Pulse, Triangle, Sine, kNumWaves };

    inline const char* waveName (int w)
    {
        static const char* names[kNumWaves] = { "SAW", "PULSE", "TRI", "SINE" };
        return (w >= 0 && w < kNumWaves) ? names[w] : names[0];
    }

    /** The programs the engine plays instead of samples. */
    inline bool servesProgram (int gmProgram) noexcept { return gmProgram == 38 || gmProgram == 39; }
    inline int  programSlot   (int gmProgram) noexcept { return gmProgram == 39 ? 1 : 0; }

    //==========================================================================
    //  THE SETTINGS.  Normalised 0..1 unless noted; the editor shows them 0..100.
    //==========================================================================
    struct Params
    {
        int   program = 38;                     // the GM program these belong to

        int   wave [2] = { Saw, Pulse };
        int   oct  [2] = { 0, -1 };             // -2 .. +2 octaves
        int   semi [2] = { 0, 0 };              // -12 .. +12
        float fine [2] = { 0.0f, 0.0f };        // cents, -50 .. +50
        float pw   [2] = { 0.5f, 0.5f };        // 0.05 .. 0.95 (PULSE only)
        float level[2] = { 1.0f, 0.6f };

        float fm    = 0.0f;                     // osc 2 -> osc 1 frequency modulation
        float ring  = 0.0f;                     // 0 = plain mix, 1 = pure ring (osc1 x osc2)
        bool  sync  = false;                    // osc 2 hard-synced to osc 1
        float sub   = 0.0f;                     // square one octave below osc 1
        float noise = 0.0f;
        float drive = 0.2f;                     // into the ladder

        float cutoff   = 0.40f;                 // 20 Hz .. 18 kHz, exponential
        float reso     = 0.20f;                 // self-oscillation near 1
        float keyTrack = 0.50f;
        float envAmt   = 0.40f;                 // -1 .. +1
        float velAmt   = 0.30f;                 // velocity -> cutoff
        float envA = 0.00f, envD = 0.30f, envS = 0.00f, envR = 0.20f;

        float outLevel = 0.8f;                  // engine output
    };

    //==========================================================================
    //  FACTORY SOUNDS.  What a set with no edits plays.
    //==========================================================================
    inline Params factory (int gmProgram)
    {
        Params p;
        if (gmProgram == 39)
        {
            // SYNTH BASS 2 - punchy and resonant: a narrow pulse over a slightly
            // detuned saw, a sub underneath, a short squelchy filter snap.
            p.program  = 39;
            p.wave[0]  = Pulse;  p.pw[0] = 0.35f;  p.level[0] = 1.0f;
            p.wave[1]  = Saw;    p.oct[1] = 0;     p.fine[1] = 7.0f;  p.level[1] = 0.70f;
            p.sub      = 0.35f;
            p.drive    = 0.35f;
            p.cutoff   = 0.30f;  p.reso = 0.45f;  p.keyTrack = 0.50f;
            p.envAmt   = 0.55f;  p.velAmt = 0.45f;
            p.envA = 0.00f;  p.envD = 0.22f;  p.envS = 0.00f;  p.envR = 0.15f;
            p.outLevel = 0.80f;
        }
        else
        {
            // SYNTH BASS 1 - the classic analog bass: a saw with a square an
            // octave below, warm drive, a round filter pluck.
            p.program  = 38;
            p.wave[0]  = Saw;    p.level[0] = 1.0f;
            p.wave[1]  = Pulse;  p.oct[1] = -1;  p.fine[1] = -4.0f;  p.level[1] = 0.55f;
            p.drive    = 0.25f;
            p.cutoff   = 0.36f;  p.reso = 0.18f;  p.keyTrack = 0.50f;
            p.envAmt   = 0.42f;  p.velAmt = 0.30f;
            p.envA = 0.00f;  p.envD = 0.32f;  p.envS = 0.10f;  p.envR = 0.20f;
            p.outLevel = 0.80f;
        }
        return p;
    }

    //==========================================================================
    //  TEXT FORM, for sets and .ins files:  "OSCENG 1|p=38|w1=0|...".
    //  INTEGERS ONLY (x1000), like the EDM kit files: a set written under one
    //  number locale reads back identically under any other.  Missing keys
    //  keep the program's factory value, unknown keys are ignored - so a set
    //  from a later build still opens here.
    //==========================================================================
    inline std::string toString (const Params& p)
    {
        auto q = [] (float v) { return std::to_string ((long) std::lround (v * 1000.0f)); };
        std::string s = "OSCENG 1|p=" + std::to_string (p.program);
        for (int i = 0; i < 2; ++i)
        {
            const std::string n = std::to_string (i + 1);
            s += "|w"  + n + "=" + std::to_string (p.wave[i]);
            s += "|o"  + n + "=" + std::to_string (p.oct[i]);
            s += "|s"  + n + "=" + std::to_string (p.semi[i]);
            s += "|f"  + n + "=" + q (p.fine[i]);
            s += "|pw" + n + "=" + q (p.pw[i]);
            s += "|l"  + n + "=" + q (p.level[i]);
        }
        s += "|fm="   + q (p.fm)    + "|ring=" + q (p.ring) + "|sync=" + std::string (p.sync ? "1" : "0");
        s += "|sub="  + q (p.sub)   + "|nz="   + q (p.noise) + "|drv=" + q (p.drive);
        s += "|cut="  + q (p.cutoff) + "|res=" + q (p.reso) + "|kt="  + q (p.keyTrack);
        s += "|env="  + q (p.envAmt) + "|vel=" + q (p.velAmt);
        s += "|ea="   + q (p.envA) + "|ed=" + q (p.envD) + "|es=" + q (p.envS) + "|er=" + q (p.envR);
        s += "|lvl="  + q (p.outLevel);
        return s;
    }

    /** The program a text form belongs to, or -1 when it is not one. */
    inline int programOf (const std::string& text)
    {
        if (text.rfind ("OSCENG 1", 0) != 0) return -1;
        const auto at = text.find ("|p=");
        return at == std::string::npos ? -1 : std::atoi (text.c_str() + at + 3);
    }

    inline bool fromString (const std::string& text, Params& out)
    {
        const int program = programOf (text);
        if (program < 0) return false;
        Params p = factory (program);
        auto clamp01 = [] (float v) { return std::min (1.0f, std::max (0.0f, v)); };

        size_t i = text.find ('|');
        while (i != std::string::npos)
        {
            const size_t next = text.find ('|', i + 1);
            const std::string tok = text.substr (i + 1, next == std::string::npos ? std::string::npos : next - i - 1);
            i = next;
            const auto eq = tok.find ('=');
            if (eq == std::string::npos) continue;
            const std::string k = tok.substr (0, eq);
            const long  iv = std::atol (tok.c_str() + eq + 1);
            const float fv = (float) iv / 1000.0f;
            for (int o = 0; o < 2; ++o)
            {
                const std::string n = std::to_string (o + 1);
                if      (k == "w"  + n) p.wave [o] = (int) std::min (3L, std::max (0L, iv));
                else if (k == "o"  + n) p.oct  [o] = (int) std::min (2L, std::max (-2L, iv));
                else if (k == "s"  + n) p.semi [o] = (int) std::min (12L, std::max (-12L, iv));
                else if (k == "f"  + n) p.fine [o] = std::min (50.0f, std::max (-50.0f, fv));
                else if (k == "pw" + n) p.pw   [o] = std::min (0.95f, std::max (0.05f, fv));
                else if (k == "l"  + n) p.level[o] = clamp01 (fv);
            }
            if      (k == "fm")   p.fm       = clamp01 (fv);
            else if (k == "ring") p.ring     = clamp01 (fv);
            else if (k == "sync") p.sync     = iv != 0;
            else if (k == "sub")  p.sub      = clamp01 (fv);
            else if (k == "nz")   p.noise    = clamp01 (fv);
            else if (k == "drv")  p.drive    = clamp01 (fv);
            else if (k == "cut")  p.cutoff   = clamp01 (fv);
            else if (k == "res")  p.reso     = clamp01 (fv);
            else if (k == "kt")   p.keyTrack = clamp01 (fv);
            else if (k == "env")  p.envAmt   = std::min (1.0f, std::max (-1.0f, fv));
            else if (k == "vel")  p.velAmt   = clamp01 (fv);
            else if (k == "ea")   p.envA     = clamp01 (fv);
            else if (k == "ed")   p.envD     = clamp01 (fv);
            else if (k == "es")   p.envS     = clamp01 (fv);
            else if (k == "er")   p.envR     = clamp01 (fv);
            else if (k == "lvl")  p.outLevel = clamp01 (fv);
        }
        p.program = program;
        out = p;
        return true;
    }

    /** True when p sounds exactly like the factory sound of its program. */
    inline bool isFactory (const Params& p) { return toString (p) == toString (factory (p.program)); }

    //==========================================================================
    //  THE VOICE.  Plain data: lives inside Channel::Voice, reset per note.
    //==========================================================================
    struct Voice
    {
        double ph [2] = { 0.0, 0.0 };
        double phSub  = 0.0;
        float  o2     = 0.0f;                   // osc 2's last output (FM source)
        std::uint32_t rng = 0x9E3779B9u;
        float  s [4]  = { 0.0f, 0.0f, 0.0f, 0.0f };   // ladder stages
        float  g      = 0.1f;                   // ladder coefficient (control rate)
        int    ctl    = 0;
        float  dcX = 0.0f, dcY = 0.0f;
        float  velocity = 1.0f;

        // envelope
        int    envStage = 0;                    // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
        float  envValue = 0.0f;
        bool   released = false;

        // prepared once per block (prepareBlock)
        double ratio [2] = { 1.0, 1.0 };
        float  aCoef = 0.0f, dCoef = 0.0f, rCoef = 0.0f, sus = 0.0f, dcR = 0.997f;
    };

    inline float envTimeAttack (float x) { return 0.0005f * std::pow (6000.0f, x); }   // 0.5 ms .. 3 s
    inline float envTimeDecay  (float x) { return 0.002f  * std::pow (2500.0f, x); }   // 2 ms .. 5 s

    /** A new note.  keepPhases: the same voice carries on legato (mono glide) -
        the oscillators keep running so the note change does not click. */
    inline void noteOn (Voice& v, float velocity01, std::uint32_t seed, bool keepPhases) noexcept
    {
        if (! keepPhases)
        {
            v.rng   = seed ? seed : 0x9E3779B9u;
            v.ph[0] = 0.0;
            v.ph[1] = (double) (v.rng >> 8) / 16777216.0;   // osc 2 free-running: no phasey identical attacks
            v.phSub = 0.0;
            v.o2    = 0.0f;
            v.s[0] = v.s[1] = v.s[2] = v.s[3] = 0.0f;
            v.dcX = v.dcY = 0.0f;
            v.envValue = 0.0f;
        }
        v.velocity = std::min (1.0f, std::max (0.0f, velocity01));
        v.envStage = 1;
        v.released = false;
        v.ctl      = 0;
    }

    inline void release (Voice& v) noexcept
    {
        if (v.released) return;
        v.released = true;
        if (v.envStage != 0) v.envStage = 4;
    }

    /** Once per audio block, before render: the per-block constants. */
    inline void prepareBlock (Voice& v, const Params& p, double sampleRate) noexcept
    {
        const double sr = std::max (8000.0, sampleRate);
        for (int i = 0; i < 2; ++i)
            v.ratio[i] = std::exp2 ((double) p.oct[i] + ((double) p.semi[i] + (double) p.fine[i] / 100.0) / 12.0);
        v.aCoef = (float) std::exp (-1.0 / (envTimeAttack (p.envA) * sr));
        v.dCoef = (float) std::exp (-1.0 / (envTimeDecay  (p.envD) * sr));
        v.rCoef = (float) std::exp (-1.0 / (envTimeDecay  (p.envR) * sr));
        v.sus   = p.envS;
        v.dcR   = (float) (1.0 - 2.0 * 3.14159265358979 * 18.0 / sr);   // 18 Hz DC blocker
    }

    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)       { t /= dt;                return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;     return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float oscSample (int wave, double ph, double dt, float pw) noexcept
    {
        switch (wave)
        {
            case Saw:
                return (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
            case Pulse:
            {
                float y = ph < (double) pw ? 1.0f : -1.0f;
                y += polyBlep (ph, dt);
                double t2 = ph - (double) pw;
                if (t2 < 0.0) t2 += 1.0;
                y -= polyBlep (t2, dt);
                return y - (2.0f * pw - 1.0f);               // centred at any width
            }
            case Triangle:
                return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            default:
                return (float) std::sin (6.283185307179586 * ph);
        }
    }

    inline float stepEnv (Voice& v) noexcept
    {
        switch (v.envStage)
        {
            case 1:                                           // attack: RC towards 1.25, capped at 1
                v.envValue += (1.25f - v.envValue) * (1.0f - v.aCoef);
                if (v.envValue >= 1.0f) { v.envValue = 1.0f; v.envStage = 2; }
                break;
            case 2:
                v.envValue = v.sus + (v.envValue - v.sus) * v.dCoef;
                if (std::abs (v.envValue - v.sus) < 1.0e-4f) { v.envValue = v.sus; v.envStage = 3; }
                break;
            case 3:
                v.envValue = v.sus;
                break;
            case 4:
                v.envValue *= v.rCoef;
                if (v.envValue < 1.0e-5f) { v.envValue = 0.0f; v.envStage = 0; }
                break;
            default:
                break;
        }
        return v.envValue;
    }

    /** One sample at hz (the voice's live pitch: glide, bend, LFO, pitch env
        are all already in it).  note is the played MIDI note (key tracking). */
    inline float render (Voice& v, const Params& p, double hz, int note, double sampleRate) noexcept
    {
        const double sr = std::max (8000.0, sampleRate);
        const double dt1 = std::min (0.45, std::max (1.0e-6, hz * v.ratio[0] / sr));
        const double dt2 = std::min (0.45, std::max (1.0e-6, hz * v.ratio[1] / sr));

        // OSC 1, frequency-modulated by osc 2 (linear FM, no through-zero)
        const double dt1m = std::min (0.45, std::max (1.0e-6, dt1 * (1.0 + (double) (p.fm * 3.0f * v.o2))));
        const float o1 = oscSample (p.wave[0], v.ph[0], dt1m, p.pw[0]);
        v.ph[0] += dt1m;
        bool wrapped = false;
        if (v.ph[0] >= 1.0) { v.ph[0] -= 1.0; wrapped = true; }

        // OSC 2, hard-synced to osc 1 when asked
        const float o2 = oscSample (p.wave[1], v.ph[1], dt2, p.pw[1]);
        v.ph[1] += dt2;
        if (v.ph[1] >= 1.0) v.ph[1] -= 1.0;
        if (p.sync && wrapped) v.ph[1] = v.ph[0] * (dt2 / dt1m);
        v.o2 = o2;

        // SUB: a square one octave below osc 1
        float sub = 0.0f;
        if (p.sub > 0.0f)
        {
            const double dts = dt1 * 0.5;
            double t2 = v.phSub + 0.5;
            if (t2 >= 1.0) t2 -= 1.0;
            sub = (v.phSub < 0.5 ? 1.0f : -1.0f) + polyBlep (v.phSub, dts) - polyBlep (t2, dts);
            v.phSub += dts;
            if (v.phSub >= 1.0) v.phSub -= 1.0;
        }

        float nz = 0.0f;
        if (p.noise > 0.0f)
        {
            v.rng ^= v.rng << 13;  v.rng ^= v.rng >> 17;  v.rng ^= v.rng << 5;
            nz = (float) (std::int32_t) v.rng * (1.0f / 2147483648.0f);
        }

        const float plain = p.level[0] * o1 + p.level[1] * o2;
        const float mix   = (1.0f - p.ring) * plain + p.ring * (o1 * o2)
                          + p.sub * sub + p.noise * nz * 0.5f;

        // DRIVE into the ladder
        const float x = std::tanh (mix * 0.45f * (1.0f + p.drive * 5.0f));

        // THE LADDER: 4 zero-delay one-poles, resonance fed back from the last,
        // saturated in the loop.  Cutoff moves at control rate (every 8 samples).
        const float env = stepEnv (v);
        if (--v.ctl <= 0)
        {
            v.ctl = 8;
            const float m = std::min (1.0f, std::max (0.0f,
                              p.cutoff + p.envAmt * env
                              + p.keyTrack * (float) (note - 60) / 48.0f
                              + p.velAmt * (v.velocity - 0.6f) * 0.5f));
            const double fc = std::min (sr * 0.45, 20.0 * std::pow (900.0, (double) m));
            v.g = (float) std::tan (3.14159265358979 * fc / sr);
        }
        const float g   = v.g;
        const float b   = 1.0f / (1.0f + g);
        const float G   = g * b;
        const float G2  = G * G, G3 = G2 * G, G4 = G3 * G;
        const float k   = p.reso * 4.0f;
        const float S   = (G3 * v.s[0] + G2 * v.s[1] + G * v.s[2] + v.s[3]) * b;
        const float xin = x * (1.0f + 0.5f * k);                       // passband make-up
        const float y4  = (G4 * xin + S) / (1.0f + k * G4);
        float in = std::tanh (xin - k * y4);
        for (int i = 0; i < 4; ++i)
        {
            const float y = (g * in + v.s[i]) * b;
            v.s[i] = 2.0f * y - v.s[i];
            in = y;
        }

        // DC blocker (pulse width, ring and FM all leave some)
        const float y = in - v.dcX + v.dcR * v.dcY;
        v.dcX = in;
        v.dcY = y;
        return y * p.outLevel * 0.9f;
    }

    /** False once the ladder envelope has finished AND been released - the
        voice's own amp envelope decides when the voice ends; this is only for
        tests and meters. */
    inline bool isSounding (const Voice& v) noexcept { return v.envStage != 0 || ! v.released; }
}
