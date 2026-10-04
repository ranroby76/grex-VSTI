


#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
#include "AssignPopup.h"
#include "Harmonizer.h"     // harmonyBlue(), shared by all four views
#include "SpaceAnimation.h" // the plate's starfield
#include "InstrEditPanel.h"  // InstrEditStyle::kPanelLabel - the SAVE SET caption grey
#include <JuceHeader.h>

// =====================================================================================
//  LedButton
//  Toggle or Push button with a LED bar on top.
//  LED: dim blue when off, bright pool blue when on (override via setLedColourOn)
// =====================================================================================
class LedButton : public juce::TextButton
{
public:
    // Plate: draws like a button but is not one. Added for SOLO 8, which
    // BECAME the harmony channel and must stop looking pressable - its on/off
    // lives on the feature panel's power switch, and two controls for one state
    // is two controls that can disagree.
    enum class Mode { Toggle, Push, Selector, Plate };

    /** Solo slot index that carries the harmony voice.  Must match
        BetelgeuseProcessor::kHarmonySlot; stated here so MainTab does not need
        to include Main.h just for one integer. */
    static constexpr int kHarmonySoloSlot = 7;

    /** Solo slot index that carries MANUAL BASS - the left hand's own voice
        while that mode is on.  Must match BetelgeuseProcessor::kBassSlotDefault,
        and duplicated here for the same reason kHarmonySoloSlot is.

        Manual bass has no slot selector: it is one button, and it is always
        this slot.  (MULTI SPLIT's bass zone is separate and keeps its cycler -
        the two modes are mutually exclusive, so they never both want a slot.) */
    static constexpr int kManualBassSoloSlot = 6;

    LedButton(const juce::String& text = {}, Mode mode = Mode::Toggle)
        : juce::TextButton(text), buttonMode(mode)
    {
        setClickingTogglesState(mode == Mode::Toggle || mode == Mode::Selector);
    }

    void setMode(Mode m)
    {
        buttonMode = m;
        setClickingTogglesState(m == Mode::Toggle || m == Mode::Selector);
    }

    void setSubLabel(const juce::String& s) { subLabel = s; repaint(); }

    /** Override the background/border colour.  Transparent (the default) means
        the normal grey scheme.  Used for the harmony blue - see
        Harmonizer::harmonyBlue, which is where that colour is defined once for
        all four views that carry it. */
    void setTintColour(juce::Colour c) { tint = c; repaint(); }

    //==========================================================================
    // A DRIFTING STARFIELD INSIDE THE PLATE, same component the header uses.
    //
    // Created only on demand - one 60 Hz timer and 160 particles is not
    // something every button on the panel should be paying for, and only the
    // HARMONY plate asks.
    //
    // WHY THE TEXT MOVES TO paintOverChildren. Children paint AFTER their
    // parent, so a starfield child would draw straight over a caption drawn in
    // paintButton. Painting the field first, the stars over it, and the word
    // over both is the only order that puts the text in front - which is what
    // was asked for.
    //==========================================================================
    void setStarfield(bool on, float alphaScale = 0.28f)
    {
        if (! on)
        {
            starfield.reset();
            repaint();
            return;
        }

        if (starfield == nullptr)
        {
            starfield = std::make_unique<SpaceAnimationComponent>();
            addAndMakeVisible (*starfield);
            starfield->toBack();                  // under the caption, over the fill
            starfield->setBounds (getLocalBounds());
        }

        // Stars only. An orb's glow is 3.5x its core radius, which on a 111 px
        // plate is a coloured smear behind the word rather than a distant star.
        starfield->setOrbsVisible (false);
        starfield->setStarAlphaScale (alphaScale);
        repaint();
    }

    //==========================================================================
    // CAPTION LENGTH.
    //
    // These buttons carry INSTRUMENT names, and instrument names are not short:
    // "Steel String Guitar", "Syn Strings 1", "Orchestra Hit". drawFittedText
    // shrinks to fit, but only so far - past that it runs into the left and
    // right edges, which is exactly what was happening on the solo row.
    //
    // Ten characters INCLUDING the "..", so the cap is a real width limit
    // rather than a suggestion: eight characters plus the marker can never be
    // wider than eleven could be. Applied at paint time rather than in the
    // setter so the button keeps the FULL name - getButtonText() is what the
    // mirror compares against to decide whether anything changed, and comparing
    // against an already-shortened copy would make two different instruments
    // that share their first eight letters look like no change at all.
    //==========================================================================
    static constexpr int kMaxCaptionChars = 10;

    static juce::String elideCaption (const juce::String& text)
    {
        if (text.length() <= kMaxCaptionChars) return text;
        return text.substring (0, kMaxCaptionChars - 2).trimEnd() + "..";
    }

    /** A small tag in the TOP-LEFT corner, drawn over everything else.

        It exists because the solo buttons now show the INSTRUMENT name, which
        is the useful thing but also the thing that changes - so the button lost
        the one label that never moves. Reading "Kanun" tells you what is
        loaded and nothing about WHICH of the eight you are looking at.

        Deliberately a corner tag rather than a second centred line: the name is
        the button's content and should keep the middle. A tag reads as a label
        ON the button, the way a channel number is printed on a mixer strip. */
    void setCornerTag(const juce::String& s) { cornerTag = s; repaint(); }
    void setLedColourOn(juce::Colour c)     { ledOn = c;    repaint(); }

    /** FACE LAMP: no round lamp at all - the whole face is the indicator, the
        normal grey when off and full gold (Pal::kAccent) when on.

        For buttons too small to carry a lamp.  The JUMPS grid was the case
        that asked for it: its buttons are narrow enough that a lamp sized for
        the MAIN tab crowded the caption and dominated the button.  Off by
        default, so every MAIN tab button keeps its lamp. */
    void setFaceLamp (bool b)               { faceLamp = b; repaint(); }

    /** Caption height as a share of the text band.  0.45 suits every button on
        the MAIN tab; a pad a third of a play button tall needs more, or its
        two-character caption renders at about 6 px.  The 14 px ceiling still
        applies, so this can only ever help a SMALL button. */
    void setCaptionScale (float s)          { captionScale = juce::jlimit (0.2f, 0.9f, s); repaint(); }

    //==========================================================================
    //  THE BUTTON FACE.  GREY, NOT BLACK - and the text on it is BLACK.
    //
    //  Matched to the SET LOAD / SAVE panel so the MAIN tab reads as one
    //  surface rather than as a black grid with a grey island in it.
    //
    //  BLACK TEXT IS NOT A SEPARATE CHOICE, IT FOLLOWS FROM THE FACE.  White on
    //  a mid grey is the worst contrast pairing on this panel; black on it is
    //  over 5:1.  Every label the button draws - caption, sub-label and corner
    //  tag - uses these, so nothing can be added later that keeps white by
    //  accident.
    //
    //  SHARED WITH JumpsTab, which uses the same class for its selector.  Its
    //  buttons change with these; setFaceColours below is the per-instance way
    //  out if that is ever unwanted.
    //==========================================================================
    //  ── ONE FACE, ON OR OFF.  THE LAMP IS WHAT SAYS WHICH. ──────────────────
    //
    //  kFaceOff used to be a darker grey than kFaceOn, so a row read as light
    //  and dark blocks.  That was the button saying its state twice - the LED
    //  above it already says it, and says it better - and it cost the layout
    //  too: a row of ON buttons looked tighter than a row of OFF ones.
    //
    //  kFaceOff is kept as a NAME rather than deleted, because paintButton
    //  still has to choose something for the resting state and a call site
    //  reading kFaceOn for an off button would be a puzzle for the next reader.
    static constexpr juce::uint32 kFaceOn       = 0xFF9A9A9A;   // the face
    static constexpr juce::uint32 kFaceOff      = kFaceOn;      // ...the same face

