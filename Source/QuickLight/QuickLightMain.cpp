#include "QuickLightApp.h"

// ============================================================================
// LC-1X+ Quick Light
//
// A menu bar app with one job: get the studio lights on without opening a DAW.
// Everything is reachable in two clicks from the menu bar icon, and there is
// no window unless you go looking for the rig setup.
// ============================================================================

namespace {

constexpr int kMenuOffBase        = 1;      // 1
constexpr int kMenuColourBase     = 100;    // 100 + colour index
constexpr int kMenuBrightnessBase = 200;    // 200 + percent/25
constexpr int kMenuDeviceBase     = 300;    // 300 + device index
constexpr int kMenuSetupRig       = 900;
constexpr int kMenuQuit           = 999;

// ----------------------------------------------------------------------------
// Rig setup window. One row per fixture: name, profile, DMX start, segments.
// Same three numbers the plugin needs, so a rig described here matches one
// described there.
// ----------------------------------------------------------------------------
class RigSetupComponent : public juce::Component {
public:
    explicit RigSetupComponent(QuickLightEngine& e) : engine_(e) {
        addAndMakeVisible(addBtn_);
        addAndMakeVisible(removeBtn_);
        addAndMakeVisible(help_);

        help_.setText("Describe the fixtures the way they're addressed on the "
                      "hardware. DMX Start is the address printed on the "
                      "fixture (1-based), Segments is how many separately "
                      "controllable sections it has.",
                      juce::dontSendNotification);
        help_.setJustificationType(juce::Justification::topLeft);
        help_.setFont(juce::FontOptions(12.0f));

        addBtn_.onClick = [this] {
            QuickFixture f;
            // Start the new fixture after the last one so a two-bar rig is
            // correct without touching the numbers.
            if (!engine_.fixtures().empty()) {
                const auto& last = engine_.fixtures().back();
                f.dmxStart = last.dmxStart + last.toConfig().dmxFootprint();
            }
            f.name = "Fixture " + juce::String((int)engine_.fixtures().size() + 1);
            engine_.fixtures().push_back(f);
            rebuild();
            engine_.rigChanged();
        };
        removeBtn_.onClick = [this] {
            if (engine_.fixtures().size() > 1) {
                engine_.fixtures().pop_back();
                rebuild();
                // Must re-render: the removed fixture's channels are still
                // lit, and rigChanged()'s clear pass is what darks them.
                engine_.rigChanged();
            }
        };

        rebuild();
        setSize(560, 320);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        help_.setBounds(r.removeFromTop(48));
        r.removeFromTop(6);

        auto buttons = r.removeFromBottom(28);
        addBtn_   .setBounds(buttons.removeFromLeft(120));
        buttons.removeFromLeft(8);
        removeBtn_.setBounds(buttons.removeFromLeft(140));
        r.removeFromBottom(8);

        for (auto& row : rows_) {
            auto line = r.removeFromTop(28);
            row->name    .setBounds(line.removeFromLeft(130)); line.removeFromLeft(6);
            row->profile .setBounds(line.removeFromLeft(210)); line.removeFromLeft(6);
            row->dmxLabel.setBounds(line.removeFromLeft(64));
            row->dmx     .setBounds(line.removeFromLeft(60));  line.removeFromLeft(6);
            row->segLabel.setBounds(line.removeFromLeft(64));
            row->segs    .setBounds(line.removeFromLeft(60));
            r.removeFromTop(4);
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::darkgrey.darker(0.6f));
    }

private:
    struct Row {
        juce::TextEditor name;
        juce::ComboBox   profile;
        juce::Label      dmxLabel { {}, "DMX @" };
        juce::Slider     dmx { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
        juce::Label      segLabel { {}, "Segs" };
        juce::Slider     segs { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    };

    void rebuild() {
        rows_.clear();

        for (size_t i = 0; i < engine_.fixtures().size(); ++i) {
            auto row = std::make_unique<Row>();
            auto& f  = engine_.fixtures()[i];

            row->name.setText(f.name, juce::dontSendNotification);
            row->name.onTextChange = [this, i] {
                // Just a label — it changes no channel mapping, so it must
                // not go through rigChanged(), which darks the whole rig
                // and rewrites the settings file on every keystroke.
                engine_.fixtures()[i].name = rows_[i]->name.getText();
            };
            row->name.onFocusLost = [this, i] { engine_.save(); };

            const auto& profiles = getFixtureProfiles();
            for (int p = 0; p < (int)profiles.size(); ++p)
                row->profile.addItem(juce::String(profiles[(size_t)p].name), p + 1);
            row->profile.setSelectedId(f.profileIdx + 1, juce::dontSendNotification);
            row->profile.onChange = [this, i] {
                auto& fix = engine_.fixtures()[i];
                fix.profileIdx = rows_[i]->profile.getSelectedId() - 1;
                // Fixed-segment profiles (par cans) dictate their own count.
                const auto& prof = getFixtureProfiles()[(size_t)fix.profileIdx];
                if (prof.fixedSegments > 0) {
                    fix.segments = prof.fixedSegments;
                    rows_[i]->segs.setValue(fix.segments, juce::dontSendNotification);
                }
                rows_[i]->segs.setEnabled(prof.fixedSegments == 0);
                engine_.rigChanged();
            };

            // DMX addresses are 1-based on the hardware and 0-based in code.
            row->dmx.setRange(1, 128, 1);
            row->dmx.setValue(f.dmxStart + 1, juce::dontSendNotification);
            row->dmx.onValueChange = [this, i] {
                engine_.fixtures()[i].dmxStart = (int)rows_[i]->dmx.getValue() - 1;
                engine_.rigChanged();
            };

            row->segs.setRange(1, 32, 1);
            row->segs.setValue(f.segments, juce::dontSendNotification);
            row->segs.onValueChange = [this, i] {
                engine_.fixtures()[i].segments = (int)rows_[i]->segs.getValue();
                engine_.rigChanged();
            };
            {
                const auto& prof = getFixtureProfiles()[(size_t)juce::jlimit(
                    0, (int)getFixtureProfiles().size() - 1, f.profileIdx)];
                row->segs.setEnabled(prof.fixedSegments == 0);
            }

            addAndMakeVisible(row->name);
            addAndMakeVisible(row->profile);
            addAndMakeVisible(row->dmxLabel);
            addAndMakeVisible(row->dmx);
            addAndMakeVisible(row->segLabel);
            addAndMakeVisible(row->segs);
            rows_.push_back(std::move(row));
        }

        resized();
        repaint();
    }

    QuickLightEngine& engine_;
    std::vector<std::unique_ptr<Row>> rows_;
    juce::TextButton addBtn_    { "Add fixture" };
    juce::TextButton removeBtn_ { "Remove last" };
    juce::Label      help_;
};

// ----------------------------------------------------------------------------
// macOS dispatches a status-item menu's selections through JUCE's main-menu
// handler, and that handler drops every selection on the floor unless a
// MenuBarModel has been set (JuceMainMenuHandler::invoke() is guarded on it).
// An agent app has no visible menu bar, so this model is empty; it exists
// purely so that clicking a menu item actually does something.
// ----------------------------------------------------------------------------
struct EmptyMenuBarModel final : juce::MenuBarModel {
    juce::StringArray getMenuBarNames() override { return {}; }
    juce::PopupMenu   getMenuForIndex(int, const juce::String&) override { return {}; }
    void              menuItemSelected(int, int) override {}
};

// ----------------------------------------------------------------------------
// The menu bar icon itself.
// ----------------------------------------------------------------------------
class QuickLightTrayIcon : public juce::SystemTrayIconComponent {
public:
    explicit QuickLightTrayIcon(QuickLightEngine& e) : engine_(e) {
        refreshIcon();
    }

