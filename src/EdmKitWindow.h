#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <vector>
#include "SynthKits.h"
#include "EdmKitFiles.h"
#include "GrexPopupWindow.h"
#include "BalladaPalette.h"
#include "InstrEditPanel.h"          // GoldSlider - the sound editors' vertical slider
#include "DrumSplitEq.h"             // the EQ's slider rule: silence .. unity .. +24 dB

//==============================================================================
//  EdmKitWindow.h  —  the EDM KIT editor.  EDIT on an EDM Kit slot opens it.
//
//  An EDM kit is 128 key cells (SynthKits.h).  This window edits them:
//      KIT      one of the six factory kits, or RELOAD FACTORY to drop edits
//      GRID     keys 12..107, one octave per row, each pad showing its sound
//               in its family colour.  Click a pad = play it.  Its E button =
//               open it in the editor below.  Its LED = the MIDI truth: lit
//               from note-on to note-off, whether a pad hit or the style.
//      CELL     family, sound, choke group, and the slider mixer under a tab
//               selector: MAIN (the eight macros and gain) plus the voice's own
//               tabs - every value that shapes the sound (SynthParams.h).  All
//               as the sound editors' vertical GoldSliders, 0..200 (201 steps).
//               Each tab's "i" explains what its sliders shape.
//      HIT      plays the selected key; height sets velocity
//
//  Every change is LIVE: the window hands SoundsTab what the user changed -
//  as SHIFTS from the base kit (edm::computeShifts) - and SoundsTab publishes
//  it on the slot exactly like a kit load, so the next hit plays the edit and
//  the set saves it.  The base kit is the developer's .dsin (EdmKitFiles.h).
//  Ringing hits are never cut by an edit (Channel swaps cells only when
//  already in EDM KIT).
//
//  BASE KITS: 15 quick clicks on the EDM KIT title open the base-kit saver,
//  which writes the kit as it now sounds to its .dsin.  Every build has it.
//
//  Message-thread only.  Owns a working copy of the kit; the engine gets its
//  own copy on every publish, so nothing here is shared with the audio thread.
//==============================================================================

namespace EdmUi
{
    inline const juce::StringArray& families()
    {
        // The engine's list (SynthKits.h): the pad colours, the family box and
        // the families' FX racks are the same fifteen, in the same order.
        static const juce::StringArray f = []
        {
            juce::StringArray a;
            for (int i = 0; i < edm::kNumFamilies; ++i)
                a.add (edm::familyName (i));
            return a;
        }();
        return f;
    }

    inline juce::Colour familyColour (const juce::String& fam)
    {
        if (fam == "Kick")       return juce::Colour (0xFFB87A36);
        if (fam == "Tom")        return juce::Colour (0xFFC0623A);
        if (fam == "Snare")      return juce::Colour (0xFF3A8FB8);
        if (fam == "Side Stick") return juce::Colour (0xFF3AB8A8);
        if (fam == "Clap")       return juce::Colour (0xFFB83A7A);
        if (fam == "Hat")        return juce::Colour (0xFF6AB83A);
        if (fam == "Crash")      return juce::Colour (0xFF8A5AC8);
        if (fam == "Ride")       return juce::Colour (0xFF5A7AC8);
        if (fam == "Hand Drum")  return juce::Colour (0xFFA8743A);
        if (fam == "Shaker")     return juce::Colour (0xFF8AA83A);
        if (fam == "Wood")       return juce::Colour (0xFF9A6A48);
        if (fam == "Scrape")     return juce::Colour (0xFF7A8A6A);
        if (fam == "Metal")      return juce::Colour (0xFFB8A83A);
        if (fam == "Whistle")    return juce::Colour (0xFF3AA8B8);
        if (fam == "FX")         return juce::Colour (0xFFC83A5A);
        return juce::Colour (0xFF505050);
    }

    // Yamaha naming: C3 = 60, so key 12 = C-1
    inline juce::String noteName (int key)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[((key % 12) + 12) % 12]) + juce::String (key / 12 - 2);
    }

    inline const char* articulationName (int a)
    {
        return a == 0 ? "closed" : (a == 1 ? "pedal" : "open");
    }

    inline juce::String soundLabel (const edm::Sound& s)
    {
        if (s.isEmpty()) return {};
        juce::String t (s.name());
        // Articulation is a hi-hat idea: shakers ride the hat voice but are
        // always "closed", so they never show it.
        if (s.type == edm::VoiceType::Hat)
        {
            const int idx = edm::findSound (s.name(), s.articulation);
            if (idx >= 0 && juce::String (edm::library()[(size_t) idx].family) == "Hat")
                t << " (" << articulationName (s.articulation) << ")";
        }
        return t;
    }

    // Library family of a sound (by name + articulation), "" for an empty cell
    inline juce::String familyOf (const edm::Sound& s)
    {
        if (s.isEmpty()) return {};
        const int idx = edm::findSound (s.name(), s.articulation);
        return idx >= 0 ? juce::String (edm::library()[(size_t) idx].family) : juce::String();
    }
}

//==============================================================================
//  The key grid: 8 rows x 12 columns = keys 12..107.  Every pad:
//      click on the pad      plays the key, like hitting it
//      E  (top right)        opens the key in the editor below - no sound
//      LED (left of the E)   lit while the key sounds, from a pad hit OR the
//                            style playing it
//==============================================================================
class EdmKeyGrid : public juce::Component
{
public:
    static constexpr int kFirstKey = 12;
    static constexpr int kRows     = 8;
    static constexpr int kCols     = 12;

    std::function<void (int key, int velocity)> onPress;
    std::function<void (int key)>               onRelease;
    std::function<void (int key)>               onEdit;

    void setKit (const edm::Kit* k)   { kit = k; repaint(); }
    void setSelected (int key)        { selected = key; repaint(); }