    //  HOVER AND PRESS STAY, and they are not state - they are the button
    //  answering the mouse.  Kept near the face so neither reads as "on".
    //  Black text is 7.5:1 on the face, 6.5:1 held.
    static constexpr juce::uint32 kFaceHover    = 0xFFA6A6A6;
    static constexpr juce::uint32 kFaceDown     = 0xFF8A8A8A;

    //  UNAVAILABLE stays dark, and its text stays faint on purpose - "the style
    //  has no such section" must read as absent, and a legible label on a
    //  greyed button reads as merely off.
    static constexpr juce::uint32 kFaceUnavail  = 0xFF4F4F4F;
    static constexpr juce::uint32 kFaceBorder   = 0xFF3A3A3A;

    //==========================================================================
    //  THE LAMP IS A ROUND LED IN THE TOP-RIGHT CORNER.
    //
    //  It was a bar across the top - kLedWidthFrac, kLedTopInset - and a bar is
    //  the wrong shape for this: it is as wide as the button, so it competes
    //  with the caption for the eye, and on a light grey face a wide block of
    //  colour reads as a second background rather than as an indicator.
    //
    //  A CIRCLE IN THE CORNER instead, the same construction as the header's
    //  registration lamp: flat fill plus a small off-centre white highlight for
    //  the glass. The one addition is a BLACK RING - the registration lamp sits
    //  on a near-black panel and needs no outline, this one sits on #9A9A9A and
    //  would otherwise have nothing to end against.
    //
    //  The caption gets the whole button height back, since nothing is reserved
    //  across the top any more.
    //==========================================================================
    /** Diameter, as a fraction of the button's HEIGHT - so the lamp keeps its
        proportions on the short variation buttons as well as the tall ones. */
    static constexpr float kLedDiaFrac  = 0.30f;

    /** Smallest and largest it may be, whatever the button does. */
    static constexpr float kLedDiaMin   = 7.0f;
    static constexpr float kLedDiaMax   = 13.0f;

    /** Distance from the button's top and right edges to the lamp's own edge. */
    static constexpr float kLedCornerPad = 4.0f;

    /** The black ring. */
    static constexpr float kLedRing = 1.2f;

    /** THE LIT LAMP, DARKENED FOR A GREY FACE.

        Pal::kLedOn (#DD8E37) is tuned against the near-black panels the rest of
        the UI uses; on a #7A7A7A button it is brighter than its own surround and
        reads as glare rather than as a lamp.  This is the same hue taken down
        two stops.

        WHAT DECIDES IT IS LIT-vs-UNLIT, NOT LIT-vs-FACE.  On a mid grey, a lamp
        cannot separate from its background by luminance in either direction -
        that is what the grey face costs.  It separates by CHROMA from the face
        and by luminance from the unlit lamp beside it, which is the comparison
        a player actually makes: not "is this bar bright" but "is it brighter
        than the seven next to it".

        TAKEN DOWN AGAIN once every face became the lighter #9A9A9A. */
    static constexpr juce::uint32 kLedOnGrey = 0xFF8A5010;

    /** THE UNLIT BAR, DARKENED TO PAY FOR IT.

        Darkening the lit lamp on its own would have been a bad trade: it moves
        TOWARD the unlit bar beside it, and at #7A4408 the two were 1.96:1 apart
        - close enough that a row of lamps stops answering "which of these is
        on" at a glance, which is the only question they exist for.

        So the unlit bar goes down with it and the pair keeps 2.7:1. On the old
        near-black panels Pal::kLedDimAvail was dark ENOUGH; on a #9A9A9A face
        there is room below it and no reason not to use it.

        UNAVAILABLE IS DARKER STILL - "this style has no such section" has to
        read as no lamp rather than as an unlit one. */
    static constexpr juce::uint32 kLedDimGrey    = 0xFF241505;
    static constexpr juce::uint32 kLedDimOffGrey = 0xFF0A0602;
    void setAlternateText(const juce::String& onText) { altText = onText; repaint(); }

    /** Currently-playing pip — used by MainTab to show which variation
        the StyleSequencer is actually playing right now (distinct from
        the user's most-recent click which sets the toggle state). */
    void setPlaying(bool p) { if (p == isPlaying) return; isPlaying = p; repaint(); }
    bool getPlaying() const noexcept { return isPlaying; }

    /** When set, ALL visual highlight state (background, border, LED bar,
        altText) follows `isPlaying` rather than the toggle/click state.
        This is what variation buttons want: clicking a button queues the
        switch but the button itself only lights up once the sequencer has
        actually moved to that section.  Default false preserves the
        toggle-driven behaviour used by Play/Stop, Sync, Solo, etc. */
    void setLedFollowsPlayingOnly(bool b) { if (b == ledFollowsPlayingOnly) return; ledFollowsPlayingOnly = b; repaint(); }