    void mouseDown(const juce::MouseEvent&) override { showMenu(); }

    /// The icon doubles as the status display: a filled dot in the current
    /// colour when the rig is lit, a hollow ring when it isn't. From the menu
    /// bar you can see at a glance whether the lights are on.
    void refreshIcon() {
        constexpr int sz = 22;
        juce::Image img(juce::Image::ARGB, sz, sz, true);
        juce::Graphics g(img);

        const auto c = engine_.getColour();
        const juce::Colour col((juce::uint8)c.r, (juce::uint8)c.g, (juce::uint8)c.b);
        const auto bounds = juce::Rectangle<float>(3.0f, 3.0f, sz - 6.0f, sz - 6.0f);

        if (engine_.isOn()) {
            g.setColour(col);
            g.fillEllipse(bounds);
        } else {
            g.setColour(juce::Colours::grey);
            g.drawEllipse(bounds, 1.8f);
        }
        setIconImage(img, img);
    }

    // Reachable by the menu action lambdas, which go through the instance.
    QuickLightEngine& engine_;

private:
    // Every item carries its own action rather than an id resolved by a
    // result callback. That's required by the macOS path below — the native
    // status-item menu reports selections through the item's action, not
    // through a PopupMenu result — and both menu implementations honour it,
    // so there's one code path instead of two.
    // Defined below QuickLightApplication, which it needs to reach.
    void showMenu();