    // Keys sounding now (bit n = key n).  Repaints only the LEDs that changed.
    void setActivity (const uint32_t m[4])
    {
        for (int key = kFirstKey; key < kFirstKey + kRows * kCols; ++key)
        {
            const bool now = ((m[key >> 5] >> (key & 31)) & 1u) != 0;
            if (now != isLit (key))
                repaint (ledRect (key).expanded (4.0f).getSmallestIntegerContainer());
        }
        for (int i = 0; i < 4; ++i)
            lit[(size_t) i] = m[i];
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF141414));

        // Column headers and octave labels
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (juce::Colour (0xFF8A8A8A));
        for (int c = 0; c < kCols; ++c)
        {
            auto r = cellRect (kFirstKey + c).withY (0.0f).withHeight ((float) kHeaderH);
            g.drawText (EdmUi::noteName (c).dropLastCharacters (1), r, juce::Justification::centred);
        }
        for (int row = 0; row < kRows; ++row)
        {
            auto r = cellRect (kFirstKey + row * kCols).withX (0.0f).withWidth ((float) kLabelW - 4.0f);
            g.drawText ("C" + juce::String (row - 1), r, juce::Justification::centredRight);
        }

        for (int key = kFirstKey; key < kFirstKey + kRows * kCols; ++key)
        {
            const auto r = padRect (key);
            const edm::Sound* s = (kit != nullptr) ? &kit->cells[(size_t) key].sound : nullptr;
            const bool sounding = (s != nullptr && ! s->isEmpty());
            const auto fam  = sounding ? EdmUi::familyOf (*s) : juce::String();
            const auto base = sounding ? EdmUi::familyColour (fam).withAlpha (0.42f)
                                       : juce::Colour (0xFF1E1E1E);
            g.setColour (base);
            g.fillRoundedRectangle (r, 4.0f);
            if (key == pressedKey)
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (r, 4.0f);
            }
            if (key == selected)
            {
                g.setColour (juce::Colour (Betel::Pal::kAccentLight));
                g.drawRoundedRectangle (r, 4.0f, 2.0f);
            }
            else
            {
                g.setColour (juce::Colour (0xFF2C2C2C));
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
            }

            auto inner = r.reduced (4.0f, 2.0f);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.setColour (juce::Colour (0xFF9A9A9A));
            g.drawText (juce::String (key), inner.removeFromTop (12.0f), juce::Justification::topLeft);
            if (sounding)
            {
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.setColour (juce::Colour (0xFFEAEAEA));
                g.drawFittedText (EdmUi::soundLabel (*s), inner.toNearestInt(),
                                  juce::Justification::centred, 2, 0.8f);
            }

            // E - edit this pad (white button, black E)
            const auto e = editRect (key);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (e, 2.5f);
            if (key == selected)
            {
                g.setColour (juce::Colour (Betel::Pal::kAccent));
                g.drawRoundedRectangle (e.expanded (1.0f), 3.0f, 1.5f);
            }
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
            g.drawText ("E", e, juce::Justification::centred);

            // LED - green while the key sounds
            const auto l  = ledRect (key);
            const bool on = isLit (key);
            if (on)
            {
                g.setColour (juce::Colour (0xFF39FF5A).withAlpha (0.28f));
                g.fillEllipse (l.expanded (2.5f));
            }
            g.setColour (on ? juce::Colour (0xFF39FF5A) : juce::Colour (0xFF173A20));
            g.fillEllipse (l);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (l, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int key = keyAt (e.position);
        if (key < 0) return;
        if (editRect (key).expanded (2.0f).contains (e.position))
        {
            if (onEdit) onEdit (key);            // edit only: no sound
            return;
        }
        pressedKey = key;
        repaint();
        if (onPress) onPress (key, 100);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedKey >= 0 && onRelease) onRelease (pressedKey);
        pressedKey = -1;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int key = keyAt (e.position);
        const bool overEdit = key >= 0 && editRect (key).expanded (2.0f).contains (e.position);
        setMouseCursor (overEdit ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
    }

private:
    static constexpr int kLabelW  = 36;
    static constexpr int kHeaderH = 18;

    juce::Rectangle<float> cellRect (int key) const
    {
        const int idx = key - kFirstKey;
        const int row = idx / kCols, col = idx % kCols;
        const float cw = (float) (getWidth() - kLabelW) / (float) kCols;
        const float ch = (float) (getHeight() - kHeaderH) / (float) kRows;
        return { (float) kLabelW + (float) col * cw, (float) kHeaderH + (float) row * ch, cw, ch };
    }

    juce::Rectangle<float> padRect (int key) const   { return cellRect (key).reduced (1.5f); }

    juce::Rectangle<float> editRect (int key) const
    {
        const auto r = padRect (key);
        const float w = juce::jmin (15.0f, r.getWidth()  * 0.22f);
        const float h = juce::jmin (13.0f, r.getHeight() * 0.40f);
        return { r.getRight() - w - 3.0f, r.getY() + 3.0f, w, h };
    }

    juce::Rectangle<float> ledRect (int key) const
    {
        const auto e = editRect (key);
        const float d = juce::jmin (7.0f, e.getHeight() * 0.6f);
        return { e.getX() - d - 4.0f, e.getCentreY() - d * 0.5f, d, d };
    }

    bool isLit (int key) const noexcept
    {
        return key >= 0 && key < 128 && ((lit[(size_t) (key >> 5)] >> (key & 31)) & 1u) != 0;
    }

    int keyAt (juce::Point<float> p) const
    {
        if (p.x < (float) kLabelW || p.y < (float) kHeaderH) return -1;
        const float cw = (float) (getWidth() - kLabelW) / (float) kCols;
        const float ch = (float) (getHeight() - kHeaderH) / (float) kRows;
        const int col = (int) ((p.x - (float) kLabelW) / cw);
        const int row = (int) ((p.y - (float) kHeaderH) / ch);
        if (col < 0 || col >= kCols || row < 0 || row >= kRows) return -1;
        return kFirstKey + row * kCols + col;
    }

    const edm::Kit*          kit = nullptr;
    int                      selected   = 36;
    int                      pressedKey = -1;
    std::array<uint32_t, 4>  lit {};
};

//==============================================================================
//  HIT pad: plays the selected key; top = hard, bottom = soft.
//==============================================================================
class EdmHitPad : public juce::Component
{
public:
    std::function<void (int velocity)> onHit;
    std::function<void()>              onRelease;

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) juce::jmax (1, getHeight()));
        down = true;
        repaint();
        if (onHit) onHit (juce::jlimit (1, 127, (int) std::lround (30.0f + t * 97.0f)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        down = false;
        repaint();
        if (onRelease) onRelease();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (down ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF232323));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (Betel::Pal::kAccentDeep));
        g.drawRoundedRectangle (r, 8.0f, 1.5f);

        g.setColour (down ? juce::Colour (0xFF141414) : juce::Colour (0xFFEAEAEA));
        g.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
        g.drawText ("HIT", r, juce::Justification::centred);

        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colour (0xFF8A8A8A));
        g.drawText ("top = hard, bottom = soft", r.removeFromBottom (16.0f), juce::Justification::centred);
    }

private:
    bool down = false;
};

//==============================================================================
//  The tab selector above the slider mixer: equal-width tabs across the
//  mixer's full width.  MAIN (the macros) first, then the tabs of the
//  selected key's voice (SynthParams.h).  Each tab's "i" (by its right
//  border) explains what that group of sliders shapes - it never switches.
//==============================================================================
class EdmTabBar : public juce::Component
{
public:
    std::function<void (int index)> onSelect;
    std::function<void (int index)> onInfo;

