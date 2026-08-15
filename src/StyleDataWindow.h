#pragma once

#include <map>
#include <array>
#include <utility>
#include "GlobalMacros.h"   // Betel::displayNameFor
//==============================================================================
// StyleDataWindow.h
//
// A read-only popup that dumps every piece of metadata the parser extracted
// from the currently-loaded Yamaha style file:
//
//   • Header   : name, SFF version, original BPM, time signature, PPQ
//                (ticks-per-quarter), CASM / OTS / Music-Finder presence.
//   • Channels : the per-channel initial voice setup (bank MSB/LSB, program,
//                volume, pan, reverb).
//   • PCs      : every Program Change in the track, in tick order, with the
//                running bank-select state at the moment it fires.
//   • CCs      : every Control Change in the track, in tick order, with the
//                CC number (named where well-known) and value.
//
// The window takes a SNAPSHOT (by const-ref, copied into a juce::String at
// build time) so it never touches the live StyleData after construction — it
// is safe to leave open while styles reload; the host just rebuilds it.
//==============================================================================

#include <JuceHeader.h>
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include "StyleData.h"
#include "StyleCensus.h"
#include "BankProgramMap.h"
#include "StylePlayer.h"

namespace Betel
{
    class StyleDataWindow : public juce::DocumentWindow
    {
    public:
        std::function<void()> onClose;

        explicit StyleDataWindow (const StyleData& style, StylePlayer* player = nullptr)
            : juce::DocumentWindow ("Style Data",
                                    juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton),
              playerRef (player)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (true);
            auto* c = new Content (style, playerRef);
            c->stylesFolderProvider = [this] { return stylesFolder; };
            setContentOwned (c, true);
            Betel::centreGrexPopupOnScreen (*this, 620, 640);
            setResizable (true, false);
            setVisible (true);
        }

        /** Re-point an already-open window at a freshly-loaded style. */
        void refreshFrom (const StyleData& style)
        {
            auto* c = new Content (style, playerRef);
            c->stylesFolderProvider = [this] { return stylesFolder; };
            setContentOwned (c, true);
            setName ("Style Data");
        }