    static juce::String colourName(int i) {
        static const char* names[] = { "Red", "Orange", "Yellow", "Green",
                                       "Cyan", "Blue", "Purple", "Pink", "White" };
        return (i >= 0 && i < 9) ? names[i] : "Colour";
    }

};

} // namespace

// ----------------------------------------------------------------------------
class QuickLightApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "LC-1X+ Quick Light"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override {
       #if JUCE_MAC
        // Must come before the tray icon: see EmptyMenuBarModel.
        juce::MenuBarModel::setMacMainMenu(&menuModel_);
       #endif
        engine_ = std::make_unique<QuickLightEngine>();
        tray_   = std::make_unique<QuickLightTrayIcon>(*engine_);
    }

    void shutdown() override {
        // Order matters. The rig window's controls hold a reference to the
        // engine and can call into it as they are torn down — a focused
        // slider text box hands focus away on destruction, which fires
        // onValueChange. So the window goes first, then the icon, then the
        // engine it all points at.
        rigWindow_.reset();
        tray_.reset();
        engine_.reset();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
       #endif
    }

    void systemRequestedQuit() override { quit(); }

    static QuickLightApplication& get() {
        return *dynamic_cast<QuickLightApplication*>(JUCEApplication::getInstance());
    }

    QuickLightEngine& engine()          { return *engine_; }
    QuickLightTrayIcon* tray()          { return tray_.get(); }

    void showRigSetup() {
        // An agent app (LSUIElement) is never the foreground application, so
        // a window it opens goes up behind whatever you were using — or on
        // some setups doesn't appear to open at all. toFront() alone isn't
        // enough; the process itself has to be brought forward first.
        juce::Process::makeForegroundProcess();

        if (rigWindow_ != nullptr) {
            rigWindow_->setVisible(true);
            rigWindow_->toFront(true);
            return;
        }

        class Window : public juce::DocumentWindow {
        public:
            Window(QuickLightEngine& e)
                : DocumentWindow("Quick Light - Rig", juce::Colours::darkgrey,
                                 DocumentWindow::closeButton) {
                setUsingNativeTitleBar(true);
                setContentOwned(new RigSetupComponent(e), true);
                setResizable(true, false);      // a long rig needs the room
                centreWithSize(juce::jmax(560, getWidth()),
                               juce::jmax(320, getHeight()));
                setVisible(true);
                toFront(true);
            }
            void closeButtonPressed() override {
                QuickLightApplication::get().closeRigSetup();
            }
        };
        rigWindow_ = std::make_unique<Window>(*engine_);
    }

