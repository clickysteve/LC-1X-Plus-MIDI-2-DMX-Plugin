#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "FixtureProfile.h"

// ============================================================================
// LC-1X+ Quick Light — menu bar app
//
// The plugin is for building shows. This is for the other 90% of studio time:
// walk in, want the lights on, don't want to open Logic. It lives in the menu
// bar, holds one colour across the rig, and does nothing else.
//
// It deliberately shares the plugin's fixture model (FixtureProfile.h) and its
// DMX-over-MIDI encoding, so a rig set up here addresses exactly the same
// channels the plugin would drive. It does NOT share project state — the
// plugin's fixture layout lives in the DAW project, which this app has no
// access to, so it keeps its own layout in Application Support.
// ============================================================================

// ----------------------------------------------------------------------------
// One fixture in the quick-light rig. A cut-down FixtureConfig: no patterns,
// no banks, just enough to know which channels to write and how.
// ----------------------------------------------------------------------------
struct QuickFixture {
    juce::String name       { "Fixture 1" };
    int          profileIdx { 0 };
    int          dmxStart   { 0 };     // 0-based
    int          segments   { 8 };

    /// Build the throwaway FixtureConfig the shared mapping code expects.
    FixtureConfig toConfig() const {
        FixtureConfig c(name.toStdString(), segments, dmxStart, profileIdx);
        return c;
    }
};

// ----------------------------------------------------------------------------
// The rig, its output device, and the current look. Owns no UI.
// ----------------------------------------------------------------------------
class QuickLightEngine {
public:
    QuickLightEngine();
    ~QuickLightEngine();

    // ---- Output device ----
    juce::StringArray getOutputDeviceNames() const;
    /// The device the user chose. Remembered even while it isn't connected,
    /// so it can be picked up again the moment it reappears.
    juce::String      getCurrentDeviceName() const  { return deviceName_; }
    /// Whether that device is actually open right now.
    bool              isConnected() const           { return out_ != nullptr; }
    void              setOutputDevice(const juce::String& name);

    /// Close and reopen the chosen device, then push the whole rig state back
    /// out. Runs automatically when the system's MIDI device list changes and
    /// on a watchdog tick; also wired to a menu item, because a CoreMIDI
    /// endpoint can go deaf without the device list changing at all.
    void reconnect();

    // ---- Look ----
    /// Hold every fixture on one colour. Passing black is the same as off.
    void setColour(RGBColor c);
    RGBColor getColour() const                      { return colour_; }

    void  setBrightness(float b);                   // 0..1
    float getBrightness() const                     { return brightness_; }

    void setOn(bool shouldBeOn);
    bool isOn() const                               { return on_; }

    /// Send zeroes to every channel we might have written. Used on "all off"
    /// and on quit, so the rig is never left lit by a process that has gone.
    void blackout();

    // ---- Rig ----
    std::vector<QuickFixture>& fixtures()           { return fixtures_; }
    void rigChanged();                              // re-send + persist

    // ---- Persistence ----
    juce::File settingsFile() const;
    void load();
    void save() const;

private:
    void resend();
    void sendChannel(int channel, int dmxValue);

    /// Resolve deviceName_ against the live device list and open it. Does not
    /// touch the look or persist anything.
    void openDevice();

    /// Periodic connection check, aimed at "the lights stop responding until I
    /// re-pick the device": verify the endpoint we have open is still in the
    /// system's device list, and reopen if it isn't. The device-list callback
    /// should have told us already, but a missed notification costs a whole
    /// session, and re-enumerating MIDI ports every couple of seconds costs
    /// nothing in a menu bar app.
    ///
    /// It deliberately does NOT re-transmit the look on a schedule. The bus is
    /// shared with the plugin; see the note above watchdogTick().
    struct Watchdog : juce::Timer {
        explicit Watchdog(QuickLightEngine& e) : owner(e) {}
        void timerCallback() override;
        QuickLightEngine& owner;
    };
    void watchdogTick();

    std::unique_ptr<juce::MidiOutput> out_;
    juce::String  deviceName_;
    // Identifier of the endpoint actually open, so the watchdog can tell
    // "still there" from "same name, different port".
    juce::String  identifier_;

    std::vector<QuickFixture> fixtures_ { QuickFixture{} };
    RGBColor colour_    { 255, 255, 255 };
    float    brightness_ { 1.0f };
    bool     on_        { false };

    // Last value sent per channel, so a colour change only transmits what
    // actually changed. -1 = unknown, forces a send.
    int lastSent_[128];

    Watchdog watchdog_ { *this };
    // Declared last so it's torn down before anything its callback touches.
    juce::MidiDeviceListConnection deviceListConnection_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickLightEngine)
};