        /** Where the LIBRARY CENSUS tab scans.  Set by the host from the folder
            manager, since the window itself has no idea where the library is. */
        void setStylesFolder (const juce::File& f) { stylesFolder = f; }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClose) onClose();
        }

    private:
        StylePlayer* playerRef = nullptr;   // engine handle for the MIDI-trace tab
        juce::File   stylesFolder;          // scanned by the LIBRARY CENSUS tab

        //======================================================================
        // Content — header card + a scrollable read-only text dump.
        //======================================================================
        struct Content : public juce::Component
        {
            juce::TextButton tabData   { "STYLE DATA" };
            juce::TextButton tabNotes  { "STYLE NOTES" };
            juce::TextButton tabExport { "NOTES EXPORT" };
            juce::TextButton tabTrace  { "MIDI TRACE" };
            juce::TextButton tabCensus { "LIBRARY CENSUS" };
            juce::Label      header;     // STYLE DATA tab
            juce::TextEditor body;       // STYLE DATA tab
            juce::TextEditor notesBody;  // STYLE NOTES tab
            juce::TextEditor exportBody; // NOTES EXPORT tab (on-demand text)
            juce::TextButton btnGenerate { "GENERATE" };  // builds the export

            // MIDI TRACE tab — no inline log; arm capture, then export to file.
            juce::ToggleButton traceArm       { "Arm capture" };
            juce::TextButton   btnTraceExport { "EXPORT TRACE" };
            juce::Label        traceStatus;
            StylePlayer*       playerRef = nullptr;
            int activeTab = 0;

            // ── LIBRARY CENSUS tab ────────────────────────────────────────
            // Walks every style in the library and reports which drum kits it
            // asks for and which notes below 35 it plays.  Neither is knowable
            // from a data sheet: the kit list is whatever the arrangers used,
            // and the 13..34 region means different instruments in different
            // kits.  Parse-only, stepped, writes a text file.
            juce::TextButton   btnCensus { "SCAN LIBRARY" };
            juce::Label        censusStatus;
            std::unique_ptr<StyleCensus> census;
            std::function<juce::File()>  stylesFolderProvider;

            // Held only for the lifetime of this Content; refreshFrom() destroys
            // and re-creates Content whenever the style changes, so this ref is
            // safe as long as the Content is alive.  Used by btnGenerate to
            // build the export on demand instead of paying for it on every
            // style load.
            const StyleData* styleRef = nullptr;

            Content (const StyleData& s, StylePlayer* player)
            {
                styleRef  = &s;
                playerRef = player;

                for (auto* b : { &tabData, &tabNotes, &tabExport, &tabTrace, &tabCensus })
                {
                    b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
                    b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC6600));
                    b->setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.75f));
                    b->setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
                    b->setClickingTogglesState (false);
                    addAndMakeVisible (*b);
                }
                tabData  .onClick = [this] { setTab (0); };
                tabNotes .onClick = [this] { setTab (1); };
                tabExport.onClick = [this] { setTab (2); };
                tabTrace .onClick = [this] { setTab (3); };
                tabCensus.onClick = [this] { setTab (4); };

                // ── LIBRARY CENSUS ──────────────────────────────────────────
                btnCensus.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF2A2A2A));
                btnCensus.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                btnCensus.onClick = [this] { runCensus(); };
                addChildComponent (btnCensus);

                censusStatus.setJustificationType (juce::Justification::topLeft);
                censusStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
                censusStatus.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                                  13.0f, juce::Font::plain));
                censusStatus.setText ("Scans every style in the library and writes\n"
                                      "grex_style_census.txt to the Grex folder.\n\n"
                                      "Reports which drum kits the styles request\n"
                                      "(with the unnamed ones flagged) and which\n"
                                      "notes below 35 they play, by folder.\n\n"
                                      "Parse-only - no samples are decoded.",
                                      juce::dontSendNotification);
                addChildComponent (censusStatus);

                header.setJustificationType (juce::Justification::topLeft);
                header.setColour (juce::Label::textColourId, juce::Colour (0xFFCC6600));
                header.setColour (juce::Label::backgroundColourId, juce::Colour (0xFF101010));
                header.setFont (juce::Font (14.0f, juce::Font::bold));
                header.setText (buildHeader (s), juce::dontSendNotification);
                addAndMakeVisible (header);

                auto styleReadOnly = [] (juce::TextEditor& ed)
                {
                    ed.setMultiLine (true);
                    ed.setReadOnly (true);
                    ed.setScrollbarsShown (true);
                    ed.setCaretVisible (false);
                    ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF0C0C0C));
                    ed.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
                    ed.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xFF333333));
                    ed.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                            13.0f, juce::Font::plain));
                };
                styleReadOnly (body);
                body.setText (buildBody (s), juce::dontSendNotification);
                addAndMakeVisible (body);

                styleReadOnly (notesBody);
                notesBody.setText (buildNotes (s), juce::dontSendNotification);
                addChildComponent (notesBody);

                // ── NOTES EXPORT tab ────────────────────────────────────────
                // Empty by default; the build is potentially expensive (walks
                // every event, builds note on/off pairs, formats them) and the
                // user asked specifically for it to be ad-hoc, never on style
                // load.  Pressing GENERATE rebuilds against whatever the live
                // style is right now.
                styleReadOnly (exportBody);
                exportBody.setText ("Press GENERATE to export every note in the "
                                    "style as comma-separated (channel, start tick, "
                                    "duration ticks, velocity) tuples, grouped by "
                                    "section and channel.",
                                    juce::dontSendNotification);
                addChildComponent (exportBody);

                btnGenerate.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFFCC6600));
                btnGenerate.setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
                btnGenerate.onClick = [this]
                {
                    if (styleRef == nullptr) return;
                    exportBody.setText (buildNotesExport (*styleRef),
                                        juce::dontSendNotification);
                };
                addChildComponent (btnGenerate);

                // ── MIDI TRACE tab ──────────────────────────────────────────
                traceArm.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
                traceArm.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFFCC6600));
                traceArm.setEnabled (playerRef != nullptr);
                traceArm.onClick = [this]
                {
                    if (playerRef == nullptr) return;
                    playerRef->setTraceArmed (traceArm.getToggleState());
                    updateTraceStatus();
                };
                addChildComponent (traceArm);

                btnTraceExport.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFFCC6600));
                btnTraceExport.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
                btnTraceExport.onClick = [this] { exportTrace(); };
                addChildComponent (btnTraceExport);

                traceStatus.setJustificationType (juce::Justification::topLeft);
                traceStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
                traceStatus.setColour (juce::Label::backgroundColourId, juce::Colour (0xFF0C0C0C));
                traceStatus.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                                                 13.0f, juce::Font::plain));
                addChildComponent (traceStatus);

                setTab (0);
            }

            //------------------------------------------------------------------
            // MIDI TRACE tab helpers.
            //------------------------------------------------------------------
            void updateTraceStatus()
            {
                if (playerRef == nullptr)
                {
                    traceStatus.setText (" MIDI trace unavailable (no engine handle).",
                                         juce::dontSendNotification);
                    btnTraceExport.setEnabled (false);
                    return;
                }
                const bool armed = playerRef->isTraceArmed();
                const int  n     = playerRef->getTraceCount();
                juce::String s;
                if (armed)
                    s << " Capturing - play the style, then switch Arm off.\n"
                      << " Captured so far: " << n;
                else
                    s << " Stopped. Captured " << n << " note event"
                      << (n == 1 ? "" : "s")
                      << (playerRef->traceOverflowed() ? " (buffer full - truncated)." : ".")
                      << "\n Press EXPORT TRACE to save as a text file.";
                traceStatus.setText (s, juce::dontSendNotification);
                // Export only while disarmed and there is data — reading the
                // buffer while the audio thread writes it would race.
                btnTraceExport.setEnabled (! armed && n > 0);
            }

            void syncTraceArmToggle()
            {
                if (playerRef != nullptr)
                    traceArm.setToggleState (playerRef->isTraceArmed(),
                                             juce::dontSendNotification);
            }

            void exportTrace()
            {
                if (playerRef == nullptr || playerRef->isTraceArmed()) return;
                const juce::String text = buildTraceText (
                    *playerRef, styleRef != nullptr ? styleRef->name : juce::String());
                auto fc = std::make_shared<juce::FileChooser> (
                    "Export MIDI trace", juce::File(), "*.txt");

                // Step aside for the OS dialog and take the topmost flag back
                // when it closes -- see GrexPopupWindow.h.  This Content has no
                // owner reference, so the window is reached the only way it
                // can be: as the top-level component this panel sits inside.
                juce::Component::SafePointer<juce::Component> back (getTopLevelComponent());
                Betel::suspendGrexPopupTopmost (back.getComponent());

                fc->launchAsync (juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [text, fc, back] (const juce::FileChooser& chooser)
                                 {
                                     Betel::restoreGrexPopupTopmost (back);

                                     auto f = chooser.getResult();
                                     if (f != juce::File())
                                         f.replaceWithText (text);
                                 });
            }

            void runCensus()
            {
                if (census != nullptr && census->isRunning()) { census->cancel(); return; }

                const auto folder = stylesFolderProvider ? stylesFolderProvider()
                                                         : juce::File();
                if (! folder.isDirectory())
                {
                    censusStatus.setText ("No styles folder - check the library path.",
                                          juce::dontSendNotification);
                    return;
                }

                census = std::make_unique<StyleCensus>();

                census->onProgress = [this] (int done, int tot)
                {
                    btnCensus.setButtonText ("STOP");
                    censusStatus.setText ("Scanning " + juce::String (done)
                                          + " / " + juce::String (tot) + " ...",
                                          juce::dontSendNotification);
                };

                census->onFinished = [this] (const juce::File& out, int scanned)
                {
                    btnCensus.setButtonText ("SCAN LIBRARY");
                    censusStatus.setText ("Done - " + juce::String (scanned)
                                          + " styles scanned.\n\nWritten to:\n"
                                          + out.getFullPathName(),
                                          juce::dontSendNotification);
                };

                if (! census->start (folder))
                    censusStatus.setText ("No style files found under\n"
                                          + folder.getFullPathName(),
                                          juce::dontSendNotification);
            }

            void setTab (int t)
            {
                activeTab = t;
                tabData  .setToggleState (t == 0, juce::dontSendNotification);
                tabNotes .setToggleState (t == 1, juce::dontSendNotification);
                tabExport.setToggleState (t == 2, juce::dontSendNotification);
                tabTrace .setToggleState (t == 3, juce::dontSendNotification);
                tabCensus.setToggleState (t == 4, juce::dontSendNotification);
                header     .setVisible (t == 0);
                body       .setVisible (t == 0);
                notesBody  .setVisible (t == 1);
                exportBody .setVisible (t == 2);
                btnGenerate.setVisible (t == 2);
                traceArm      .setVisible (t == 3);
                btnTraceExport.setVisible (t == 3);
                traceStatus   .setVisible (t == 3);
                btnCensus     .setVisible (t == 4);
                censusStatus  .setVisible (t == 4);
                if (t == 3) { syncTraceArmToggle(); updateTraceStatus(); }
                resized();
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (10);

                auto tabRow = r.removeFromTop (30);
                const int tw = juce::jmin (130, tabRow.getWidth() / 5 - 4);
                tabData  .setBounds (tabRow.removeFromLeft (tw));
                tabRow.removeFromLeft (4);
                tabNotes .setBounds (tabRow.removeFromLeft (tw));
                tabRow.removeFromLeft (4);
                tabExport.setBounds (tabRow.removeFromLeft (tw));
                tabRow.removeFromLeft (4);
                tabTrace .setBounds (tabRow.removeFromLeft (tw));
                tabRow.removeFromLeft (4);
                tabCensus.setBounds (tabRow.removeFromLeft (tw + 20));
                r.removeFromTop (8);

                if (activeTab == 0)
                {
                    header.setBounds (r.removeFromTop (118));
                    r.removeFromTop (8);
                    body.setBounds (r);
                }
                else if (activeTab == 1)
                {
                    notesBody.setBounds (r);
                }
                else if (activeTab == 2)
                {
                    // GENERATE button on top, export text fills the rest.
                    auto btnRow = r.removeFromTop (32);
                    btnGenerate.setBounds (btnRow.removeFromLeft (140));
                    r.removeFromTop (8);
                    exportBody.setBounds (r);
                }
                else if (activeTab == 4)
                {
                    auto btnRow = r.removeFromTop (32);
                    btnCensus.setBounds (btnRow.removeFromLeft (160));
                    r.removeFromTop (10);
                    censusStatus.setBounds (r);
                }
                else  // MIDI TRACE — arm toggle + export button + status text
                {
                    auto top = r.removeFromTop (30);
                    traceArm.setBounds (top.removeFromLeft (160));
                    top.removeFromLeft (8);
                    btnTraceExport.setBounds (top.removeFromLeft (150));
                    r.removeFromTop (8);
                    traceStatus.setBounds (r.removeFromTop (64));
                }
            }

            /** MIDI note → name + octave, Yamaha convention (middle C = C3). */
            static juce::String noteName (int midi)
            {
                static const char* names[12] =
                    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                if (midi < 0 || midi > 127) return "--";
                const int oct = midi / 12 - 2;
                return juce::String (names[midi % 12]) + juce::String (oct);
            }

            //------------------------------------------------------------------
            // Small formatting helpers.
            //------------------------------------------------------------------
            static juce::String pad (int value, int width)
            {
                // -1 ("not set") prints as a dash so the table reads cleanly.
                const juce::String t = (value < 0) ? juce::String ("-")
                                                   : juce::String (value);
                return t.paddedLeft (' ', width);
            }

            static int countCmd (const StyleData& s, int cmdNibble)
            {
                int n = 0;
                for (const auto& e : s.events)
                    if ((e.status & 0xF0) == cmdNibble) ++n;
                return n;
            }

            // Well-known CC names; everything else just shows "CC<n>".
            static juce::String ccName (int cc)
            {
                switch (cc)
                {
                    case 0:   return "Bank MSB";
                    case 32:  return "Bank LSB";
                    case 1:   return "Modulation";
                    case 5:   return "Portamento T";
                    case 6:   return "Data Entry";
                    case 7:   return "Volume";
                    case 10:  return "Pan";
                    case 11:  return "Expression";
                    case 64:  return "Sustain";
                    case 65:  return "Portamento";
                    case 71:  return "Resonance";
                    case 72:  return "Release";
                    case 73:  return "Attack";
                    case 74:  return "Brightness";
                    case 84:  return "Porta Ctrl";
                    case 91:  return "Reverb Send";
                    case 93:  return "Chorus Send";
                    case 94:  return "Variation";
                    case 98:  return "NRPN LSB";
                    case 99:  return "NRPN MSB";
                    case 100: return "RPN LSB";
                    case 101: return "RPN MSB";
                    case 120: return "All Sound Off";
                    case 121: return "Reset Ctrls";
                    case 123: return "All Notes Off";
                    default:  return "CC" + juce::String (cc);
                }
            }

            //------------------------------------------------------------------
            // Header card — the high-level numbers the user asked for first.
            //------------------------------------------------------------------
            static juce::String buildHeader (const StyleData& s)
            {
                juce::String h;
                h << "  " << (s.name.isEmpty()
                                ? Betel::displayNameFor (s.sourceFile)
                                : s.name) << "\n";
                h << "  Format       : " << (s.isSFF2 ? "SFF2 (SFF GE)" : "SFF1") << "\n";
                h << "  Tempo (BPM)  : " << juce::String (s.originalBPM, 2) << "\n";
                h << "  Time Sig     : " << juce::String (s.timeSigNum)
                  << " / " << juce::String (s.timeSigDen) << "\n";
                h << "  PPQ (TPQN)   : " << juce::String (s.ticksPerQuarter) << "\n";
                h << "  CASM / OTS / MF : "
                  << (s.hasCasm ? "CASM " : "-- ")
                  << (s.hasOTSc ? "OTS "  : "-- ")
                  << (s.hasFNRc ? "MF"    : "--");
                return h;
            }

            //------------------------------------------------------------------
            // Body — channel voice table + PC list + CC list.
            //------------------------------------------------------------------
            static juce::String buildBody (const StyleData& s)
            {
                juce::String b;

                // ── Per-channel initial voice setup ──────────────────────────
                b << "INITIAL VOICE SETUP (per source channel)\n";
                b << "----------------------------------------\n";
                b << " CH | bMSB bLSB  PC | VOL  PAN  REV\n";
                bool anyVoice = false;
                for (int ch = 0; ch < 16; ++ch)
                {
                    const auto& v = s.voices[(size_t) ch];
                    if (! v.isUsed()) continue;
                    anyVoice = true;
                    b << " " << pad (ch + 1, 2) << " | "
                      << pad (v.bankMsb, 4) << " " << pad (v.bankLsb, 4) << " "
                      << pad (v.program, 3) << " | "
                      << pad (v.volume, 3) << " " << pad (v.pan, 4) << " "
                      << pad (v.reverb, 4) << "\n";
                }
                if (! anyVoice) b << " (none)\n";
                b << "\n";

                // ── Program changes (tick order, bank-aware) ─────────────────
                b << "PROGRAM CHANGES (" << juce::String (countCmd (s, 0xC0)) << ")\n";
                b << "----------------------------------------\n";
                b << "   TICK  CH | bMSB bLSB  PC\n";
                {
                    int runMsb[16], runLsb[16];
                    for (int i = 0; i < 16; ++i)
                    {
                        runMsb[i] = s.voices[(size_t) i].bankMsb;
                        runLsb[i] = s.voices[(size_t) i].bankLsb;
                    }
                    bool any = false;
                    for (const auto& e : s.events)
                    {
                        const int ch  = e.channel & 0x0F;
                        const int cmd = e.status  & 0xF0;
                        if (cmd == 0xB0)
                        {
                            if      (e.data1 == 0)  runMsb[ch] = e.data2;
                            else if (e.data1 == 32) runLsb[ch] = e.data2;
                        }
                        else if (cmd == 0xC0)
                        {
                            any = true;
                            b << pad (e.tick, 7) << "  " << pad (ch + 1, 2) << " | "
                              << pad (runMsb[ch], 4) << " " << pad (runLsb[ch], 4) << " "
                              << pad ((int) e.data1, 3) << "\n";
                        }
                    }
                    if (! any) b << " (none)\n";
                }
                b << "\n";

                // ── Control changes (tick order) ─────────────────────────────
                // CONTROL CHANGES - ACTIONABLE ONLY.
                //
                // This used to print every CC in the file, which on a style with
                // authored expression rides means thousands of lines (70sDisco2
                // alone carries 3617) and buries the handful that matter.  Grex
                // acts on exactly four: bank select MSB/LSB (0/32, which the
                // following program change needs), Main Volume (7) and
                // Expression (11).  Everything else - pan, reverb/chorus sends,
                // brightness, resonance - is filtered by the dispatcher and can
                // only mislead when read here.
                //
                // The ignored ones are summarised by CC number and count rather
                // than listed, so it stays obvious WHAT the style contains
                // without printing it all.  Actionable CCs are collapsed per
                // channel too: a 300-step expression fade becomes one line with
                // its step count and value range.
                {
                    auto actionable = [] (int cc) noexcept
                    { return cc == 0 || cc == 32 || cc == 7 || cc == 11; };

                    int shown = 0, hidden = 0;
                    std::map<int, int> hiddenByCc;                          // cc -> count
                    std::map<std::pair<int,int>, std::array<int,3>> rides;  // (ch,cc) -> {n,min,max}

                    for (const auto& e : s.events)
                    {
                        if ((e.status & 0xF0) != 0xB0) continue;
                        const int cc = (int) e.data1;
                        if (! actionable (cc)) { ++hidden; ++hiddenByCc[cc]; continue; }
                        ++shown;
                        auto& r = rides[{ (int) (e.channel & 0x0F), cc }];
                        if (r[0]++ == 0) { r[1] = r[2] = (int) e.data2; }
                        else { r[1] = juce::jmin (r[1], (int) e.data2);
                               r[2] = juce::jmax (r[2], (int) e.data2); }
                    }

                    b << "CONTROL CHANGES - acting on " << juce::String (shown)
                      << " of " << juce::String (countCmd (s, 0xB0)) << "\n";
                    b << "----------------------------------------\n";

                    if (rides.empty())
                    {
                        b << " (none)\n";
                    }
                    else
                    {
                        b << " CH | CC  NAME            COUNT   RANGE\n";
                        for (const auto& kv : rides)
                            b << pad (kv.first.first + 1, 3) << " | "
                              << pad (kv.first.second, 3) << " "
                              << ccName (kv.first.second).paddedRight (' ', 15) << " "
                              << pad (kv.second[0], 5) << "   "
                              << pad (kv.second[1], 3) << ".." << pad (kv.second[2], 3)
                              << "\n";
                    }

                    if (hidden > 0)
                    {
                        b << "\nIGNORED BY THE ENGINE (" << juce::String (hidden) << ")\n";
                        for (const auto& kv : hiddenByCc)
                            b << "  CC " << pad (kv.first, 3) << " "
                              << ccName (kv.first).paddedRight (' ', 15) << " x"
                              << juce::String (kv.second) << "\n";
                    }
                }

                return b;
            }

            //------------------------------------------------------------------
            // STYLE NOTES tab — the first 6 note-ons each melodic part channel
            // actually plays in Main A (Variation 1).  Drum channels (bank
            // MSB 127) are skipped.  These are the SOURCE notes as written in
            // the file (before live chord transposition), which is exactly what
            // reveals the register the style was authored in — handy for
            // spotting parts that sit too low or too high.
            //------------------------------------------------------------------
            static juce::String buildNotes (const StyleData& s)
            {
                juce::String b;
                b << "FIRST 6 NOTES PER MELODIC CHANNEL  -  Main A (Var 1)\n";
                b << "source notes as written, before chord transposition\n";
                b << "octave naming: middle C = C3 (MIDI 60), Yamaha style\n";
                b << "----------------------------------------------------\n\n";

                const auto& sec = s.getSection (StyleSection::MainA);
                if (! sec.present)
                {
                    b << "Main A (Var 1) is not present in this style.\n";
                    return b;
                }

                BankProgramMap bank;   // MegaVoice-aware flag, matching playback

                bool any = false;
                for (int ch = 8; ch < 16; ++ch)
                {
                    const auto& v = s.voices[(size_t) ch];
                    if (! v.isUsed())     continue;   // channel not used
                    if (v.bankMsb == 127) continue;   // drum channel — skipped
                    any = true;

                    const int flag = bank.resolve (v.bankMsb, v.bankLsb, v.program);
                    b << "CH " << pad (ch + 1, 2)
                      << "   flag " << pad (flag, 3)
                      << "   (bank " << pad (v.bankMsb, 3) << "/" << pad (v.bankLsb, 3)
                      << "  PC " << pad (v.program, 3) << ")\n";

                    int count = 0;
                    for (size_t idx : sec.eventIdx)
                    {
                        const auto& e = s.events[idx];
                        if ((int) (e.channel & 0x0F) != ch)        continue;
                        if ((e.status & 0xF0) != 0x90)             continue;  // note-on
                        if (e.data2 == 0)                          continue;  // vel 0 = note-off
                        b << "       " << noteName ((int) e.data1).paddedRight (' ', 5)
                          << "(" << pad ((int) e.data1, 3) << ")   vel " << pad ((int) e.data2, 3) << "\n";
                        if (++count >= 6) break;
                    }
                    if (count == 0) b << "       (no notes in Main A)\n";
                    b << "\n";
                }
                if (! any) b << "No melodic channels found.\n";
                return b;
            }

            //------------------------------------------------------------------
            // Notes export — built on demand from the GENERATE button.  Walks
            // every section that's present, pairs note-on / note-off events
            // per (source channel, MIDI note), and emits each note as four
            // comma-separated integers: source channel (1-16), start tick,
            // duration ticks, velocity.  Notes within a (section, channel)
            // group concatenate into a single line, so the format is easy to
            // diff or paste into a spreadsheet / parser.
            //
            // Section header tells you the tick range so absolute ticks in the
            // notes can be interpreted; PPQ is in the file header so the
            // consumer can convert to musical time.
            static juce::String buildNotesExport (const StyleData& s)
            {
                juce::String out;
                out << "Style Notes Export\n";
                out << "==================\n";
                out << "Style       : " << (s.name.isEmpty()
                                                ? Betel::displayNameFor (s.sourceFile)
                                                : s.name) << "\n";
                out << "PPQ         : " << s.ticksPerQuarter << "\n";
                out << "Time Sig    : " << s.timeSigNum << "/" << s.timeSigDen << "\n";
                out << "Format      : ch,startTick,durationTicks,velocity  (one note per 4 fields)\n";
                out << "\n";

                bool anySection = false;

                for (int si = 0; si < kNumStyleSections; ++si)
                {
                    const auto& sec = s.sections[(size_t) si];
                    if (! sec.present) continue;
                    anySection = true;

                    const char* secName = getStyleSectionName (
                        static_cast<StyleSection> (si));
                    const int bars = sec.lengthBars (s.ticksPerQuarter, s.timeSigNum);

                    out << "==== " << secName
                        << "  (ticks " << sec.startTick << "-" << sec.endTick
                        << ",  " << bars << " bar" << (bars == 1 ? "" : "s")
                        << ") ====\n";

                    // For each source channel, pair note-on and note-off events
                    // by note number.  active[note] holds the start tick of an
                    // in-flight note, or -1 when none; activeVel[note] stores
                    // the velocity that started it.  Note-off (0x80, or 0x90
                    // with velocity 0) closes the pair and emits the tuple.
                    bool anyOnSection = false;
                    for (int ch = 0; ch < 16; ++ch)
                    {
                        int activeStart [128];
                        int activeVel   [128];
                        std::fill (activeStart, activeStart + 128, -1);
                        std::fill (activeVel,   activeVel   + 128,  0);

                        juce::String line;

                        for (auto idx : sec.eventIdx)
                        {
                            if (idx >= s.events.size()) continue;
                            const auto& e = s.events[idx];
                            if ((e.channel & 0x0F) != ch) continue;

                            const int cmd  = e.status & 0xF0;
                            const int note = (int) e.data1;
                            const int vel  = (int) e.data2;
                            if (note < 0 || note > 127) continue;

                            if (cmd == 0x90 && vel > 0)
                            {
                                // Close any already-running note on this key
                                // before starting a new one (defensive — most
                                // styles don't overlap on the same key, but
                                // some do, and an orphaned start would dangle).
                                if (activeStart[note] >= 0)
                                {
                                    const int dur = e.tick - activeStart[note];
                                    if (line.isNotEmpty()) line << ",";
                                    line << (ch + 1) << "," << activeStart[note]
                                         << "," << dur << "," << activeVel[note];
                                }
                                activeStart[note] = e.tick;
                                activeVel  [note] = vel;
                            }
                            else if (cmd == 0x80
                                     || (cmd == 0x90 && vel == 0))
                            {
                                if (activeStart[note] >= 0)
                                {
                                    const int dur = e.tick - activeStart[note];
                                    if (line.isNotEmpty()) line << ",";
                                    line << (ch + 1) << "," << activeStart[note]
                                         << "," << dur << "," << activeVel[note];
                                    activeStart[note] = -1;
                                    activeVel  [note] = 0;
                                }
                            }
                        }

                        // Flush still-active notes at section end.  Yamaha
                        // styles occasionally leave notes ringing past the
                        // section marker; truncate them at endTick so the
                        // duration is bounded by the section.
                        for (int n = 0; n < 128; ++n)
                        {
                            if (activeStart[n] >= 0)
                            {
                                const int dur = juce::jmax (0, sec.endTick - activeStart[n]);
                                if (line.isNotEmpty()) line << ",";
                                line << (ch + 1) << "," << activeStart[n]
                                     << "," << dur << "," << activeVel[n];
                            }
                        }

                        if (line.isNotEmpty())
                        {
                            anyOnSection = true;
                            out << "=== CH " << (ch + 1) << " ===\n";
                            out << line << "\n";
                        }
                    }

                    if (! anyOnSection)
                        out << "(no notes in this section)\n";
                    out << "\n";
                }

                if (! anySection)
                    out << "No present sections in this style.\n";

                return out;
            }

            //------------------------------------------------------------------
            // MIDI trace formatter — turns the StylePlayer's captured note
            // events into a column table.  One line per source note-on the
            // dispatcher processed: its source channel/note, the held chord at
            // that moment, the outcome (PLAY or DROP:reason), and the sounding
            // destination when it played.  Compare the src columns against the
            // "NOTES EXPORT" dump to see which notes are missing and why.
            static juce::String buildTraceText (const StylePlayer& sp,
                                                const juce::String& styleName)
            {
                static const char* pcNames[12] =
                    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                auto resultName = [] (uint8_t res) -> const char*
                {
                    switch (res)
                    {
                        case StylePlayer::TR_PLAY:           return "PLAY";
                        case StylePlayer::TR_DROP_MUTED:     return "DROP:MUTED";
                        case StylePlayer::TR_DROP_VARIANT:   return "DROP:VARIANT";
                        case StylePlayer::TR_DROP_TRANSPOSE: return "DROP:TRANSPOSE";
                        case StylePlayer::TR_DROP_PHANTOM:   return "DROP:PHANTOM";
                        case StylePlayer::TR_NOTE_OFF:       return "NOTEOFF";
                        default:                             return "?";
                    }
                };

                const int n = sp.getTraceCount();
                juce::String out;
                out << "Grex MIDI Trace\n";
                out << "===============\n";
                out << "Style   : " << (styleName.isEmpty() ? juce::String ("(unnamed)") : styleName) << "\n";
                out << "Events  : " << n
                    << (sp.traceOverflowed() ? "  (buffer full - truncated)" : "") << "\n";
                out << "Columns : seq | tick | srcCh | srcNote | vel | result | dstCh | dstNote | chord | casm\n";
                out << "          srcCh/dstCh are 1-based; chord is the held chord at the event.\n\n";

                for (int i = 0; i < n; ++i)
                {
                    const auto& r = sp.getTraceRec (i);
                    const juce::String srcNm = noteName ((int) r.srcData1)
                                                 + " (" + juce::String ((int) r.srcData1) + ")";
                    const juce::String dstNm = (r.dstNote == 0xFF)
                                                 ? juce::String ("-")
                                                 : noteName ((int) r.dstNote)
                                                     + " (" + juce::String ((int) r.dstNote) + ")";
                    const juce::String dstChStr = (r.dstCh == 0xFF)
                                                    ? juce::String ("-")
                                                    : juce::String ((int) r.dstCh + 1);
                    const juce::String chordStr = juce::String (pcNames[r.chordRoot % 12])
                          + ":" + (r.chordQual < 34 ? juce::String (kChordTypeNames[r.chordQual])
                                                    : juce::String ((int) r.chordQual));

                    out << pad ((int) r.seq, 6) << "  "
                        << pad ((int) r.tick, 7) << "  "
                        << pad ((int) r.srcCh + 1, 3) << "  "
                        << srcNm.paddedRight (' ', 11) << " "
                        << pad ((int) r.srcData2, 4) << "  "
                        << juce::String (resultName (r.result)).paddedRight (' ', 16)
                        << dstChStr.paddedLeft (' ', 5) << "  "
                        << dstNm.paddedRight (' ', 11) << " "
                        << chordStr.paddedRight (' ', 12) << " "
                        << (r.casmMatched ? "Y" : "N")
                        << "\n";
                }
                if (n == 0)
                    out << "(no events captured)\n";
                return out;
            }

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleDataWindow)
    };
} // namespace Betel