    void setTabs (const juce::StringArray& names, int sel)
    {
        tabs     = names;
        selected = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), sel);
        repaint();
    }

    juce::String         getTabName (int i) const   { return tabs[i]; }
    juce::Rectangle<int> getTabArea (int i) const   { return tabRect (i).toNearestInt(); }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < tabs.size(); ++i)
        {
            const auto r  = tabRect (i);
            const bool on = (i == selected);
            g.setColour (on ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF232323));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? juce::Colour (Betel::Pal::kAccentLight) : juce::Colour (0xFF333333));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);

            const auto ib = infoRect (i);
            g.setColour (on ? juce::Colour (0xFF141414) : juce::Colour (0xFFBDBDBD));
            g.setFont (juce::Font (juce::FontOptions (11.5f).withStyle ("Bold")));
            g.drawFittedText (tabs[i], r.withRight (ib.getX() - 2.0f).withTrimmedLeft (4.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);

            // i - what this tab's sliders shape (white button, black i)
            g.setColour (juce::Colours::white);
            g.fillEllipse (ib);
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (juce::FontOptions (ib.getHeight() * 0.78f).withStyle ("Bold")));
            g.drawText ("i", ib.translated (0.0f, 0.5f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = tabAt (e.position);
        if (i < 0) return;
        if (infoRect (i).expanded (2.0f).contains (e.position))
        {
            if (onInfo) onInfo (i);                      // info only: no tab change
            return;
        }
        if (i == selected) return;
        selected = i;
        repaint();
        if (onSelect) onSelect (i);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = tabAt (e.position);
        const bool overInfo = i >= 0 && infoRect (i).expanded (2.0f).contains (e.position);
        setMouseCursor (overInfo ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
    }

private:
    juce::Rectangle<float> tabRect (int i) const
    {
        const float w = (float) getWidth() / (float) juce::jmax (1, tabs.size());
        return juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (1.5f, 1.0f);
    }

    juce::Rectangle<float> infoRect (int i) const
    {
        const auto  r = tabRect (i);
        const float d = juce::jmin (14.0f, r.getHeight() - 8.0f);
        return { r.getRight() - d - 5.0f, r.getCentreY() - d * 0.5f, d, d };
    }

    int tabAt (juce::Point<float> p) const
    {
        if (tabs.isEmpty() || getWidth() <= 0) return -1;
        return juce::jlimit (0, tabs.size() - 1, (int) (p.x / ((float) getWidth() / (float) tabs.size())));
    }

    juce::StringArray tabs;
    int               selected = 0;
};

//==============================================================================
//  The info popup behind a tab's "i": the section first, then one line per
//  slider with its label in bold.  Shown in a CallOutBox pointing at the tab;
//  a click anywhere else closes it.
//==============================================================================
class EdmInfoPanel : public juce::Component
{
public:
    EdmInfoPanel (const juce::String& title, const juce::String& text)
    {
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::topLeft);
        as.setLineSpacing (2.0f);

        const juce::Font titleFont (juce::FontOptions (15.0f).withStyle ("Bold"));
        const juce::Font labelFont (juce::FontOptions (12.5f).withStyle ("Bold"));
        const juce::Font bodyFont  (juce::FontOptions (12.5f));

        as.append (title + "\n", titleFont, juce::Colour (Betel::Pal::kAccentLight));
        juce::StringArray lines;
        lines.addLines (text);
        for (int i = 0; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            const int bar = line.indexOfChar ('|');
            if (bar < 0)
            {
                as.append (line + "\n\n", bodyFont, juce::Colour (0xFFCFCFCF));   // the section
            }
            else
            {
                as.append (line.substring (0, bar), labelFont, juce::Colours::white);
                as.append ("   " + line.substring (bar + 1) + "\n", bodyFont, juce::Colour (0xFFB4B4B4));
            }
        }
        layout.createLayout (as, (float) (kWidth - 2 * kPad));
        setSize (kWidth, (int) std::ceil (layout.getHeight()) + 2 * kPad);
    }

    void paint (juce::Graphics& g) override
    {
        layout.draw (g, getLocalBounds().toFloat().reduced ((float) kPad));
    }

private:
    static constexpr int kWidth = 440, kPad = 14;
    juce::TextLayout layout;
};

//==============================================================================
//  The editor content.
//==============================================================================
//==============================================================================
//  The selected key's MIDI LED: lit while its note is on (the pad LEDs' own
//  source, so a hit shorter than a frame still flashes).
//==============================================================================
class EdmMidiLed : public juce::Component
{
public:
    void setOn (bool on)   { if (on != lit) { lit = on; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float d = juce::jmin (r.getHeight(), 11.0f);
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + d * 0.5f + 2.0f, r.getCentreY() });
        if (lit)
        {
            g.setColour (juce::Colour (0x553CFF5A));
            g.fillEllipse (dot.expanded (2.5f));
        }
        g.setColour (lit ? juce::Colour (0xFF3CFF5A) : juce::Colour (0xFF1C3A22));
        g.fillEllipse (dot);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (dot, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.95f : 0.55f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("MIDI", r.withTrimmedLeft (d + 7.0f), juce::Justification::centredLeft);
    }

private:
    bool lit = false;
};