    /** THE PLATE'S CAPTION, drawn after the starfield child.  See setStarfield
        for why it cannot live in paintButton. Only the plate uses this; every
        other mode draws its text in paintButton as before. */
    void paintOverChildren(juce::Graphics& g) override
    {
        if (buttonMode != Mode::Plate) return;

        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.34f, 14.0f),
                             juce::Font::bold));
        g.drawFittedText(elideCaption(getButtonText()),
                         bounds.toNearestInt().reduced(3),
                         juce::Justification::centred, 1);

        // THE CORNER TAG, IN WHITE, and here rather than in paintButton for the
        // same reason the caption is: the starfield is a child and would paint
        // straight over anything written earlier.
        //
        // White rather than the dimmed grey the other modes use. Grey is right
        // when the tag sits behind a lit caption on a dark button; on the blue
        // field it reads as disabled, which is the wrong thing to say about the
        // one control here that is deliberately not pressable.
        if (cornerTag.isNotEmpty())
        {
            const float tagH = juce::jmax(7.0f, bounds.getHeight() * 0.20f);
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.setFont(juce::Font(tagH * 0.95f, juce::Font::bold));
            g.drawText(cornerTag,
                       bounds.toNearestInt().reduced(4, 2),
                       juce::Justification::topLeft, false);
        }
    }

    void resized() override
    {
        if (starfield != nullptr)
            starfield->setBounds(getLocalBounds());
    }

    //==========================================================================
    //  paintLitLamp - THE ONE DEFINITION OF A LIT LAMP.
    //
    //  Moved out of paintButton when a second surface needed lamps: the black
    //  SoloPairPanel carries two, and two copies of this drawing would drift -
    //  the day one is retuned, the lamps on one row stop matching the other.
    //
    //  `haloClip` is the surface the lamp is mounted on.  The glow is light
    //  landing on THAT surface, so it stops at its edge rather than hanging in
    //  the air beside it.  The unlit lens and the bezel ring are left to each
    //  caller, because those two depend on what colour the surface is.
    //==========================================================================
    static void paintLitLamp (juce::Graphics& g, juce::Rectangle<float> ledArea,
                              const juce::Path& haloClip, juce::Colour ledOn)
    {
        //==================================================================
        //  A LIT LAMP, BUILT THE WAY A REAL ONE LOOKS.
        //
        //  It used to be a flat disc with a smaller white disc on top - one
        //  hard-edged, perfectly round highlight and no light leaving the
        //  lens at all.  That reads as a sticker, for two reasons this
        //  version answers one by one:
        //
        //    NO DIFFUSION.  A lit lens throws light onto the face around it,
        //    and its own body is not one colour: it is hottest where the
        //    light enters and falls off to a deeper tint at the far rim.
        //
        //    A SYMMETRIC HIGHLIGHT.  A reflection on a dome is not a circle
        //    and not centred.  It is an ellipse, long across the curvature,
        //    sitting toward the light and soft on its lower edge - and the
        //    light that gets THROUGH the dome comes out on the opposite
        //    side as a faint crescent.
        //
        //  Four layers, back to front: halo, lens, specular, glint.  The
        //  light is upper-left throughout, which is where every other
        //  shaded surface in the UI takes its light from.
        //==================================================================
        const float dia = ledArea.getWidth();
        const float r   = dia * 0.5f;
        const juce::Point<float> c = ledArea.getCentre();

        // The EMITTED colour.  ledOn is the lamp's identity but it is the
        // colour of the lens at rest - kLedOnGrey is a deep amber - and
        // light is brighter than the glass it comes through.  Lifting the
        // brightness is what turns the halo into light on the face rather
        // than a brown stain on it.
        const juce::Colour emit = ledOn.withMultipliedSaturation(1.15f)
                                       .withMultipliedBrightness(1.6f);

        // 1. HALO - the diffusion.  Clipped to the face's own rounded rect:
        //    the lamp sits in a corner, and unclipped its glow would paint
        //    into the transparent corner OUTSIDE the button, i.e. light
        //    hanging in mid-air beside it.  Radial and round on purpose - a
        //    glow is symmetric about its source; the asymmetry belongs to the
        //    lens and its reflections, below.
        {
            juce::Graphics::ScopedSaveState keepClip (g);
            g.reduceClipRegion(haloClip);

            const float haloR = r * 2.1f;
            juce::ColourGradient halo(emit.withAlpha(0.55f), c.x, c.y,
                                      emit.withAlpha(0.0f),  c.x + haloR, c.y,
                                      true);
            halo.addColour(0.48, emit.withAlpha(0.30f));   // at the lens rim
            halo.addColour(0.74, emit.withAlpha(0.09f));
            g.setGradientFill(halo);
            g.fillEllipse(juce::Rectangle<float>(haloR * 2.0f, haloR * 2.0f)
                              .withCentre(c));
        }

        // 2. LENS - hot where the light enters, deeper at the far rim.  The
        //    gradient is centred OFF the middle, toward the light, so the
        //    near rim stays bright and the far one falls to ledOn's own
        //    depth.  That falloff is most of what makes it read as a dome.
        {
            const juce::Point<float> hot = c.translated(-r * 0.26f, -r * 0.30f);
            juce::ColourGradient lens(emit.interpolatedWith(juce::Colours::white, 0.50f),
                                      hot.x, hot.y,
                                      ledOn.withMultipliedBrightness(0.72f),
                                      hot.x + r * 1.30f, hot.y,
                                      true);
            lens.addColour(0.42, emit);
            g.setGradientFill(lens);
            g.fillEllipse(ledArea);
        }

        // 3. SPECULAR - the reflection of the light itself.  An ellipse
        //    long across the curvature, tilted perpendicular to the line
        //    from the centre to the light (about -27 degrees), placed toward
        //    the light, and faded out downward so it has no lower edge.
        //    Sized so its far tip stays inside the rim at every diameter.
        {
            const juce::Point<float> sc = c.translated(-r * 0.22f, -r * 0.42f);
            juce::Path spec;
            spec.addEllipse(-dia * 0.25f, -dia * 0.14f, dia * 0.50f, dia * 0.28f);
            spec.applyTransform(juce::AffineTransform::rotation(-0.48f)
                                                      .translated(sc.x, sc.y));
            const auto sb = spec.getBounds();
            juce::ColourGradient sg(juce::Colours::white.withAlpha(0.85f),
                                    sb.getCentreX(), sb.getY(),
                                    juce::Colours::white.withAlpha(0.0f),
                                    sb.getCentreX(), sb.getBottom(),
                                    false);
            g.setGradientFill(sg);
            g.fillPath(spec);
        }

        // 4. GLINT - light that passed through the dome, focused onto the
        //    OPPOSITE inside edge.  A thin crescent, lower-right, tinted by
        //    the lens it came through.  Faint on purpose: at 7-13 px it is a
        //    pixel or so, and it is what separates glass from paint.
        {
            juce::Path glint;
            glint.addCentredArc(c.x, c.y, r * 0.74f, r * 0.74f, 0.0f,
                                juce::MathConstants<float>::pi * 0.60f,
                                juce::MathConstants<float>::pi * 0.90f,
                                true);
            g.setColour(emit.interpolatedWith(juce::Colours::white, 0.60f)
                            .withAlpha(0.38f));
            g.strokePath(glint, juce::PathStrokeType(r * 0.20f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        //----------------------------------------------------------------------
        // PLATE: a field with a word on it, and none of the button furniture.
        //
        // Everything below this - the border, the LED bar, the corner tag, the
        // hover and press shading - exists to say "this is pressable and here
        // is its state". A plate is neither, so drawing them was the button
        // showing through the thing that is supposed to have replaced it.
        //
        // Returns early rather than guarding each piece: one exit is easier to
        // keep true than six conditions, and there is nothing below that a
        // plate wants.
        //----------------------------------------------------------------------
        if (buttonMode == Mode::Plate)
        {
            if (! tint.isTransparent())
            {
                g.setColour(tint);
                g.fillRoundedRectangle(bounds.reduced(0.5f), 4.0f);
            }

            // The caption is drawn in paintOverChildren, NOT here: the
            // starfield is a child and children paint after their parent, so a
            // word written here would end up behind the stars.
            return;
        }

        const bool toggleOn    = (buttonMode == Mode::Push) ? isButtonDown : getToggleState();

        // UNAVAILABLE = the style has no such section.  Drawn dim and, crucially,
        // its LED can never light: the button stays on screen so the panel keeps
        // its shape, but it reads as absent rather than as merely unlit.
        const bool avail = isEnabled();

        // ── LED ONLY, FOR VARIATION BUTTONS ─────────────────────────────────
        //
        // The background used to follow the CLICK while the LED followed what
        // was actually playing.  Two indicators for one question: after a
        // switch the old section kept a bright panel while the new one had the
        // lit LED, so the brightest thing on screen was the variation that had
        // just STOPPED.  A player reads brightness first and the small red bar
        // second, so the panel was actively telling them the wrong section.
        //
        // Now ledFollowsPlayingOnly means exactly that - the LED is the whole
        // indication and the background never highlights.  Hover and press
        // feedback below are untouched; those answer "am I about to click
        // this", not "what is playing".
        const bool highlightOn = ledFollowsPlayingOnly ? false : toggleOn;
        const bool ledOnState  = avail && (ledFollowsPlayingOnly ? isPlaying : toggleOn);

        // altText has to follow whatever this button uses as its lit state, or
        // a variation button would never show it now that highlightOn is false.
        const bool textLit     = ledFollowsPlayingOnly ? ledOnState : highlightOn;

        // Background
        const bool tinted = ! tint.isTransparent();

        if (tinted)
            g.setColour(tint.withMultipliedBrightness(0.42f));
        else if (! avail)
            g.setColour(juce::Colour(kFaceUnavail));    // sunk below the panel
        else if (faceLamp && ledOnState)
            // FACE LAMP: the whole face is the indicator - full gold on.  The
            // same Pal::kAccent the tempo SelectorGroups use for their fore, so
            // "selected" is one colour everywhere.  A press darkens it a touch
            // so the click still answers.  See setFaceLamp.
            g.setColour(juce::Colour(Betel::Pal::kAccent)
                            .darker(isButtonDown ? 0.15f : 0.0f));
        else if (highlightOn)
            g.setColour(juce::Colour(kFaceOn));
        else if (isButtonDown)
            g.setColour(juce::Colour(kFaceDown));
        else if (isHighlighted)
            g.setColour(juce::Colour(kFaceHover));
        else
            g.setColour(juce::Colour(kFaceOff));

        g.fillRoundedRectangle(bounds.reduced(0.5f), 4.0f);

        // Border
        // ── THE BORDER IS THE SAME COLOUR ON OR OFF, AND THAT MATTERS ────────
        //
        // It used to go light when the button was toggled on.  A LIGHT border
        // reads as part of the button; a DARK one merges into the gap beside
        // it - so a row where every button is on (STYLE ELEMENTS, normally all
        // eight) looked 2 to 3 px tighter than a row where they are off (SOLO
        // ELEMENTS), with identical geometry underneath.  Measured: 3 px gaps
        // against 6 px, from the same layoutRow call.
        //
        // The face already says on or off - #7A7A7A against #9A9A9A - so the
        // border was saying it a second time and charging the layout for it.
        g.setColour(tinted   ? tint
                   : ! avail ? juce::Colour(0xFF2A2A2A)
                             : juce::Colour(kFaceBorder));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // ── THE ROUND LAMP, TOP-RIGHT ────────────────────────────────────────
        //
        // Not drawn at all on a FACE LAMP button - there the face carries the
        // state, and a lamp on top of it would be saying it twice.
        if (! faceLamp)
        {
        const float dia = juce::jlimit(kLedDiaMin, kLedDiaMax,
                                       bounds.getHeight() * kLedDiaFrac);
        const juce::Rectangle<float> ledArea(
            bounds.getRight()  - kLedCornerPad - dia,
            bounds.getY()      + kLedCornerPad,
            dia, dia);

        // Two dim levels: an available-but-silent lamp still reads as a lamp
        // that could light, an unavailable one has to read as no lamp at all.
        // LOCAL, not Pal::kLedDim* - see kLedDimGrey.  The palette's dim levels
        // are tuned against the near-black panels elsewhere in the UI.
        const juce::Colour ledDim = avail ? juce::Colour(kLedDimGrey)
                                          : juce::Colour(kLedDimOffGrey);

        if (! ledOnState)
        {
            g.setColour(ledDim);
            g.fillEllipse(ledArea);
        }
        else
        {
            // The lit layers are shared with SoloPairPanel - see paintLitLamp.
            // The halo is bounded by this button's own face.
            juce::Path facePath;
            facePath.addRoundedRectangle(bounds.reduced(0.5f), 4.0f);
            paintLitLamp(g, ledArea, facePath, ledOn);
        }

        // THE BLACK RING, LAST so nothing draws over it. It is what gives the
        // lamp an edge against a light grey face - the registration lamp needs
        // none because it sits on near-black.  Lit, it is also the bezel that
        // separates the lens from its own halo, which is how a panel-mounted
        // lamp looks.
        g.setColour(juce::Colours::black.withAlpha(avail ? 0.85f : 0.55f));
        g.drawEllipse(ledArea, kLedRing);
        }   // ! faceLamp

        // Text
        // ONE place, so nothing can be added later that misses it.
        juce::String displayText = elideCaption (
            (textLit && altText.isNotEmpty()) ? altText : getButtonText());
        g.setColour(juce::Colours::black.withAlpha(avail ? 0.92f : 0.38f));

        if (subLabel.isNotEmpty())
        {
            auto top    = bounds.withY(bounds.getY() + 2.0f)
                                .withHeight(bounds.getHeight() * 0.40f);
            auto bottom = juce::Rectangle<float>(bounds.getX(),
                                                  top.getBottom(),
                                                  bounds.getWidth(),
                                                  bounds.getBottom() - top.getBottom() - 2.0f);
            g.setFont(juce::Font(juce::jmin(top.getHeight() * 0.65f, 13.0f), juce::Font::bold));
            g.drawFittedText(displayText, top.toNearestInt().reduced(2), juce::Justification::centred, 1);
            g.setFont(juce::Font(juce::jmin(bottom.getHeight() * 0.55f, 11.0f)));
            g.setColour(juce::Colours::black.withAlpha(avail ? 0.70f : 0.30f));
            g.drawFittedText(subLabel, bottom.toNearestInt().reduced(2), juce::Justification::centred, 2);
        }
        else
        {
            // A corner tag takes its band off the TOP of the text area rather
            // than being drawn over it. Centring the caption in the full height
            // and then painting a tag into the same space is how two labels end
            // up touching on a short button - and these buttons get shorter
            // every time the panel is resized down.
            const float tagBand = cornerTag.isNotEmpty()
                                    ? juce::jlimit (7.0f, 11.0f, bounds.getHeight() * 0.22f) + 1.0f
                                    : 0.0f;

            auto textBounds = bounds.withY(bounds.getY() + 2.0f + tagBand)
                                    .withHeight(bounds.getHeight() - 4.0f - tagBand);
            g.setFont(juce::Font(juce::jmin(textBounds.getHeight() * captionScale, 14.0f), juce::Font::bold));
            g.drawFittedText(displayText, textBounds.toNearestInt().reduced(2), juce::Justification::centred, 2);
        }

        // ── CORNER TAG ──────────────────────────────────────────────────────
        //
        // LAST, so nothing can draw over it, and tucked under the LED bar so it
        // never fights the caption for the middle of the button.
        //
        // Dimmer than the instrument name on purpose. It is there to be found
        // when looked for, not to compete with the name for attention - a tag
        // as bright as the content would make every button read as two labels
        // of equal weight, which is worse than the single label it replaced.
        if (cornerTag.isNotEmpty())
        {
            const float tagH = juce::jlimit (7.0f, 11.0f, bounds.getHeight() * 0.22f);
            // WIDTH STOPS SHORT OF THE LAMP.  The tag sits top-LEFT and the
            // lamp top-RIGHT, on the same line now that the bar is gone, so the
            // tag has to end before the lamp's ring rather than run under it.
            const float ledDia  = juce::jlimit(kLedDiaMin, kLedDiaMax,
                                               bounds.getHeight() * kLedDiaFrac);
            const float tagRight = bounds.getRight() - kLedCornerPad - ledDia - 4.0f;

            auto tagArea = juce::Rectangle<float> (bounds.getX() + 3.0f,
                                                   bounds.getY() + 2.0f,
                                                   juce::jmax (10.0f,
                                                       tagRight - bounds.getX() - 3.0f),
                                                   tagH);

            g.setFont (juce::Font (tagH * 0.95f, juce::Font::bold));
            g.setColour (juce::Colours::black.withAlpha (avail ? 0.60f : 0.25f));
            g.drawFittedText (cornerTag, tagArea.toNearestInt(),
                              juce::Justification::centredLeft, 1);
        }
    }

private:
    Mode buttonMode = Mode::Toggle;
    // POOL BLUE, not red. JumpsTab reuses this same class for its selector
    // grid, so both tabs' lamps follow this one member - and both follow kBase
    // through it.
    // DARKENED GOLD, not Pal::kLedOn - see kLedOnGrey.  The palette's lamp is
    // tuned for the near-black panels elsewhere and disappears into a grey face.
    juce::Colour ledOn { kLedOnGrey };
    bool faceLamp = false;              // see setFaceLamp
    float captionScale = 0.45f;         // see setCaptionScale
    juce::String subLabel;
    juce::Colour tint { juce::Colours::transparentBlack };
    std::unique_ptr<SpaceAnimationComponent> starfield;
    juce::String cornerTag;
    juce::String altText;
    bool isPlaying = false;
    bool ledFollowsPlayingOnly = false;
};


//======================================================================================
// PlainButton — LedButton's chrome without the lamp.
//
// The record and song buttons are not transport state, they are actions, and a
// red LED bar on them would read as "this section is playing" alongside the six
// buttons beside them that mean exactly that. State is carried by the FILL
// colour instead, which is what a record button does everywhere.
//======================================================================================
class PlainButton : public juce::Button
{
public:
    PlainButton() : juce::Button ({}) {}

    /** Background when lit. Empty (default) leaves the normal dark fill. */
    void setLitColour (juce::Colour c)   { litCol = c; repaint(); }
    void setLit (bool b)                 { if (b == lit) return; lit = b; repaint(); }
    bool isLit() const noexcept          { return lit; }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        const bool avail = isEnabled();

        juce::Colour fill = lit             ? litCol
                          : ! avail         ? juce::Colour (0xFF151515)
                          : isButtonDown    ? juce::Colour (0xFF2E2E2E)
                          : isHighlighted   ? juce::Colour (0xFF2A2A2A)
                                            : juce::Colour (0xFF1E1E1E);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds.reduced (0.5f), 4.0f);

        g.setColour (! avail ? juce::Colour (0xFF2A2A2A)
                   : lit     ? fill.brighter (0.35f)
                             : juce::Colour (0xFF444444));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        // White on a lit fill, the usual dim white otherwise - a dark red panel
        // needs the text at full strength or it disappears into it.
        g.setColour (lit ? juce::Colours::white
                         : juce::Colours::white.withAlpha (avail ? 0.92f : 0.28f));
        g.setFont (juce::Font (juce::jmin (bounds.getHeight() * 0.42f, 12.0f),
                               juce::Font::bold));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (3, 1),
                          juce::Justification::centred, 1);
    }