    void closeRigSetup() {
        // Deleting the window from inside its own closeButtonPressed would
        // destroy the object we are currently executing a method of.
        juce::MessageManager::callAsync([this] {
            rigWindow_.reset();
            // Nothing left on screen, so hand the foreground back rather
            // than leaving a menu bar app sitting in front of everything.
            juce::Process::hide();
        });
    }

private:
    EmptyMenuBarModel                   menuModel_;
    std::unique_ptr<QuickLightEngine>   engine_;
    std::unique_ptr<QuickLightTrayIcon> tray_;
    std::unique_ptr<juce::DocumentWindow> rigWindow_;
};

// Defined out of line: it reaches QuickLightApplication, declared above.
void QuickLightTrayIcon::showMenu() {
    juce::PopupMenu menu;
    juce::Component::SafePointer<QuickLightTrayIcon> safeThis(this);

    // The action is invoked asynchronously, so it must not assume the
    // icon still exists: quitting from the menu destroys it.
    auto act = [safeThis](std::function<void(QuickLightTrayIcon&)> fn) {
        return [safeThis, fn] {
            if (auto* self = safeThis.getComponent()) {
                fn(*self);
                self->refreshIcon();
            }
        };
    };

    const bool on = engine_.isOn();
    menu.addItem(on ? "Turn lights off" : "Turn lights on",
                 act([](QuickLightTrayIcon& s) { s.engine_.setOn(!s.engine_.isOn()); }));
    menu.addSeparator();

    // Colours. The palette is the plugin's, minus its "black = off" tile,
    // which the item above already covers.
    juce::PopupMenu colours;
    const auto cur = engine_.getColour();
    for (int i = 0; i < NUM_PRESET_COLORS - 1; ++i) {
        const auto pc = PRESET_COLORS[i];
        juce::PopupMenu::Item item(colourName(i));
        item.itemID   = kMenuColourBase + i;   // native menus need a non-zero id
        item.isTicked = on && pc.r == cur.r && pc.g == cur.g && pc.b == cur.b;
        item.colour   = juce::Colour((juce::uint8)pc.r, (juce::uint8)pc.g,
                                     (juce::uint8)pc.b);
        item.action   = act([pc](QuickLightTrayIcon& s) { s.engine_.setColour(pc); });
        colours.addItem(item);
    }
    menu.addSubMenu("Colour", colours);

    juce::PopupMenu bright;
    for (int pct = 25; pct <= 100; pct += 25) {
        const float level = pct / 100.0f;
        juce::PopupMenu::Item item(juce::String(pct) + "%");
        item.itemID   = kMenuBrightnessBase + pct / 25;
        item.isTicked = std::abs(engine_.getBrightness() - level) < 0.01f;
        item.action   = act([level](QuickLightTrayIcon& s) { s.engine_.setBrightness(level); });
        bright.addItem(item);
    }
    menu.addSubMenu("Brightness", bright);

    menu.addSeparator();

    juce::PopupMenu devices;
    const auto names = engine_.getOutputDeviceNames();
    if (names.isEmpty()) {
        devices.addItem(-1, "No MIDI outputs found", false, false);
    } else {
        for (int i = 0; i < names.size(); ++i) {
            const auto name = names[i];
            juce::PopupMenu::Item item(name);
            item.itemID   = kMenuDeviceBase + i;
            item.isTicked = (name == engine_.getCurrentDeviceName());
            item.action   = act([name](QuickLightTrayIcon& s) {
                                    s.engine_.setOutputDevice(name); });
            devices.addItem(item);
        }
    }
    menu.addSubMenu("MIDI output", devices);

    menu.addItem("Set up rig...", [] {
        QuickLightApplication::get().showRigSetup();
    });

    menu.addSeparator();
    menu.addItem("Quit", [] {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });

   #if JUCE_MAC
    // A status-bar menu has to be the NSStatusItem's own menu. Showing a
    // JUCE PopupMenu from the tray icon's mouseDown puts up a window that
    // the status item's own event handling dismisses on the very next
    // event, so the menu flashes and vanishes.
    showDropdownMenu(menu);
   #else
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
   #endif
}

START_JUCE_APPLICATION(QuickLightApplication)