class EdmKitEditor : public juce::Component,
                     private juce::Timer
{
public:
    // baseKit = the kit's name (lastLoadedKit); savedCells = the user's
    // shifts from the base kit, "" when the kit is exactly the base.
    std::function<void (const juce::String& baseKit, const juce::String& savedCells)> onKitChanged;
    std::function<void (int note, int velocity)> onNoteOn;
    std::function<void (int note)>               onNoteOff;
    // Pad LEDs: fill mask (bit n = key n) with the keys sounding now.
    std::function<void (uint32_t* mask)>         onQueryPadActivity;

    EdmKitEditor()
    {
        titleLbl.setText ("EDM KIT", juce::dontSendNotification);
        titleLbl.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
        titleLbl.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccentLight));
        addAndMakeVisible (titleLbl);

        for (int i = 0; i < (int) edm::factoryKits().size(); ++i)
            kitBox.addItem (juce::String (edm::factoryKits()[(size_t) i].name), i + 1);
        kitBox.onChange = [this]
        {
            if (updating) return;
            loadFactory (kitBox.getText());
        };
        addAndMakeVisible (kitBox);

        reloadBtn.setButtonText ("RELOAD FACTORY");
        reloadBtn.setTooltip ("Drop every edit and reload this kit as it ships");
        reloadBtn.onClick = [this] { loadFactory (baseName); };
        addAndMakeVisible (reloadBtn);

        // SAVE PAD AS BASE: the selected pad, as it sounds now, becomes that
        // pad's default in this kit - in every set that plays it.  The other
        // pads keep theirs (EdmKitFiles::saveBasePad).
        saveBaseBtn.setButtonText ("SAVE PAD AS BASE");
        saveBaseBtn.setTooltip ("Make this pad's current settings its default in this kit. "
                                "Sets keep their own changes on top of it.");
        saveBaseBtn.onClick = [this] { confirmSavePadAsBase(); };
        addAndMakeVisible (saveBaseBtn);

        slotLbl.setJustificationType (juce::Justification::centredRight);
        slotLbl.setColour (juce::Label::textColourId, juce::Colour (0xFF9A9A9A));
        addAndMakeVisible (slotLbl);

        grid.setKit (&kit);
        grid.onPress   = [this] (int key, int vel) { if (onNoteOn)  onNoteOn (key, vel); };
        grid.onRelease = [this] (int key)          { if (onNoteOff) onNoteOff (key); };
        grid.onEdit    = [this] (int key)          { selectKey (key); };
        addAndMakeVisible (grid);

        keyLbl.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
        keyLbl.setColour (juce::Label::textColourId, juce::Colour (0xFFEAEAEA));
        addAndMakeVisible (keyLbl);

        familyBox.addItem ("(silent)", 1);
        for (int i = 0; i < EdmUi::families().size(); ++i)
            familyBox.addItem (EdmUi::families()[i], i + 2);
        familyBox.onChange = [this] { if (! updating) familyChosen(); };
        addAndMakeVisible (familyBox);

        soundBox.onChange = [this] { if (! updating) soundChosen(); };
        addAndMakeVisible (soundBox);

        chokeBox.addItem ("No choke", 1);
        for (int i = 1; i <= 4; ++i)
            chokeBox.addItem ("Choke group " + juce::String (i), i + 1);
        chokeBox.onChange = [this]
        {
            if (updating || ! validKey()) return;
            cell().chokeGroup = chokeBox.getSelectedId() - 1;
            cellEdited();
        };
        addAndMakeVisible (chokeBox);

        for (auto* l : { &familyLbl, &soundLbl, &chokeLbl })
        {
            l->setColour (juce::Label::textColourId, juce::Colour (0xFF9A9A9A));
            l->setFont (juce::Font (juce::FontOptions (11.0f)));
            addAndMakeVisible (*l);
        }
        familyLbl.setText ("FAMILY", juce::dontSendNotification);
        soundLbl .setText ("SOUND",  juce::dontSendNotification);
        chokeLbl .setText ("CHOKE",  juce::dontSendNotification);

        tabBar.onSelect = [this] (int i) { currentTab = i; rebuildSliders(); };
        tabBar.onInfo   = [this] (int i) { showTabInfo (i); };

        // MAIN: the key's own two-handle filter, beside the macros
        addChildComponent (filterSlider);
        filterSlider.onChange = [this] (float lo, float hi)
        {
            if (updating || ! validKey()) return;
            cell().filterHpNorm = lo;
            cell().filterLpNorm = hi;
            cellEdited();
        };

        // FX: the selected key's family rack - six pages and a stage switch
        addAndMakeVisible (midiLed);
        addChildComponent (fxFamilyLbl);
        fxFamilyLbl.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        fxFamilyLbl.setJustificationType (juce::Justification::centredLeft);
        for (int i = 0; i < kNumFxPages; ++i)
        {
            auto& b = fxPageBtns[(size_t) i];
            b.setButtonText (kFxPageNames[i]);
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFD4A937));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            b.onClick = [this, i] { fxPage = i; rebuildSliders(); };
            addChildComponent (b);
        }
        fxOnBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF3CB85A));
        fxOnBtn.onClick = [this] { toggleFxStage(); };
        addChildComponent (fxOnBtn);
        addAndMakeVisible (tabBar);

        pad.onHit     = [this] (int vel) { if (validKey() && onNoteOn) onNoteOn (selectedKey, vel); };
        pad.onRelease = [this]           { if (validKey() && onNoteOff) onNoteOff (selectedKey); };
        addAndMakeVisible (pad);

        hintLbl.setText ("E opens a pad here. Edits play on the next hit and save with the set.",
                         juce::dontSendNotification);
        hintLbl.setColour (juce::Label::textColourId, juce::Colour (0xFF7A7A7A));
        hintLbl.setFont (juce::Font (juce::FontOptions (11.0f)));
        hintLbl.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (hintLbl);

        setSize (1100, 680);
        startTimerHz (60);                               // pad LEDs: within ~16 ms of the MIDI
    }

    ~EdmKitEditor() override   { stopTimer(); }

    //==========================================================================
    //  Load what the slot holds: its saved cells, else its factory kit.
    void setKit (const juce::String& baseKit, const juce::String& savedCells)
    {
        auto bHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
        edm::Kit& b = *bHeap;
        if (! EdmKitFiles::baseKit (baseKit, b))
            if (! EdmKitFiles::baseKit (juce::String (edm::factoryKits().front().name), b))
                return;
        baseline = b;
        edm::resolveWithBase (baseline, savedCells.toStdString(), kit);
        baseName = juce::String (b.name);
        const juce::ScopedValueSetter<bool> svs (updating, true);
        kitBox.setText (baseName, juce::dontSendNotification);
        grid.repaint();
        selectKey (selectedKey);
    }

    void setSlotLabel (const juce::String& s)   { slotLbl.setText (s, juce::dontSendNotification); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF1A1A1A));
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.drawHorizontalLine (headerBottom, 12.0f, (float) getWidth() - 12.0f);
        g.drawHorizontalLine (gridBottom + 6, 12.0f, (float) getWidth() - 12.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        auto header = area.removeFromTop (34);
        titleLbl .setBounds (header.removeFromLeft (110));
        kitBox   .setBounds (header.removeFromLeft (200).reduced (0, 4));
        header.removeFromLeft (8);
        reloadBtn.setBounds (header.removeFromLeft (130).reduced (0, 4));
        slotLbl  .setBounds (header);
        headerBottom = area.getY() + 4;
        area.removeFromTop (10);

        const int editorH = 250;
        auto gridArea = area.removeFromTop (juce::jmax (220, area.getHeight() - editorH - 14));
        grid.setBounds (gridArea);
        gridBottom = gridArea.getBottom();
        area.removeFromTop (14);

        // Cell editor: left column, the slider mixer, HIT pad
        auto left = area.removeFromLeft (250);
        keyLbl.setBounds (left.removeFromTop (26));
        left.removeFromTop (6);
        {
            auto famRow = left.removeFromTop (14);
            midiLed.setBounds (famRow.removeFromRight (60));
            familyLbl.setBounds (famRow);
        }
        familyBox.setBounds (left.removeFromTop (26));
        left.removeFromTop (6);
        soundLbl .setBounds (left.removeFromTop (14));
        soundBox .setBounds (left.removeFromTop (26));
        left.removeFromTop (6);
        chokeLbl .setBounds (left.removeFromTop (14));
        chokeBox .setBounds (left.removeFromTop (26));
        left.removeFromTop (8);
        saveBaseBtn.setBounds (left.removeFromTop (26));      // under every drop-list
        left.removeFromTop (6);
        hintLbl  .setBounds (left.removeFromTop (32));

        area.removeFromLeft (16);
        auto padArea = area.removeFromRight (190);
        pad.setBounds (padArea.reduced (0, 10));
        area.removeFromRight (12);

        // The mixer: the tab selector across its full width, the sliders under it
        tabBar.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);
        sliderArea = area;
        layoutSliders();
    }