private:
    juce::Colour litCol { 0xFF8B0000 };   // dark red
    bool lit = false;
};


// =====================================================================================
//  MainTab — Canvas: 918 x 435 (design coords)
//
//  This tab is the arranger's panel.  It owns the 16 variation buttons
//  (3 intros + 4 mains + 4 fills + break + 3 endings, plus one spare),
//  the 8 solo selectors, the 8 style-element on/off buttons, and the
//  play-control row (sync-play, play/stop, on-press, funkey, crash, fingered,
//  restart).
//
//  Wiring to the engine is OUTSIDE this class: MainTab fires generic
//  callbacks and MainComponent translates them into StyleSequencer
//  commands.  This keeps MainTab UI-only and engine-agnostic.
//
//  Variation-button index layout (kVariNames in the .cpp):
//       0..3   INTRO 1-4     (only 1-3 map to IntroA/B/C; index 3 is a
//                             spare with no underlying section)
//       4..7   VAR 1-4       → Main A/B/C/D
//       8..11  FILL 1-4      → Fill AA/BB/CC/DD
//       12     BRAKE         → FillBA (the "Break" section)
//       13..15 END 1-3       → Ending A/B/C
// =====================================================================================
//==============================================================================
//  OctaveMacroPad - THE SOLO OCTAVE MACRO: five pads in a card-five pattern.
//
//        [+1]        [+2]
//              [ 0 ]
//        [-1]        [-2]
//
//  A SECOND VIEW OF ONE NUMBER - the octave of the selected solo slot, the
//  same SlotParams::octaveOffset the sound editor's OCT slider edits - and it
//  deliberately keeps no copy of that number.  A press ASKS for a value; what
//  the pads SHOW comes back from the model through setOctave(), which the
//  editor's 30 Hz mirror calls.  One owner is what lets the pads, the slider
//  and the engine never disagree, whichever of the three moved last.
//
//  The slider reaches +/-3 and the pads stop at +/-2.  At +/-3 no pad lights,
//  which is the honest answer - lighting the nearest pad would claim a value
//  that is not the one playing.
//
//  Face-lamp LedButtons, the same grey-off / gold-on pads as the JUMPS grid:
//  a round lamp would not fit a button a third of a play button wide.
//==============================================================================
class OctaveMacroPad : public juce::Component
{
public:
    std::function<void (int octaves)> onOctave;

