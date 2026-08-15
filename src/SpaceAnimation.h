#pragma once
#include <JuceHeader.h>
#include <random>

// =====================================================================================
//  SpaceAnimationComponent
//
//  Two visual layers:
//    1. Starfield  — tiny slow-moving white dots at multiple depth layers
//    2. Star orbs  — soft glowing coloured circles that drift slowly,
//                    mimicking distant colourful stars / nebula stars
//
//  Driven at 60fps by the parent OpenGLContext (setContinuousRepainting).
//  Uses normal JUCE paint() — no custom GL calls needed.
//  setInterceptsMouseClicks(false) so it never blocks underlying UI.
// =====================================================================================
class SpaceAnimationComponent : public juce::Component,
                                private juce::Timer
{
public:
    SpaceAnimationComponent()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
        initParticles();
        startTimerHz(60);
    }

    ~SpaceAnimationComponent() override { stopTimer(); }

    void setAnimating(bool shouldAnimate)
    {
        if (shouldAnimate && !isTimerRunning()) startTimerHz(60);
        if (!shouldAnimate && isTimerRunning()) stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        const float W = (float)getWidth();
        const float H = (float)getHeight();

        // ── 1. Starfield (white dots, depth-layered) ──────────────────────────
        for (auto& s : stars)
        {
            const float alpha = 0.20f + s.depth * 0.65f;
            const float size  = 0.6f  + s.depth * 1.8f;
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.fillEllipse(s.x * W - size * 0.5f,
                          s.y * H - size * 0.5f,
                          size, size);
        }

        // ── 2. Coloured star orbs (glowing circles) ───────────────────────────
        for (auto& o : orbs)
        {
            const float pulse  = 0.55f + 0.45f * std::sin(phase * o.pulseSpeed + o.pulsePhase);
            const float coreR  = o.radius * juce::jmin(W, H) * pulse;
            const float glowR  = coreR * 3.5f;
            const float cx     = o.x * W;
            const float cy     = o.y * H;

            // Outer soft glow
            juce::ColourGradient glow(
                o.colour.withAlpha(0.18f * pulse),
                cx, cy,
                o.colour.withAlpha(0.0f),
                cx + glowR, cy,
                true);
            g.setGradientFill(glow);
            g.fillEllipse(cx - glowR, cy - glowR, glowR * 2.0f, glowR * 2.0f);

            // Mid halo
            juce::ColourGradient halo(
                o.colour.withAlpha(0.45f * pulse),
                cx, cy,
                o.colour.withAlpha(0.0f),
                cx + coreR * 1.8f, cy,
                true);
            g.setGradientFill(halo);
            g.fillEllipse(cx - coreR * 1.8f, cy - coreR * 1.8f,
                          coreR * 3.6f, coreR * 3.6f);

            // Bright core
            g.setColour(o.colour.withAlpha(0.90f * pulse));
            g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

            // Specular highlight
            g.setColour(juce::Colours::white.withAlpha(0.50f * pulse));
            g.fillEllipse(cx - coreR * 0.35f, cy - coreR * 0.55f,
                          coreR * 0.5f, coreR * 0.5f);
        }
    }

private:
    // ── Data types ────────────────────────────────────────────────────────────
    struct Star
    {
        float x, y, depth, vx, vy;
    };

    struct Orb
    {
        float x, y, vx, vy;
        float radius;
        float pulseSpeed, pulsePhase;
        juce::Colour colour;
    };

    // ── Members ───────────────────────────────────────────────────────────────
    std::vector<Star> stars;
    std::vector<Orb>  orbs;
    float             phase = 0.0f;

    std::mt19937                          rng { std::random_device{}() };
    std::uniform_real_distribution<float> rnd { 0.0f, 1.0f };

    float rndRange(float lo, float hi) { return lo + rnd(rng) * (hi - lo); }

    // ── Init ──────────────────────────────────────────────────────────────────
    void initParticles()
    {
        // White star dots — 3 depth layers
        stars.resize(160);
        for (auto& s : stars)
        {
            s.x     = rnd(rng);
            s.y     = rnd(rng);
            s.depth = rnd(rng);                                      // 0=far, 1=near
            s.vx    = rndRange(-0.00006f, 0.00006f) * (0.3f + s.depth);
            s.vy    = rndRange(-0.00003f, 0.00003f) * (0.3f + s.depth);
        }

        // Coloured orbs
        const juce::Colour palette[] = {
            juce::Colour(0xFFFFAA44),   // warm orange
            juce::Colour(0xFF44AAFF),   // sky blue
            juce::Colour(0xFF88FFCC),   // teal
            juce::Colour(0xFFFFEE88),   // pale yellow
            juce::Colour(0xFFDD88FF),   // violet
            juce::Colour(0xFFFF6688),   // pink-red
            juce::Colour(0xFF66FFAA),   // mint
            juce::Colour(0xFFAADDFF),   // light blue
        };
        constexpr int kNumColours = (int)(sizeof(palette) / sizeof(palette[0]));

        orbs.resize(18);
        for (auto& o : orbs)
        {
            o.x          = rnd(rng);
            o.y          = rnd(rng);
            o.vx         = rndRange(-0.00012f, 0.00012f);
            o.vy         = rndRange(-0.00008f, 0.00008f);
            o.radius     = rndRange(0.006f, 0.018f);
            o.pulseSpeed = rndRange(0.018f, 0.055f);
            o.pulsePhase = rndRange(0.0f, juce::MathConstants<float>::twoPi);
            o.colour     = palette[(int)(rnd(rng) * kNumColours)];
        }
    }

    // ── Timer tick ────────────────────────────────────────────────────────────
    void timerCallback() override
    {
        phase += 1.0f;

        for (auto& s : stars)
        {
            s.x = std::fmod(s.x + s.vx + 1.0f, 1.0f);
            s.y = std::fmod(s.y + s.vy + 1.0f, 1.0f);
        }

        for (auto& o : orbs)
        {
            o.x += o.vx;
            o.y += o.vy;

            // Gentle random drift
            o.vx += rndRange(-0.000008f, 0.000008f);
            o.vy += rndRange(-0.000006f, 0.000006f);
            o.vx  = juce::jlimit(-0.00018f, 0.00018f, o.vx);
            o.vy  = juce::jlimit(-0.00012f, 0.00012f, o.vy);

            // Wrap with margin so glow doesn't pop
            if (o.x < -0.05f) o.x = 1.05f;
            if (o.x >  1.05f) o.x = -0.05f;
            if (o.y < -0.05f) o.y = 1.05f;
            if (o.y >  1.05f) o.y = -0.05f;
        }

        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpaceAnimationComponent)
};