private:
    enum { kTune = 0, kDecay, kDamp, kSnap, kColor, kDrive, kHuman, kVelSens, kGain, kNumKnobs };

    //==========================================================================
    //  MAIN tab: 0..200 on screen <-> the engine's values.  Chosen so every
    //  value the kits use sits on a whole step:
    //      TUNE   +/-25 semitones, quarter-semitone steps, 100 = in tune
    //      GAIN   +/-20 dB, 0.2 dB steps, 100 = 0 dB
    //      others 0..1 in 0.005 steps (defaults land on 30, 50, 100, 120)
    //  The voice tabs map through SynthParams.h (log for frequencies, times
    //  and ratios, linear for levels and amounts).
    //==========================================================================
    static float toUi (int k, float v)
    {
        if (k == kTune) return (v + 25.0f) * 4.0f;
        if (k == kGain) return (v + 20.0f) * 5.0f;
        return v * 200.0f;
    }

    static float fromUi (int k, float u)
    {
        if (k == kTune) return u / 4.0f - 25.0f;
        if (k == kGain) return u / 5.0f - 20.0f;
        return u / 200.0f;
    }

    static double defaultFor (int k)
    {
        const drum::Macros m;
        switch (k)
        {
            case kDecay:   return m.decay;
            case kDamp:    return m.damp;
            case kSnap:    return m.snap;
            case kColor:   return m.color;
            case kDrive:   return m.drive;
            case kHuman:   return m.humanize;
            case kVelSens: return m.velSens;
            default:       return 0.0;
        }
    }

    bool validKey() const          { return selectedKey >= 0 && selectedKey < 128; }
    edm::Cell& cell()              { return kit.cells[(size_t) selectedKey]; }

    //==========================================================================
    void timerCallback() override
    {
        if (! onQueryPadActivity) return;
        // Read even while hidden: each read consumes the channel's one-shot hit
        // flags, so reopening the window never flashes hits that are long gone.
        uint32_t m[4] = { 0u, 0u, 0u, 0u };
        onQueryPadActivity (m);
        grid.setActivity (m);
        midiLed.setOn (validKey() && ((m[selectedKey >> 5] >> (selectedKey & 31)) & 1u) != 0);
    }

    //==========================================================================
    void selectKey (int key)
    {
        selectedKey = juce::jlimit (0, 127, key);
        grid.setSelected (selectedKey);
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const auto& c = cell();
        keyLbl.setText ("KEY " + juce::String (selectedKey) + "  -  " + EdmUi::noteName (selectedKey),
                        juce::dontSendNotification);
        const auto fam = EdmUi::familyOf (c.sound);
        const int famIdx = EdmUi::families().indexOf (fam);
        familyBox.setSelectedId (famIdx >= 0 ? famIdx + 2 : 1, juce::dontSendNotification);
        refreshSoundBox (fam, c.sound);
        chokeBox.setSelectedId (juce::jlimit (0, 4, c.chokeGroup) + 1, juce::dontSendNotification);
        soundBox.setEnabled (! c.sound.isEmpty());
        chokeBox.setEnabled (! c.sound.isEmpty());
        refreshEditor();
    }

    // Tabs for the selected key's voice, then its sliders.  Another kind of
    // voice starts on MAIN; the same kind keeps the tab the user is on.
    void refreshEditor()
    {
        const auto& snd = cell().sound;
        juce::StringArray names { "MAIN" };
        if (const auto* t = edm::params::tableFor (snd))
            for (const char* n : t->tabs)
                names.add (n);
        names.add ("FX");                                // the key's family rack - always last
        fxTab = names.size() - 1;
        // Kick and snare controls depend on the model, so it is part of "kind"
        const int key = (int) snd.type * 8 + edm::params::kickModel (snd) + edm::params::snareModel (snd);
        if (key != tabKey)
            currentTab = 0;
        tabKey     = key;
        currentTab = juce::jlimit (0, names.size() - 1, currentTab);
        tabBar.setTabs (names, currentTab);
        rebuildSliders();
    }

    void rebuildSliders()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        sliders.clear();
        sliderSpecs.clear();
        fxSliderSpec.clear();
        if (! validKey()) return;

        const auto& c  = cell();
        const bool  on = ! c.sound.isEmpty();

        filterSlider.setVisible (currentTab == 0);
        filterSlider.setEnabled (on);
        filterSlider.setValues (c.filterHpNorm, c.filterLpNorm, false);
        const bool fxShown = (currentTab == fxTab);
        fxFamilyLbl.setVisible (fxShown);
        for (auto& b : fxPageBtns) b.setVisible (fxShown);
        fxOnBtn.setVisible (fxShown && fxPage < 4);

        if (fxShown)                                     // FX: the family's rack
        {
            buildFxPage();
        }
        else if (currentTab == 0)                        // MAIN: the macros and gain
        {
            static const char* names[kNumKnobs] =
                { "TUNE", "DECAY", "DAMP", "SNAP", "COLOR", "DRIVE", "HUMAN", "VEL SENS", "GAIN" };
            const auto& m = c.macros;
            const float vals[kNumKnobs] = { m.tuneSemis, m.decay, m.damp, m.snap, m.color,
                                            m.drive, m.humanize, m.velSens, c.gainDb };
            for (int k = 0; k < kNumKnobs; ++k)
            {
                auto s = std::make_unique<GoldSlider> (names[k], 0.0f, 200.0f,
                                                       toUi (k, (float) defaultFor (k)));
                s->setStep (1.0f);
                s->displayFn = [] (float v) { return juce::String ((int) std::lround (v)); };
                s->setValue (toUi (k, vals[k]), false);
                s->onChange  = [this, k] (float) { if (! updating) knobMoved (k); };
                s->setEnabled (on);
                addAndMakeVisible (*s);
                sliders.push_back (std::move (s));
            }
        }
        else if (const auto* t = edm::params::tableFor (c.sound))
        {
            // Double-click default = the value the sound ships with.
            const edm::Sound* lib = edm::params::librarySound (c.sound);
            edm::Sound ref = (lib != nullptr) ? *lib : c.sound;
            edm::Sound cur = c.sound;
            for (const auto& sp : t->specs)
            {
                if (sp.tab != currentTab - 1) continue;
                auto s = std::make_unique<GoldSlider> (sp.label, 0.0f, 200.0f,
                                                       edm::params::toUi (sp, *sp.field (ref)));
                s->setStep (1.0f);
                s->displayFn = [] (float v) { return juce::String ((int) std::lround (v)); };
                s->setValue (edm::params::toUi (sp, *sp.field (cur)), false);
                const int idx = (int) sliderSpecs.size();
                s->onChange  = [this, idx] (float) { if (! updating) paramMoved (idx); };
                s->setEnabled (on);
                addAndMakeVisible (*s);
                sliders.push_back (std::move (s));
                sliderSpecs.push_back (&sp);
            }
        }
        layoutSliders();
    }

    // One row of vertical sliders.  Columns never narrower than MAIN's nine,
    // so every tab keeps the same slider width; a short tab sits centred.
    void layoutSliders()
    {
        if (sliderArea.isEmpty()) return;
        auto area = sliderArea;

        if (currentTab == fxTab)                         // FX: family, six pages, ON
        {
            auto row = area.removeFromTop (24);
            fxFamilyLbl.setBounds (row.removeFromLeft (130));
            fxOnBtn.setBounds (row.removeFromRight (64).reduced (2, 0));
            row.removeFromRight (8);
            const int bw = row.getWidth() / kNumFxPages;
            for (int i = 0; i < kNumFxPages; ++i)
                fxPageBtns[(size_t) i].setBounds (row.removeFromLeft (bw).reduced (2, 0));
            area.removeFromTop (6);
        }

        const bool withFilter = currentTab == 0 && filterSlider.isVisible();
        const int  nSliders   = (int) sliders.size();
        const int  n          = nSliders + (withFilter ? 1 : 0);
        if (n == 0) return;
        const int cols  = juce::jmax (n, (int) kNumKnobs);
        const int cellW = area.getWidth() / cols;
        const int x0    = area.getX() + (area.getWidth() - cellW * n) / 2;
        for (int i = 0; i < nSliders; ++i)
            sliders[(size_t) i]->setBounds (x0 + i * cellW, area.getY(), cellW, area.getHeight());
        if (withFilter)
            filterSlider.setBounds (x0 + nSliders * cellW, area.getY(), cellW, area.getHeight());
    }

    // Fill SOUND with the family's library entries and select `current`.
    void refreshSoundBox (const juce::String& fam, const edm::Sound& current)
    {
        soundBox.clear (juce::dontSendNotification);
        soundIndices.clear();
        if (fam.isEmpty()) return;
        const auto& lib = edm::library();
        int selectId = 0;
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            if (fam != lib[(size_t) i].family) continue;
            soundIndices.push_back (i);
            const int id = (int) soundIndices.size();
            soundBox.addItem (EdmUi::soundLabel (lib[(size_t) i].sound), id);
            if (! current.isEmpty()
                && lib[(size_t) i].sound.articulation == current.articulation
                && juce::String (lib[(size_t) i].sound.name()) == juce::String (current.name()))
                selectId = id;
        }
        soundBox.setSelectedId (selectId, juce::dontSendNotification);
    }

    void familyChosen()
    {
        if (! validKey()) return;
        auto& c = cell();
        const int id = familyBox.getSelectedId();
        if (id <= 1)                                   // (silent)
        {
            c.sound = edm::Sound();
            cellEdited();
            selectKey (selectedKey);
            return;
        }
        const auto fam = EdmUi::families()[id - 2];
        const bool wasEmpty = c.sound.isEmpty();
        refreshSoundBox (fam, edm::Sound());
        if (soundIndices.empty()) return;
        c.sound = edm::library()[(size_t) soundIndices.front()].sound;
        if (wasEmpty)
        {
            c.macros = drum::Macros();                 // a fresh key starts neutral
            c.gainDb = 0.0f;
        }
        cellEdited();
        selectKey (selectedKey);
    }

    void soundChosen()
    {
        if (! validKey()) return;
        const int i = soundBox.getSelectedId() - 1;
        if (i < 0 || i >= (int) soundIndices.size()) return;
        cell().sound = edm::library()[(size_t) soundIndices[(size_t) i]].sound;
        cellEdited();
        refreshEditor();                               // the new sound's own values
    }

    void knobMoved (int k)
    {
        if (! validKey() || k < 0 || k >= (int) sliders.size()) return;
        auto& c = cell();
        const float v = fromUi (k, sliders[(size_t) k]->getValue());
        switch (k)
        {
            case kTune:    c.macros.tuneSemis = v; break;
            case kDecay:   c.macros.decay     = v; break;
            case kDamp:    c.macros.damp      = v; break;
            case kSnap:    c.macros.snap      = v; break;
            case kColor:   c.macros.color     = v; break;
            case kDrive:   c.macros.drive     = v; break;
            case kHuman:   c.macros.humanize  = v; break;
            case kVelSens: c.macros.velSens   = v; break;
            case kGain:    c.gainDb           = v; break;
            default: break;
        }
        cellEdited();
    }

    // The "i" of a tab: what its sliders shape, in a callout pointing at the tab.
    void showTabInfo (int i)
    {
        const auto& snd = cell().sound;
        const auto text = (i == fxTab) ? std::string (kFxInfo) : edm::params::tabInfo (snd, i);
        if (text.empty()) return;
        const auto title = tabBar.getTabName (i) + "  -  " + edm::params::voiceKindName (snd);
        juce::CallOutBox::launchAsynchronously (std::make_unique<EdmInfoPanel> (title, juce::String (text)),
                                                getLocalArea (&tabBar, tabBar.getTabArea (i)), this);
    }

    // A voice-tab slider moved: write straight into the key's prototype copy.
    void paramMoved (int idx)
    {
        if (! validKey() || idx < 0 || idx >= (int) sliderSpecs.size()) return;
        const auto& sp = *sliderSpecs[(size_t) idx];
        *sp.field (cell().sound) = edm::params::fromUi (sp, sliders[(size_t) idx]->getValue());
        cellEdited();
    }

    void cellEdited()
    {
        grid.repaint();
        if (onKitChanged)
            onKitChanged (baseName, juce::String (edm::computeShifts (baseline, kit)));
    }

    void loadFactory (const juce::String& name)
    {
        auto bHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
        edm::Kit& b = *bHeap;
        if (! EdmKitFiles::baseKit (name, b)) return;
        baseline = b;
        kit      = b;
        baseName = name;
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            kitBox.setText (baseName, juce::dontSendNotification);
        }
        grid.repaint();
        selectKey (selectedKey);
        if (onKitChanged) onKitChanged (baseName, {});   // "" = exactly the base kit
    }

    //==========================================================================
    //  SAVE PAD AS BASE.  The selected pad, as it sounds now, becomes its base:
    //  the value every set that plays this kit starts from, with the set's own
    //  changes stored as shifts on top.  Only that pad moves - the other pads
    //  and the family racks keep their base (EdmKitFiles::saveBasePad).
    //==========================================================================
    void confirmSavePadAsBase()
    {
        const int key = selectedKey;
        if (key < 0 || key > 127 || baseName.isEmpty()) return;
        const auto file = EdmKitFiles::baseKitFile (baseName);
        auto* aw = new juce::AlertWindow ("Save pad as base",
            "Save " + padLabel (key) + " of \"" + baseName + "\" as it sounds now as its BASE:\n\n"
              + (file == juce::File() ? juce::String ("(the sound library folder is not set)")
                                      : file.getFullPathName())
              + "\n\nEvery set playing this kit starts this pad from here and keeps\n"
                "its own changes on top. The other pads are not touched.",
            juce::MessageBoxIconType::NoIcon, this);
        aw->addButton ("SAVE BASE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel",    0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<EdmKitEditor> safe (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, key] (int result)
        {
            if (result == 1 && safe != nullptr)
                safe->savePadAsBase (key);
        }), true);
    }

    void savePadAsBase (int key)
    {
        if (key < 0 || key > 127) return;
        const auto r = EdmKitFiles::saveBasePad (baseName, key, kit.cells[(size_t) key]);
        if (r.failed())
        {
            // LOUD: the hint line is easy to miss, and a base that silently did not
            // save looks exactly like one that saved and then failed to load.
            hintLbl.setText ("Base NOT saved: " + r.getErrorMessage(), juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Pad NOT saved as base", r.getErrorMessage(), "OK", this);
            return;
        }
        saveBaseBtn.setButtonText ("SAVED");
        {
            juce::Component::SafePointer<EdmKitEditor> safe (this);
            juce::Timer::callAfterDelay (1500, [safe]
            {
                if (safe != nullptr) safe->saveBaseBtn.setButtonText ("SAVE PAD AS BASE");
            });
        }
        // The base as it is on disk now: this pad as it sounds, the rest as before.
        EdmKitFiles::baseKit (baseName, baseline);
        hintLbl.setText (padLabel (key) + " saved as base", juce::dontSendNotification);

        // The slot's edits, measured again from the new base: this pad's shift
        // is zero now, every other pad keeps its own.  What plays does not change.
        grid.repaint();
        if (onKitChanged) onKitChanged (baseName, juce::String (edm::computeShifts (baseline, kit)));
    }

    juce::String padLabel (int key) const
    {
        const char* snd = kit.cells[(size_t) key].sound.name();
        return EdmUi::noteName (key) + " (" + juce::String (key) + ")"
             + ((snd != nullptr && *snd != 0) ? " " + juce::String (snd) : juce::String());
    }

    //==========================================================================
    edm::Kit          kit;
    edm::Kit          baseline;                 // the base kit the shifts are measured from
    juce::String      baseName;
    int               selectedKey = 36;
    bool              updating    = false;
    std::vector<int>  soundIndices;             // SOUND item (id - 1) -> library index
    int               headerBottom = 0, gridBottom = 0;

    int               currentTab = 0;           // 0 = MAIN, then the voice's tabs
    int               tabKey     = -1;          // voice type x kick model of the tabs shown
    juce::Rectangle<int> sliderArea;

    juce::Label       titleLbl, slotLbl, keyLbl, hintLbl, familyLbl, soundLbl, chokeLbl;
    juce::ComboBox    kitBox, familyBox, soundBox, chokeBox;
    juce::TextButton  reloadBtn, saveBaseBtn;
    EdmKeyGrid        grid;
    EdmTabBar         tabBar;
    EdmHitPad         pad;
    std::vector<std::unique_ptr<GoldSlider>>        sliders;
    std::vector<const edm::params::Spec*>           sliderSpecs;   // voice tabs: slider -> field

    //==========================================================================
    //  FX TAB: the selected key's FAMILY rack (edm::Kit::fx) - the six pages
    //  of a sampled kit's rack.  0..200 on screen, like every slider here.
    //==========================================================================
    static constexpr int kNumFxPages = 6;
    static constexpr const char* kFxPageNames[kNumFxPages] = { "EQ", "SAT", "COMP", "SWEET", "PAN", "SENDS" };
    static constexpr const char* kFxInfo =
        "Every family of the kit - every pad colour - has its own effect chain; this page shows the one of the "
        "selected key's family, and every key of that family plays through it.\n"
        "EQ|10 bands, 31 Hz to 16 kHz - each slider is the volume of its own band. 0 = silent, 100 = flat, 200 = +24 dB.\n"
        "SAT|Saturation: DRIVE and the wet MIX.\n"
        "COMP|A compressor: threshold, ratio, attack, release, make-up gain.\n"
        "SWEET|The sweetener: soften or sharpen the attack, peak control, tame the harsh band, round warmth.\n"
        "PAN|Where the family sits, left to right. 100 = centre.\n"
        "SENDS|How much of the family reaches the global chorus, reverb and delay.\n"
        "ON|Switches the page's stage on or off. Moving any of its sliders switches it on.";

    struct FxSpec { int page; const char* label; float lo, hi; bool log; int band; float edm::FamilyFx::* field; };
    static const std::vector<FxSpec>& fxSpecs()
    {
        using F = edm::FamilyFx;
        // The EQ rows' range is the shared rule's (DrumSplitEq.h); their slider
        // mapping is the rule's too - see fxToUi / fxFromUi.
        constexpr float eqLo = Betel::DrumSplitEq::kSilenceDb;
        constexpr float eqHi = Betel::DrumSplitEq::kMaxDb;
        static const std::vector<FxSpec> v
        {
            { 0, "31",  eqLo, eqHi, false, 0, nullptr }, { 0, "62",  eqLo, eqHi, false, 1, nullptr },
            { 0, "125", eqLo, eqHi, false, 2, nullptr }, { 0, "250", eqLo, eqHi, false, 3, nullptr },
            { 0, "500", eqLo, eqHi, false, 4, nullptr }, { 0, "1K",  eqLo, eqHi, false, 5, nullptr },
            { 0, "2K",  eqLo, eqHi, false, 6, nullptr }, { 0, "4K",  eqLo, eqHi, false, 7, nullptr },
            { 0, "8K",  eqLo, eqHi, false, 8, nullptr }, { 0, "16K", eqLo, eqHi, false, 9, nullptr },
            { 1, "DRIVE",   0.0f, 1.0f,  false, -1, &F::satDrive },   { 1, "MIX", 0.0f, 1.0f, false, -1, &F::satMix },
            { 2, "THRESH",  -60.0f, 0.0f, false, -1, &F::compThreshDb }, { 2, "RATIO", 1.0f, 20.0f, true, -1, &F::compRatio },
            { 2, "ATTACK",  0.1f, 200.0f, true, -1, &F::compAttackMs },  { 2, "RELEASE", 5.0f, 2000.0f, true, -1, &F::compReleaseMs },
            { 2, "MAKEUP",  0.0f, 24.0f, false, -1, &F::compMakeupDb },
            { 3, "MIX",     0.0f, 1.0f, false, -1, &F::sweetMix },      { 3, "SOFTEN", -1.0f, 1.0f, false, -1, &F::softenDepth },
            { 3, "SOFT TM", 1.0f, 150.0f, true, -1, &F::softenMs },     { 3, "PEAK", 3.0f, 24.0f, false, -1, &F::peakCeilDb },
            { 3, "P RATIO", 1.0f, 20.0f, true, -1, &F::peakRatio },     { 3, "TAME", 0.0f, 12.0f, false, -1, &F::tameDepthDb },
            { 3, "TAME HZ", 1500.0f, 8000.0f, true, -1, &F::tameFreqHz }, { 3, "ROUND", 0.0f, 1.0f, false, -1, &F::roundDrive },
            { 3, "R MIX",   0.0f, 1.0f, false, -1, &F::roundMix },
            { 4, "PAN",     -1.0f, 1.0f, false, -1, &F::pan },
            { 5, "CHORUS",  0.0f, 1.0f, false, -1, &F::chorusSend },   { 5, "REVERB", 0.0f, 1.0f, false, -1, &F::reverbSend },
            { 5, "DELAY",   0.0f, 1.0f, false, -1, &F::delaySend },
        };
        return v;
    }
    static float& fxRef (edm::FamilyFx& fx, const FxSpec& sp)   { return sp.field ? fx.*(sp.field) : fx.eqGainDb[sp.band]; }
    // An EQ row: a band gain, not a plain field.
    static bool isEqBand (const FxSpec& sp) { return sp.field == nullptr && sp.band >= 0; }

    static float fxToUi (const FxSpec& sp, float v)
    {
        // THE EQ FOLLOWS THE SHARED SLIDER RULE, not a straight line: the
        // bottom half is silence .. unity, the top half unity .. +24 dB - the
        // same as the sampled kits' EQ page.  100 stays flat.
        if (isEqBand (sp)) return 200.0f * Betel::DrumSplitEq::positionFromDb (v);

        v = juce::jlimit (sp.lo, sp.hi, v);
        return sp.log ? 200.0f * std::log (v / sp.lo) / std::log (sp.hi / sp.lo) : 200.0f * (v - sp.lo) / (sp.hi - sp.lo);
    }
    static float fxFromUi (const FxSpec& sp, float u)
    {
        const float t = juce::jlimit (0.0f, 200.0f, u) / 200.0f;
        if (isEqBand (sp)) return Betel::DrumSplitEq::dbFromPosition (t);
        return sp.log ? sp.lo * std::pow (sp.hi / sp.lo, t) : sp.lo + (sp.hi - sp.lo) * t;
    }
    static bool* fxStage (edm::FamilyFx& fx, int page)
    {
        switch (page) { case 0: return &fx.eqOn; case 1: return &fx.satOn; case 2: return &fx.compOn; case 3: return &fx.sweetOn; default: return nullptr; }
    }
    int familyOfSelected() const
    {
        return validKey() ? edm::familyIndex (EdmUi::familyOf (kit.cells[(size_t) selectedKey].sound).toStdString()) : -1;
    }

    void buildFxPage()
    {
        const int fam = familyOfSelected();
        for (int i = 0; i < kNumFxPages; ++i)
            fxPageBtns[(size_t) i].setToggleState (i == fxPage, juce::dontSendNotification);
        if (fam < 0)
        {
            fxFamilyLbl.setText ("NO FAMILY", juce::dontSendNotification);
            fxOnBtn.setVisible (false);
            return;
        }
        fxFamilyLbl.setText (juce::String (edm::familyName (fam)).toUpperCase() + " FAMILY", juce::dontSendNotification);
        fxFamilyLbl.setColour (juce::Label::textColourId, EdmUi::familyColour (edm::familyName (fam)).brighter (0.4f));
        auto& fx = kit.fx[(size_t) fam];
        if (bool* on = fxStage (fx, fxPage))
        {
            fxOnBtn.setButtonText (*on ? "ON" : "OFF");
            fxOnBtn.setToggleState (*on, juce::dontSendNotification);
        }
        const edm::FamilyFx def;
        const auto& specs = fxSpecs();
        for (int i = 0; i < (int) specs.size(); ++i)
        {
            const auto& sp = specs[(size_t) i];
            if (sp.page != fxPage) continue;
            auto s = std::make_unique<GoldSlider> (sp.label, 0.0f, 200.0f,
                                                   fxToUi (sp, fxRef (const_cast<edm::FamilyFx&> (def), sp)));
            s->setStep (1.0f);
            s->displayFn = [] (float v) { return juce::String ((int) std::lround (v)); };
            s->setValue (fxToUi (sp, fxRef (fx, sp)), false);
            const int idx = (int) fxSliderSpec.size();
            s->onChange = [this, idx] (float) { if (! updating) fxMoved (idx); };
            addAndMakeVisible (*s);
            sliders.push_back (std::move (s));
            fxSliderSpec.push_back (i);
        }
    }

    void fxMoved (int idx)
    {
        const int fam = familyOfSelected();
        if (fam < 0 || idx < 0 || idx >= (int) fxSliderSpec.size() || idx >= (int) sliders.size()) return;
        const auto& sp = fxSpecs()[(size_t) fxSliderSpec[(size_t) idx]];
        auto& fx = kit.fx[(size_t) fam];
        fxRef (fx, sp) = fxFromUi (sp, sliders[(size_t) idx]->getValue());
        if (bool* on = fxStage (fx, sp.page); on != nullptr && ! *on)
        {
            *on = true;                                  // moving a stage's slider switches it on
            fxOnBtn.setButtonText ("ON");
            fxOnBtn.setToggleState (true, juce::dontSendNotification);
        }
        cellEdited();
    }

    void toggleFxStage()
    {
        const int fam = familyOfSelected();
        if (fam < 0) return;
        if (bool* on = fxStage (kit.fx[(size_t) fam], fxPage))
        {
            *on = ! *on;
            fxOnBtn.setButtonText (*on ? "ON" : "OFF");
            fxOnBtn.setToggleState (*on, juce::dontSendNotification);
            cellEdited();
        }
    }

    BandFilterSlider  filterSlider { "FILTER" };
    EdmMidiLed        midiLed;
    juce::Label       fxFamilyLbl;
    std::array<juce::TextButton, kNumFxPages> fxPageBtns;
    juce::TextButton  fxOnBtn { "ON" };
    int               fxTab  = -1;                  // index of the FX tab (always the last)
    int               fxPage = 0;                   // EQ .. SENDS
    std::vector<int>  fxSliderSpec;                 // FX page: slider -> fxSpecs() index

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdmKitEditor)
};

//==============================================================================
//  The window — same behaviour as the drum editor popup.
//==============================================================================
class EdmKitWindow : public juce::DocumentWindow
{
public:
    std::function<void()> onClosed;

    EdmKitWindow()
        : juce::DocumentWindow ("EDM Kit Editor",
                                juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (false);
        setResizable (true, true);
        setResizeLimits (960, 600, 2200, 1400);
        editor = new EdmKitEditor();
        setContentOwned (editor, true);
        Betel::centreGrexPopupOnScreen (*this, 1100, 680);
    }

    ~EdmKitWindow() override { clearContentComponent(); }

    EdmKitEditor& getEditor() noexcept   { return *editor; }

    void openCentredOver (juce::Component* parent, float scale)
    {
        if (parent != nullptr)
        {
            const auto pb = parent->getScreenBounds();
            const int w = juce::jlimit (960, 2200, (int) ((float) pb.getWidth()  * scale));
            const int h = juce::jlimit (600, 1400, (int) ((float) pb.getHeight() * scale));
            setBounds (pb.getCentreX() - w / 2, pb.getCentreY() - h / 2, w, h);
        }
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed) onClosed();
    }

private:
    EdmKitEditor* editor = nullptr;   // owned by the window (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdmKitWindow)
};