    OctaveMacroPad()
    {
        for (int i = 0; i < kNumPads; ++i)
        {
            auto& b = pads[(size_t) i];
            b.setButtonText (kLabels[i]);
            b.setMode (LedButton::Mode::Selector);
            b.setFaceLamp (true);
            b.setCaptionScale (0.70f);

            // A RADIO GROUP, so JUCE itself guarantees one lit pad: clicking a
            // pad that is already on leaves it on instead of toggling it off,
            // and lighting one turns its siblings off.  onClick still fires in
            // both cases, so a repeat press is simply a request for the value
            // that is already set.
            b.setRadioGroupId (kRadioGroup);
            b.onClick = [this, i] { if (onOctave) onOctave (kValues[i]); };
            addAndMakeVisible (b);
        }
    }

    /** Show the model's value.  Called every tick, and deliberately NOT cached:
        a cache would go stale the moment a click lit a pad the model then
        refused, and the next tick would skip the correction.  setToggleState
        is a no-op for an unchanged state, so re-asserting costs nothing. */
    void setOctave (int octaves)
    {
        for (int i = 0; i < kNumPads; ++i)
            pads[(size_t) i].setToggleState (kValues[i] == octaves,
                                             juce::dontSendNotification);
    }

    /** The gap between pads - MainTab passes its own, so the pad reads as part
        of the same grid as the rows around it. */
    void setGap (int g)                 { gap = juce::jmax (0, g); resized(); }

    void resized() override
    {
        // THE LEFTOVER PIXELS GO TO THE MIDDLE column and row, never the edges:
        // a card-five reads by its symmetry, so the four corner pads have to
        // be identical and only the centre may be a pixel larger.  Every gap
        // is exactly `gap`, same rule as MainTab::layoutRow.
        const int aw = juce::jmax (3, getWidth()  - 2 * gap);
        const int ah = juce::jmax (3, getHeight() - 2 * gap);
        const int cw = aw / 3, cwMid = aw - 2 * cw;
        const int ch = ah / 3, chMid = ah - 2 * ch;

        const int x0 = 0, x1 = cw + gap, x2 = x1 + cwMid + gap;
        const int y0 = 0, y1 = ch + gap, y2 = y1 + chMid + gap;

        pads[0].setBounds (x0, y0, cw,    ch);      // +1   top-left
        pads[1].setBounds (x2, y0, cw,    ch);      // +2   top-right
        pads[2].setBounds (x1, y1, cwMid, chMid);   //  0   centre
        pads[3].setBounds (x0, y2, cw,    ch);      // -1   bottom-left
        pads[4].setBounds (x2, y2, cw,    ch);      // -2   bottom-right
    }

private:
    static constexpr int kNumPads    = 5;
    static constexpr int kRadioGroup = 0x0C7A;

