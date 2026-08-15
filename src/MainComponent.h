


#pragma once
#include <JuceHeader.h>

#include "RectangleKnob.h"
#include "InstrEditPanel.h"   // InstrEditStyle::kPanelLabel
#include "SelectorGroup.h"
#include "FinisherWindow.h"
#include "SpaceAnimation.h"

// Tabs
#include "MainTab.h"
#include "StylesTab.h"
#include "StyleDataWindow.h"
#include "SoundsTab.h"
#include "MixerTab.h"
#include "SetEditorTab.h"
#include "MasterSettings.h"   // player-wide chord mode + tempo free/synced
#include "CcMap.h"            // the MIDI CC learn map, its own global preset
#include "SetBaker.h"         // writes the GM starting point into the whole set pack
#include "JumpsTab.h"
#include "CrashTab.h"
#include "SettingsTab.h"
#include "TutorialTab.h"     // the built-in manual, last in the tab row
#include "GrexPopupWindow.h" // stays in front, minimisable, never vanishes
#include "GrexSongRecorder.h"
#include "GrexSongPlayer.h"
#include "GrexTransportBar.h"
#include "FolderLocatorLED.h"

class BetelgeuseProcessor;  // forward-declared; full def comes via Main.h in .cpp

// =====================================================================================
//  BetelButton – transparent overlay, only draws text (back.png has the bg)
// =====================================================================================
class BetelButton : public juce::TextButton
{
public:
    BetelButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isButtonDown || getToggleState())
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        g.setColour(textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 14.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  BlinkButton – transparent normally, blinks orange for 200ms on click
// =====================================================================================
class BlinkButton : public juce::TextButton, private juce::Timer
{
public:
    BlinkButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (blinking)
            g.setColour(juce::Colour(0xFFCC6600));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        g.setColour(blinking ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 14.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        blinking = true;
        repaint();
        startTimer(200);
        juce::TextButton::mouseDown(e);
    }

    /** Hold the amber for longer than the 200 ms press blink.  Used to confirm
        FAST SAVE: the whole point of that button is that nothing opens, so the
        button itself has to be the receipt. */
    void flash(int ms)
    {
        blinking = true;
        repaint();
        startTimer(ms);
    }

private:
    void timerCallback() override
    {
        blinking = false;
        stopTimer();
        repaint();
    }

    bool blinking = false;
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  Separator between the three stacked buttons that read as one control
//  (COMMENTS / ORIENTAL SCALE / DAW START).
//
//  Grey rather than white and drawn as a sub-pixel fillRect rather than a
//  1-px line: it is a hairline dividing parts of ONE group, not a border
//  between three controls, and at white/0.5 it read as the latter.
// =====================================================================================
inline void drawGroupSeparator (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colour (0xFF9A9A9A).withAlpha (0.30f));
    g.fillRect (bounds.getX() + 6.0f, bounds.getY(), bounds.getWidth() - 12.0f, 0.6f);
}

// =====================================================================================
//  BlinkSeparatorButton – blinks orange on click + hairline separator on top edge
// =====================================================================================
class BlinkSeparatorButton : public juce::TextButton, private juce::Timer
{
public:
    BlinkSeparatorButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }
    void setDrawTopSeparator(bool draw) { drawTopSep = draw; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (blinking)
            g.setColour(juce::Colour(0xFFCC6600));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        if (drawTopSep) drawGroupSeparator(g, bounds);

        g.setColour(blinking ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 13.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        blinking = true;
        repaint();
        startTimer(200);
        juce::TextButton::mouseDown(e);
    }

private:
    void timerCallback() override
    {
        blinking = false;
        stopTimer();
        repaint();
    }

    bool blinking = false;
    bool drawTopSep = false;
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  DawStartButton – toggle, fully transparent when off, orange fill when on
// =====================================================================================
class DawStartButton : public juce::TextButton
{
public:
    DawStartButton(const juce::String& text = {}) : juce::TextButton(text)
    {
        setClickingTogglesState(true);
    }

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (getToggleState())
            g.setColour(juce::Colour(0xFFCC6600));
        else if (isButtonDown)
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        drawGroupSeparator(g, bounds);

        // BLACK on the lit (orange) state — see TabButton.
        g.setColour(getToggleState() ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 13.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  TabButton – tab selector button (full orange when active, not dimmed)
// =====================================================================================
class TabButton : public juce::TextButton
{
public:
    TabButton(const juce::String& text = {}) : juce::TextButton(text)
    {
        setClickingTogglesState(false);
    }

    void setActive(bool active) { isActive = active; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isActive)
            g.setColour(juce::Colour(0xFFCC6600));
        else if (isButtonDown)
            g.setColour(juce::Colour(0x40FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x18FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        // BLACK on the lit (orange) state: white on #CC6600 is a poor read, and
        // black is what "engaged" looks like everywhere else in the plugin.
        g.setColour(isActive ? juce::Colours::black : juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.40f, 14.0f), juce::Font::bold));
        // drawFittedText, not drawText: these labels are two lines ("BIG DRUMS /
        // MODE"), and drawText renders a single one.
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(3),
                         juce::Justification::centred, 2);
    }

private:
    bool isActive = false;
};

// =====================================================================================
//  BetelTextLabel – text only, no background
// =====================================================================================
class BetelTextLabel : public juce::Component
{
public:
    BetelTextLabel(const juce::String& text = {}, float fontSize = 14.0f,
                   juce::Colour textCol = InstrEditStyle::kPanelLabel)
        : displayText(text), fSize(fontSize), colour(textCol)
    {
        setOpaque(false);
    }

    void setText(const juce::String& t) { displayText = t; repaint(); }
    juce::String getText() const { return displayText; }
    void setFontSize(float s) { fSize = s; repaint(); }
    void setTextColour(juce::Colour c) { colour = c; repaint(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(colour);
        g.setFont(juce::Font(fSize, juce::Font::bold));
        g.drawFittedText(displayText, getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::String displayText;
    float fSize;
    juce::Colour colour;
};

// =====================================================================================
//  HoldButton – toggle, NO text, orange LED circle
// =====================================================================================
class HoldButton : public juce::TextButton
{
public:
    HoldButton() : juce::TextButton(juce::String())
    {
        setClickingTogglesState(true);
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isButtonDown)
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        const float ledDiameter = juce::jmin(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.25f);
        const float ledX = bounds.getCentreX() - ledDiameter * 0.5f;
        const float ledY = bounds.getY() + bounds.getHeight() * 0.15f;

        g.setColour(getToggleState() ? juce::Colour(0xFFCC6600) : juce::Colour(0xFF666666));
        g.fillEllipse(ledX, ledY, ledDiameter, ledDiameter);

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(ledX + ledDiameter * 0.2f, ledY + ledDiameter * 0.15f,
                       ledDiameter * 0.35f, ledDiameter * 0.35f);
    }
};

// =====================================================================================
//  RegistrationLED – rounded circle, green = registered, red = unregistered
// =====================================================================================
class RegistrationLED : public juce::Component
{
public:
    RegistrationLED() { setOpaque(false); }

    void setRegistered(bool reg) { registered = reg; repaint(); }
    bool isRegistered() const { return registered; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto circle = bounds.withSizeKeepingCentre(diameter, diameter);

        g.setColour(registered ? juce::Colour(0xFF00CC00) : juce::Colour(0xFFCC0000));
        g.fillEllipse(circle);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillEllipse(circle.reduced(diameter * 0.25f).translated(-diameter * 0.08f, -diameter * 0.08f));
    }

private:
    bool registered = false;
};

// =====================================================================================
//  SplitKnob – compact knob with left-aligned triangle pair + value
// =====================================================================================
class SplitKnob : public juce::Component
{
public:
    SplitKnob(double minVal = 36.0, double maxVal = 127.0, double defaultVal = 60.0)
        : minValue(minVal), maxValue(maxVal), value(defaultVal), defaultValue(defaultVal)
    {
        setOpaque(false);
        setRepaintsOnMouseActivity(true);
    }

    void setValue(double newVal, bool notify = true)
    {
        newVal = juce::jlimit(minValue, maxValue, std::round(newVal));
        if (newVal != value)
        {
            value = newVal;
            repaint();
            if (notify && onChange)
                onChange(value);
        }
    }

    double getValue() const { return value; }

    std::function<void(double)> onChange;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        const float triSize = juce::jmin(bounds.getHeight() * 0.25f, 6.0f);
        const float centreY = bounds.getCentreY();
        const float lx = bounds.getX() + 10.0f + triSize * 0.5f;

        g.setColour(InstrEditStyle::kPanelLabel.withAlpha(0.85f));

        juce::Path upTri;
        upTri.addTriangle(lx - triSize * 0.5f, centreY - 1.5f,
                          lx + triSize * 0.5f, centreY - 1.5f,
                          lx,                  centreY - 1.5f - triSize);
        g.fillPath(upTri);

        juce::Path downTri;
        downTri.addTriangle(lx - triSize * 0.5f, centreY + 1.5f,
                            lx + triSize * 0.5f, centreY + 1.5f,
                            lx,                  centreY + 1.5f + triSize);
        g.fillPath(downTri);

        auto textArea = bounds.withLeft(lx + triSize + 2.0f);
        g.setColour(InstrEditStyle::kPanelLabel);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.60f, 16.0f), juce::Font::bold));
        g.drawText(juce::String((int)value), textArea, juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartY = e.y;
        dragStartValue = value;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const double pxPerStep = e.mods.isShiftDown() ? 8.0 : 3.0;
        const double dy = (double)(dragStartY - e.y);
        setValue(dragStartValue + dy / pxPerStep);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        setValue(defaultValue);
    }

private:
    double minValue, maxValue, value, defaultValue;
    int dragStartY = 0;
    double dragStartValue = 0.0;
};

// =====================================================================================
//  Custom MidiKeyboard: dark gray white keys, grayish-white black keys
// =====================================================================================
class BetelMidiKeyboard : public juce::MidiKeyboardComponent
{
public:
    BetelMidiKeyboard(juce::MidiKeyboardState& state, Orientation orientation)
        : juce::MidiKeyboardComponent(state, orientation) {}

    void setSplitPoint(int noteNumber)
    {
        splitNote = noteNumber;
        repaint();
    }

    // In piano mode the whole keyboard is one melodic range — no chord zone,
    // so the bronze split tint is suppressed.
    void setPianoMode(bool p)
    {
        pianoMode = p;
        repaint();
    }

    void drawWhiteNote(int midiNoteNumber, juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown, bool isOver,
                       juce::Colour /*lineColour*/, juce::Colour /*textColour*/) override
    {
        auto col = juce::Colour(0xFF3A3A3A);
        if (isDown)       col = col.brighter(0.3f);
        else if (isOver)  col = col.brighter(0.1f);

        g.setColour(col);
        g.fillRect(area);

        if (!pianoMode && midiNoteNumber < splitNote)
        {
            g.setColour(juce::Colour(0x50CC6600));
            g.fillRect(area);
        }

        g.setColour(juce::Colour(0xFF2A2A2A));
        g.drawRect(area, 1.0f);
    }

    void drawBlackNote(int midiNoteNumber, juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown, bool isOver,
                       juce::Colour /*noteFillColour*/) override
    {
        auto col = juce::Colour(0xFFBBBBBB);
        if (isDown)       col = col.darker(0.3f);
        else if (isOver)  col = col.darker(0.1f);

        g.setColour(col);
        g.fillRect(area);

        if (!pianoMode && midiNoteNumber < splitNote)
        {
            g.setColour(juce::Colour(0x50CC6600));
            g.fillRect(area);
        }

        g.setColour(juce::Colour(0xFF999999));
        g.drawRect(area, 1.0f);
    }

private:
    int  splitNote = 60;
    bool pianoMode = false;
};

// =====================================================================================
//  CommentsWindow — free-floating notepad for the current set.  Text is held in
//  the processor and written out when the set/state is saved.
// =====================================================================================
class CommentsWindow : public juce::DocumentWindow
{
public:
    std::function<void(const juce::String&)> onSave;   // fired by SAVE button
    std::function<void()>                    onClose;  // fired when window closes

    explicit CommentsWindow (const juce::String& initialText)
        : juce::DocumentWindow ("Comments", juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (true);
        auto* c = new Content (initialText);
        c->onSave  = [this] (const juce::String& t) { if (onSave)  onSave (t);  };
        setContentOwned (c, true);
        centreWithSize (440, 320);
        setResizable (true, false);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

private:
    struct Content : public juce::Component
    {
        juce::TextEditor editor;
        juce::TextButton clearBtn { "CLEAR ALL" }, saveBtn { "SAVE" };
        std::function<void(const juce::String&)> onSave;

        explicit Content (const juce::String& init)
        {
            editor.setMultiLine (true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setScrollbarsShown (true);
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF101010));
            editor.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
            editor.setText (init, juce::dontSendNotification);
            addAndMakeVisible (editor);

            for (auto* b : { &clearBtn, &saveBtn })
            {
                b->setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (*b);
            }
            clearBtn.onClick = [this] { editor.clear(); editor.grabKeyboardFocus(); };
            saveBtn .onClick = [this] { if (onSave) onSave (editor.getText()); };
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto bottom = r.removeFromBottom (32);
            editor.setBounds (r.withTrimmedBottom (6));
            clearBtn.setBounds (bottom.removeFromLeft (120));
            bottom.removeFromLeft (8);
            saveBtn .setBounds (bottom.removeFromLeft (120));
        }
    };
};

// =====================================================================================
//  MainComponent
// =====================================================================================
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit MainComponent(BetelgeuseProcessor& proc);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;          // 30 Hz transport-sync poll
    const Betel::StyleData* lastSeenStylePtr = nullptr;
    juce::String            lastChordText;

    static constexpr int kDesignW = 1600;
    static constexpr int kDesignH = 853;

    BetelgeuseProcessor&     processor;
    juce::MidiKeyboardState& keyboardState;  // reference into processor

    juce::Image background;
    bool debugLayout = false;

    // ── Header animation ──────────────────────────────────────────────────────
    SpaceAnimationComponent headerAnimation;

    // ── Left panel controls ───────────────────────────────────────────────────
    RectangleKnob         knobTempo { "TEMPO", 30.0, 300.0, 120.0, 1.0 };
    SelectorGroup         selectorSpeed { {"x1","x2","x1/2"}, 0 };
    SelectorGroup         selectorTempoType { {"FREE","SYNCED"}, 0 };

    BlinkButton           btnTap        { "TAP" };
    BlinkButton           btnResetTempo { "RESET\nTEMPO" };

    SelectorGroup         selectorArrangerPiano { {"ARRANGER","PIANO"}, 0 };
    SelectorGroup         selectorSingleMulti   { {"SINGLE","MULTI"}, 0 };
    SelectorGroup         selectorTransition    { {"1/2","1/4","1/8"}, 0 };
    SelectorGroup         selectorFillLength    { {"1","1/2"}, 0 };

    // FINISHER editor — created lazily the first time the mixer's FINISHER
    // button is pressed, like SoundsTab's drumsPopup.
    std::unique_ptr<FinisherWindow> finisherWindow;
    void openFinisherWindow();
    void refreshFinisherWindow();

    RectangleKnob         knobGlobalSemitone { "SEMITONE", -12.0, 12.0, 0.0, 1.0 };
    BetelTextLabel        lblPlayedChord     { "C", 20.0f };

    // 19.5 = 13 x 1.5.  This is the one line on the panel that answers "what am
    // I playing", so it reads from across a room rather than matching the
    // captions around it.
    BetelTextLabel        lblStyleName { "No Style", 19.5f };

    BlinkButton           btnLoadSet  { "LOAD\nSET" };
    BlinkButton           btnSaveSet  { "SAVE\nSET" };
    // RE-LOAD became FAST SAVE.  Re-loading the last set reverted unsaved
    // edits, which is a rescue for a mistake; saving them without a dialog is
    // something you want after every good one.  The second is worth a front-
    // panel button, the first was not.
    BlinkButton           btnFastSave { "FAST\nSAVE" };

    // Set file I/O (LOAD SET / SAVE SET / FAST SAVE).
    std::unique_ptr<juce::FileChooser> setChooser;
    juce::File                         lastSetFile;

    /** The set that belongs to a style: <root>/sets/<genre>/<style name>.bset,
        mirroring the styles tree exactly.  Selecting a style loads THIS rather
        than the raw style file whenever it exists. */
    juce::File setFileForStyle (const juce::String& styleAbsolutePath) const;

    /** Grey out the variation buttons the LOADED style has no section for.
        Called after every style load; see the definition for the mapping. */
    void refreshVariationAvailability();

    /** Write the current state to a .bset.  Shared by SAVE SET and FAST SAVE so
        the two can never drift into saving different things. */
    bool writeSetToFile (const juce::File& file);

    /** Pull every mixer control back from the processor after a set load.  The
        per-slot faders are re-synced by the timer when the style pointer moves,
        but the buses, the master, the boost and the Finisher switch are not
        polled by anything — without this they would sit at the previous song's
        positions while the engine ran the new one's. */
    void refreshMixerFromProcessor();

    /** Pull every crash control back from the processor after a set load. */
    void refreshCrashFromProcessor();

    /** The name the left panel shows for what is playing: the SET's file
        name, falling back to the style FILE's — never the machine-generated
        string stored inside the style. */
    juce::String currentSetDisplayName() const;

    /** The set-pack baker.  Owned here because it outlives a single button
        press — it steps through the library on a timer so the editor stays
        responsive.  Constructed with the processor's bank map, which is what
        resolves a style's slot programs. */
    std::unique_ptr<Betel::SetBaker> setBaker;
    void applySetFromFile (const juce::File& file);
    void applySetPayload  (const juce::ValueTree& payload);
    juce::ValueTree buildSetSnapshot();

    BlinkSeparatorButton  btnComments  { "COMMENTS" };
    BlinkSeparatorButton  btnForgetAll { "ORIENTAL\nSCALE" };   // quick Arabic-scale editor
    DawStartButton        btnDawStart  { "DAW\nSTART" };

    BetelTextLabel        lblSetName { "No Set", 13.0f };

    // ── Global macros (under the set manager) ─────────────────────────────────
    // BIG DRUMS and FUNKEY MODE.  TabButton is reused for its lit/unlit state:
    // these are toggles, not momentary actions, and the lit look is exactly the
    // "this is engaged" signal the tab bar already uses.
    //
    // The buttons only flip the flags in GlobalMacros and ask SoundsTab to
    // re-commit; all the actual FX swapping lives there, so nothing in this
    // class needs to know what the macros do.
    TabButton             btnBigDrums   { "BIG DRUMS\nMODE" };
    TabButton             btnFunkeyMode { "FUNKEY\nMODE" };
    void refreshMacroButtons();

    /** Put FUNKEY MODE and BIG DRUMS where the given UiState says, defaulting
        BOTH OFF when it does not say — then relight the buttons and make the
        result audible.  Pass an invalid tree to mean "nothing declares them",
        which is a bare style load.  See the definition for why the default is
        false rather than the current value. */
    void applyMacroEngagedState (const juce::ValueTree& uiState);

    // ── Right panel ───────────────────────────────────────────────────────────
    // NINE tabs since TUTORIAL was added.  Nothing else needs touching for a
    // new tab: resized() divides the selector canvas by kNumTabs in float math,
    // so the nine buttons redistribute evenly by themselves, and selectTab
    // works off the same count.  TUTORIAL goes LAST on purpose — appending
    // leaves every existing tab index where it was.
    static constexpr int kNumTabs = 9;
    std::array<TabButton, kNumTabs> tabButtons;
    int activeTabIndex = 0;

    juce::Component tabContentContainer;
    MainTab      mainTab;
    StylesTab    stylesTab;
    SoundsTab    soundsTab;
    MixerTab     mixerTab;
    SetEditorTab setEditorTab;
    JumpsTab     jumpsTab;
    CrashTab     crashTab;
    SettingsTab  settingsTab;
    TutorialTab  tutorialTab;
    std::array<juce::Component*, kNumTabs> tabPages {};

    // ── MIDI song transport (record / play) ───────────────────────────────────
    GrexTransportBar transportBar;
    GrexSongRecorder::Setup captureSongSetup() const;   // snapshot for arm()
    void applySongSetup (const GrexSongRecorder::Setup&); // restore before play

    // ── Global style-search bar (fixed strip under the tab menu) ───────────────
    juce::TextEditor styleSearchBox;
    juce::TextButton btnStyleSearch  { "SEARCH" };
    juce::TextButton btnStyleSearchClear { "CLEAR" };
    void runStyleSearch();
    void clearStyleSearch();

    BetelMidiKeyboard keyboardComponent;
    SplitKnob         knobSplit { 36.0, 127.0, 60.0 };
    HoldButton        btnHold;
    RegistrationLED   ledRegistration;
    juce::Label       lblReg;
    FolderLocatorLED  ledFolderLocator;
    juce::TextButton  btnLocateFolder;

    // ── Helpers ───────────────────────────────────────────────────────────────
    float getUiScale() const;
    juce::Rectangle<int> getUiTargetRect() const;
    juce::Rectangle<int> scaleRect(const juce::Rectangle<int>& r) const;
    // Float variant — keeps sub-pixel design coordinates exact until the single
    // rounding at the end, instead of truncating them at the call site.
    juce::Rectangle<int> scaleRectF(const juce::Rectangle<float>& r) const;

    void selectTab(int index);

    // Quick Arabic/oriental scale editor (ORIENTAL SCALE button) -- pops the
    // ArabicScaleKeyboard in its own window; edits apply to all solo channels.
    std::unique_ptr<juce::DocumentWindow> arabicQuickWin;
    void toggleArabicQuickEditor();
    void drawDebugOverlay(juce::Graphics& g);

    // ── Styles browser state ──────────────────────────────────────────────────
    // Selecting a style cell caches its absolute path here; LOAD / RELOAD load
    // it via processor.loadStyle.
    juce::String pendingStylePath;

    // Scan <root>/styles sub-folders into the genre + style grids.
    void rescanStyleBrowser();
    // Rebuilds the reference-voice pill library from the engine's per-instrument
    // flag library (Reference = flags >= 200, GM = flags 0..127).
    void refreshReferenceLibrary();

    std::unique_ptr<CommentsWindow> commentsWindow;
    std::unique_ptr<Betel::StyleDataWindow> styleDataWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};



