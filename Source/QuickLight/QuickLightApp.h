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
    juce::String      getCurrentDeviceName() const  { return deviceName_; }
    void              setOutputDevice(const juce::String& name);

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

    std::unique_ptr<juce::MidiOutput> out_;
    juce::String  deviceName_;

    std::vector<QuickFixture> fixtures_ { QuickFixture{} };
    RGBColor colour_    { 255, 255, 255 };
    float    brightness_ { 1.0f };
    bool     on_        { false };

    // Last value sent per channel, so a colour change only transmits what
    // actually changed. -1 = unknown, forces a send.
    int lastSent_[128];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickLightEngine)
};