    // Same order as the cells in resized(): top-left, top-right, centre,
    // bottom-left, bottom-right.  ASCII minus on purpose - the tree carries no
    // non-ASCII string literals.
    static constexpr int kValues[kNumPads] = { +1, +2, 0, -1, -2 };
    static inline const char* const kLabels[kNumPads] = { "+1", "+2", "0", "-1", "-2" };

    std::array<LedButton, kNumPads> pads;
    int gap = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaveMacroPad)
};

//==============================================================================
//  SoloPairPanel - SOLO 7 and SOLO 8 as ONE display instead of two buttons.
//
//      +-----------------------------+----------------------------+
//      | MANUAL BASS               o | HARMONY                  o |
//      |      Electric Bass (finger) |     String Ensemble 2      |
//      +-----------------------------+----------------------------+
//
//  WHY: the two slots looked exactly like the six buttons beside them and did
//  not behave like them.  They are the manual-bass and harmony CHANNELS,
//  driven by their own features, and cannot be switched on from this row -
//  so two buttons that ignore a press, in a row of six that answer one, read
//  as broken.  A black display titled by FEATURE rather than by slot number
//  reads as what it is: a readout of what those two channels are doing.
//
//  THE PARTS:
//    frame    the neighbour buttons' face grey, a little darker, derived from
//             LedButton::kFaceOn so it stays "a little darker than the
//             buttons" if the buttons are ever retuned.  Edged in the same
//             dark border every button in the row carries.
//    display  black.
//    divider  InstrEditStyle::kPanelLabel, the SAVE SET caption grey, at the
//             panel's middle x.  The panel spans exactly the two old cells, so
//             the middle is where the two slots used to meet.
//    titles   MANUAL BASS | HARMONY, top-left of each half, where the SOLO n
//             tag sits on the buttons beside it.
//    name     the channel's instrument, larger, centred - same place and size
//             rule as the instrument name on the neighbour buttons.
//    lamp     top-right of each half: bassZone() purple, harmonyBlue() blue,
//             drawn by LedButton::paintLitLamp so a lit lamp here is the same
//             lamp as everywhere else on the tab.
//
//  NOT A BUTTON.  It takes no clicks at all - the same contract the two lamp
//  buttons had, and for the same reason: nothing on this row may write the
//  solo mask for a slot the row cannot switch on.
//==============================================================================
class SoloPairPanel : public juce::Component
{
public:
    enum Section { kBass = 0, kHarmony = 1 };

    SoloPairPanel()                     { setInterceptsMouseClicks (false, false); }

    /** Lamp and instrument name for one half.  Called every mirror tick; only
        a real change repaints. */
    void setSection (Section which, bool lit, const juce::String& instrument)
    {
        auto& s = halves[(size_t) which];
        if (s.lit == lit && s.name == instrument) return;
        s.lit  = lit;
        s.name = instrument;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto  b       = getLocalBounds().toFloat().reduced (0.5f);
        constexpr float kR  = 4.0f;                 // the neighbour buttons' corner

        // 1. FRAME, edged like every button in the row.
        g.setColour (juce::Colour (LedButton::kFaceOn).darker (0.12f));
        g.fillRoundedRectangle (b, kR);
        g.setColour (juce::Colour (LedButton::kFaceBorder));
        g.drawRoundedRectangle (b, kR, 1.0f);

        // 2. THE BLACK DISPLAY.  Frame thickness follows the panel height the
        //    way the lamps do, clamped so it never vanishes or turns into a
        //    border wider than the text it surrounds.
        const float frame   = juce::jlimit (2.0f, 5.0f, b.getHeight() * 0.06f);
        const auto  display = b.reduced (frame);
        const float displayR = juce::jmax (1.0f, kR - frame * 0.5f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (display, displayR);

        // 3. THE DIVIDER, snapped to a whole pixel so it stays one crisp line
        //    instead of two half-lit ones.
        const float midX = std::floor (display.getCentreX());
        g.setColour (InstrEditStyle::kPanelLabel);
        g.fillRect (juce::Rectangle<float> (midX, display.getY(), 1.0f, display.getHeight()));

        // 4. THE TWO HALVES.
        juce::Path displayPath;
        displayPath.addRoundedRectangle (display, displayR);

        paintHalf (g, display.withRight (midX),        b, displayPath, kBass);
        paintHalf (g, display.withLeft  (midX + 1.0f), b, displayPath, kHarmony);
    }

private:
    struct Half { bool lit = false; juce::String name; };
    std::array<Half, 2> halves;

    void paintHalf (juce::Graphics& g, juce::Rectangle<float> area,
                    juce::Rectangle<float> panel, const juce::Path& displayPath,
                    Section which) const
    {
        const auto& h        = halves[(size_t) which];
        const auto  lampCol  = (which == kBass) ? Betel::Harmonizer::bassZone()
                                                : Betel::Harmonizer::harmonyBlue();
        const juce::String title = (which == kBass) ? "MANUAL BASS" : "HARMONY";

        // ── LAMP, top-right.  Sized from the PANEL height with the same
        //    constants as every button lamp, and its top on the same line as
        //    theirs, so the row's lamps stay level across the join.
        const float dia = juce::jlimit (LedButton::kLedDiaMin, LedButton::kLedDiaMax,
                                        panel.getHeight() * LedButton::kLedDiaFrac);
        const float ledY = juce::jmax (area.getY() + 1.5f,
                                       panel.getY() + LedButton::kLedCornerPad);
        const juce::Rectangle<float> led (area.getRight() - LedButton::kLedCornerPad - dia,
                                          ledY, dia, dia);

        if (h.lit)
        {
            // The glow is light landing on THIS half of the display: clipped
            // to it, so a lit harmony lamp does not wash purple's half blue.
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (area.toNearestInt());
            LedButton::paintLitLamp (g, led, displayPath, lampCol);
        }
        else
        {
            // UNLIT, BUT STILL A LAMP.  The button lamps' dark lens and black
            // ring are tuned for a light grey face and vanish on black, so an
            // unlit lamp here is its own colour, dimmed - a real coloured LED
            // is tinted when off - and it gets a faint light ring instead of a
            // black one, because black on black is no edge at all.
            g.setColour (lampCol.withMultipliedBrightness (0.28f));
            g.fillEllipse (led);
        }
        g.setColour (juce::Colours::white.withAlpha (h.lit ? 0.30f : 0.22f));
        g.drawEllipse (led, LedButton::kLedRing);

        // ── TITLE, top-left - the band the SOLO n tag uses on the buttons.
        const float tagH = juce::jlimit (7.0f, 11.0f, panel.getHeight() * 0.22f);
        const auto  titleArea = juce::Rectangle<float> (area.getX() + 5.0f,
                                                        ledY - 1.0f,
                                                        led.getX() - 4.0f - (area.getX() + 5.0f),
                                                        tagH + 2.0f);
        g.setColour (InstrEditStyle::kPanelLabel);
        g.setFont (juce::Font (tagH * 0.95f, juce::Font::bold));
        g.drawFittedText (title, titleArea.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.85f);

        // ── INSTRUMENT NAME, larger, centred below the title band - the same
        //    size rule the neighbour buttons use for theirs.
        if (h.name.isNotEmpty())
        {
            const auto nameArea = area.withTop (titleArea.getBottom())
                                      .withTrimmedBottom (2.0f)
                                      .reduced (4.0f, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont (juce::Font (juce::jmin (nameArea.getHeight() * 0.45f, 14.0f),
                                   juce::Font::bold));
            g.drawFittedText (h.name, nameArea.toNearestInt(),
                              juce::Justification::centred, 2, 0.85f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloPairPanel)
};

//==============================================================================
// FunkeyMixPanel - what the small "E" on the FUNKEY button opens: one MIX
// slider, 0..100 in 101 steps, in a CallOutBox pointing at the "E".
//
// PER SET, i.e. per style: one value for every funkeyed instrument, blending
// its dry signal with Funkey's finished output (Channel::applyFunkeyInPlace),
// saved in the set and put back with it.  The slider is the sound editors'
// GoldSlider, so it looks and drags like every other slider in the plugin.
//==============================================================================
class FunkeyMixPanel : public juce::Component
{
public:
    FunkeyMixPanel (float value0to100, std::function<void(float)> onChange)
        : slider ("MIX", 0.0f, 100.0f, 50.0f)
    {
        slider.setStep (1.0f);
        slider.setValue (value0to100, false);
        slider.onChange = std::move (onChange);
        addAndMakeVisible (slider);
        setSize (96, 280);
    }

    void resized() override { slider.setBounds (getLocalBounds().reduced (8)); }

private:
    GoldSlider slider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkeyMixPanel)
};

class MainTab : public juce::Component
{
public:
    MainTab();
    ~MainTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ── Transport callbacks (wired by MainComponent) ──────────────────────────
    //
    // onVariationClicked fires AFTER setSelectedVariation() has updated the
    // LED row, so the host doesn't have to worry about visual state.
    std::function<void(int variIndex)> onVariationClicked;
    std::function<void(bool wantsPlay)> onPlayStopToggled;
    std::function<void(bool on)>        onSyncPlayToggled;
    std::function<void()>               onCrashClicked;
    std::function<void()>               onRestartClicked;

    /** Fires when a STYLE ELEMENTS ON/OFF button is clicked.  `on` is the
        new toggle state of that button (true = element ON / unmuted). */
    std::function<void(int slotIndex, bool on)> onStyleElementToggled;

    /** Fires whenever any SOLO ELEMENTS button changes state, passing the
        full 8-bit enable mask (bit N = slot N).  The host routes upper-
        keyboard input to all enabled slots; when mask is 0 it falls back
        to the legacy single-slot routing via activeSoloSlot. */
    std::function<void(juce::uint8 mask)> onSoloMaskChanged;

    /** Fires when ONPRESS (sync-stop) toggles. */
    std::function<void(bool on)> onSyncStopToggled;

    /** FUNKEY MODE toggled from the play-control row.  It sits here rather than
        on the left panel because it is a PERFORMANCE switch - something you hit
        mid-song beside ONPRESS and CRASH - not a settings toggle you set once
        and forget. */
    std::function<void(bool on)> onFunkeyToggled;

    /** THE FUNKEY MIX moved, 0..100 - the "E" in FUNKEY's top-left corner. */
    std::function<void(float mix0to100)> onFunkeyMixChanged;

    /** Fires when FINGERED / 1 FINGER toggles.  `fingeredMode` = true means
        full-chord recognition; false means Yamaha single-finger mode. */
    std::function<void(bool fingeredMode)> onChordModeToggled;

    /** THE SOLO OCTAVE MACRO asks for a value.  MainComponent decides which
        solo slot that means and writes it to the model - see OctaveMacroPad. */
    std::function<void(int octaves)> onSoloOctave;

    /** NO-OP, kept so the host's per-frame call site does not have to change.

        The status strip it used to write to is gone - it repeated the style
        name, tempo and time signature that the left panel already shows. */
    void setStyleInfo (const juce::String&, float, int, int) {}

    /** Drives the variation-button LEDs as ONE mutually-exclusive selector:
        exactly one of the 16 buttons is lit — the section that is active
        right now — and every other LED is off.  The host guarantees a valid
        index is always passed (never -1), so a button is never blank: while a
        section plays its own button is lit; when stopped the last-played main
        variation's button stays lit.  The LED moves only when the active
        section actually changes. */
    void setCurrentlyPlayingVariation (int activeBtn) noexcept
    {
        for (int i = 0; i < 16; ++i)
            variButtons[i].setPlaying (i == activeBtn);
    }

    // Called by MainComponent when Single/Multi selector changes
    void setSoloSingleMode(bool isSingle);

    // Set instrument name shown below channel label
    // Solo channels use MIDI ch 17-24 (indices 0-7)
    void setSoloInstrumentName(int index, const juce::String& name)
    {
        jassert(index >= 0 && index < 8);
        soloButtons[index].setSubLabel(name);
    }

    // Style channels use 8 roles: DRUMS / PERC / BASS / CHORD 1 / CHORD 2 /
    // PAD / LEAD 1 / LEAD 2 (matching SoundsTab::kStyleRoleNames).
    //
    // SAME TREATMENT AS SOLO, and it used to differ for no reason: this wrote a
    // subLabel, which draws a SECOND centred line and squeezes both. The solo
    // buttons had already settled the question - the instrument name is the
    // button's content and keeps the middle, the fixed role goes in the corner
    // tag where it stays put while the name under it changes.
    //
    // (It was also dead: nothing in the tree ever called it, so the style
    // buttons showed their role and never named the sound loaded on them.)
    void setStyleInstrumentName(int index, const juce::String& name)
    {
        jassert(index >= 0 && index < 8);
        styleButtons[index].setButtonText(name);
    }

    // ── Accessors for engine hookup ───────────────────────────────────────────
    //==========================================================================
    // REGISTER THIS TAB'S CONTROLS FOR MIDI ASSIGNMENT.
    //
    // The tab does it rather than MainComponent reaching in, for one reason: the
    // id belongs beside the widget. A mapping table kept somewhere else drifts
    // the first time a button is renamed or moved, and drifts SILENTLY - the
    // wrong control simply becomes assignable and nobody notices until a pad
    // fires the wrong thing.
    //==========================================================================
    void attachRemotes (Betel::RemoteAssignHub& hub)
    {
        using R = Betel::RemoteId;

        for (int i = 0; i < 16; ++i)
            hub.attach (variButtons[(size_t) i], (R) ((int) R::Intro1 + i));

        for (int i = 0; i < 8; ++i)
        {
            hub.attach (styleButtons[(size_t) i], (R) ((int) R::Element1 + i));
            hub.attach (soloButtons [(size_t) i], (R) ((int) R::Solo1    + i));
        }

        hub.attach (btnPlayStop, R::PlayStop);
        hub.attach (btnRestart,  R::Restart);
        hub.attach (btnSyncPlay, R::SyncPlay);
        hub.attach (btnOnPress,  R::OnPress);
        hub.attach (btnCrash,    R::Crash);
        hub.attach (btnFunkey,   R::FunkeyMode);
        hub.attach (btnFingered, R::Fingered);
    }

    //==========================================================================
    // RECORD AND SONG PLAYER HAVE LEFT THIS COMPONENT.
    //
    // They were the eighth play-control slot, split at its mid-Y. They now sit
    // in their own painted area on the LEFT PANEL, which is outside this tab's
    // canvas entirely - so they are MainComponent's children now, along with
    // RecordState, setRecordState/getRecordState and setSongActive.
    //
    // The play-control row went 8 slots -> 7 and every button in it got wider
    // as a result; see MainTab::resized.
    //
    // setSongMode below no longer disables the record button either. MainComponent
    // does that beside its own two buttons, through setSongModeLocksRecord.
    //==========================================================================

    //==========================================================================
    // SONG MODE: the band is the song's, the RIGHT HAND is still yours.
    //
    // The whole MainComponent used to be setEnabled(false) while the song
    // window was open, which was right about the band and wrong about the
    // player: the solo selectors choose which slots the keyboard reaches, and
    // over a backing track that is the one thing you most want to change
    // mid-song. A blanket disable on the parent cannot be undone by a child -
    // JUCE's isEnabled() walks up - so the disable has to be selective from the
    // start.
    //
    // DISABLED: everything that would move the band under the song - the style
    // row, the sixteen variations, and the transport.
    // LEFT LIVE: the eight solo selectors. Pitch bend, modulation and sustain
    // never needed anything here; they are handled processor-side and never
    // pass through this component at all.
    //==========================================================================
    void setSongMode (bool on)
    {
        for (auto& b : styleButtons) b.setEnabled (! on);
        for (auto& b : variButtons)  b.setEnabled (! on);

        btnPlayStop.setEnabled (! on);
        btnSyncPlay.setEnabled (! on);
        btnOnPress .setEnabled (! on);
        btnCrash   .setEnabled (! on);
        btnFingered.setEnabled (! on);
        btnRestart .setEnabled (! on);
        // btnRecord is MainComponent's now - it disables it on the same signal.

        // Solo row deliberately untouched - see above.
    }

    /** Model -> pads.  Called from MainComponent's 30 Hz mirror with the
        selected solo slot's own octave. */
    void setSoloOctaveDisplay (int octaves)  { octavePad.setOctave (octaves); }

    /** Model -> the MANUAL BASS | HARMONY display that replaced SOLO 7 and 8.
        Called from the same 30 Hz mirror. */
    void setSoloPairState (bool bassLit,    const juce::String& bassName,
                           bool harmonyLit, const juce::String& harmonyName)
    {
        soloPair.setSection (SoloPairPanel::kBass,    bassLit,    bassName);
        soloPair.setSection (SoloPairPanel::kHarmony, harmonyLit, harmonyName);
    }

    LedButton& getSoloButton(int i)  { jassert(i >= 0 && i < 8);  return soloButtons[i]; }
    LedButton& getStyleButton(int i) { jassert(i >= 0 && i < 8);  return styleButtons[i]; }
    LedButton& getVariButton(int i)  { jassert(i >= 0 && i < 16); return variButtons[i]; }
    LedButton& getBtnSyncPlay()      { return btnSyncPlay; }
    LedButton& getBtnPlayStop()      { return btnPlayStop; }
    LedButton& getBtnOnPress()       { return btnOnPress;  }
    LedButton& getBtnCrash()         { return btnCrash;    }
    LedButton& getBtnFingered()      { return btnFingered; }
    LedButton& getBtnRestart()       { return btnRestart;  }

    int  getSelectedVariation() const { return selectedVariation; }
    void setSelectedVariation(int i, bool notify = false);

    /** Which of the 16 variation buttons the loaded style can actually play.

        A style is not obliged to carry all fifteen sections - two intros
        instead of three is common, and plenty have no break or only one
        ending.  Those buttons used to look identical to the working ones and
        simply did nothing when pressed, which reads as a broken plugin rather
        than as a style that stops at INTRO 2.

        setEnabled does double duty: it blocks the click AND is what
        LedButton::paintButton reads to draw the dimmed state, so availability
        has one owner instead of a flag that could disagree with the button. */
    void setVariationAvailable (int idx, bool available)
    {
        if (idx < 0 || idx >= 16) return;
        if (variButtons[(size_t) idx].isEnabled() == available) return;
        variButtons[(size_t) idx].setEnabled (available);
        variButtons[(size_t) idx].repaint();
    }

    /** No style loaded: everything is playable again, so the panel does not sit
        greyed out between loads. */
    void setAllVariationsAvailable()
    {
        for (int i = 0; i < 16; ++i) setVariationAvailable (i, true);
    }

    /** Force-set the PLAY/STOP visual state without firing the callback.
        Used by the host when the sequencer transport changes from a non-UI
        source (e.g. external MIDI start). */
    void setPlayStopVisual (bool playing) noexcept
    {
        btnPlayStop.setToggleState (playing, juce::dontSendNotification);
        btnPlayStop.setButtonText  (playing ? "STOP" : "PLAY");
    }

    /** Relight FUNKEY without firing the callback.  The Settings page carries
        the same switch, so whichever surface is used the other has to follow -
        and it must not bounce back through onFunkeyToggled and toggle it again. */
    void setFunkeyVisual (bool on) noexcept
    { btnFunkey.setToggleState (on, juce::dontSendNotification); }

    /** What the FUNKEY MIX pop-up opens at, 0..100.  Set by the host from the
        processor's stored value; the pop-up keeps it current as it moves. */
    void setFunkeyMixValue (float v0to100) noexcept
    { funkeyMixValue = juce::jlimit (0.0f, 100.0f, v0to100); }

private:
    // ── Section labels ────────────────────────────────────────────────────────
    juce::Label lblSolo, lblStyle, lblVariations, lblPlayControl;
    juce::Label lblSoloOctave;      // title over the eighth column - see resized()

    // ── Solo elements (8) — MIDI ch 17-24 ────────────────────────────────────
    std::array<LedButton, 8>  soloButtons;
    bool soloSingleMode = true;

    // SOLO 7 and 8's display - covers the two hidden cells.  See SoloPairPanel.
    SoloPairPanel soloPair;

    // ── Style elements (8) — the 8 fixed Yamaha-arranger roles ───────────────
    // DRUMS / PERC / BASS / CHORD 1 / CHORD 2 / PAD / LEAD 1 / LEAD 2
    std::array<LedButton, 8> styleButtons;

    // ── Variations (16 selector buttons, always one active) ───────────────────
    std::array<LedButton, 16> variButtons;

    int selectedVariation = 4;    // default: VAR 1 (index 4)

    // ── Play controls ─────────────────────────────────────────────────────────
    LedButton btnSyncPlay { "SYNC\nPLAY",  LedButton::Mode::Toggle };
    LedButton btnPlayStop { "PLAY",        LedButton::Mode::Toggle };
    LedButton btnOnPress  { "ONPRESS",     LedButton::Mode::Toggle };
    LedButton btnFunkey   { "FUNKEY",      LedButton::Mode::Toggle };

    // The "E" in FUNKEY's top-left corner - opens the MIX pop-up.  A SIBLING
    // laid OVER the button (see resized), not a child of it, and added after
    // it so it sits on top: a click on the "E" never toggles Funkey.
    juce::TextButton btnFunkeyMix { "E" };
    float funkeyMixValue = 50.0f;     // 0..100, what the pop-up opens at
    void  showFunkeyMix();
    LedButton btnCrash    { "CRASH",       LedButton::Mode::Push   };
    LedButton btnFingered { "1 FINGER",    LedButton::Mode::Toggle };
    LedButton btnRestart  { "RESTART",     LedButton::Mode::Push   };

    // ── Solo octave macro - the eighth column of the PLAY CONTROL row ─────────
    OctaveMacroPad octavePad;

    // ── Layout helpers ────────────────────────────────────────────────────────
    void layoutRow(juce::Component** items, int count, int x, int y, int w, int h, int gap);
    static void initSectionLabel(juce::Label& lbl, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainTab)
};
