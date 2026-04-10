#pragma once
#include <JuceHeader.h>
#include "PatternData.h"
#include "SongMode.h"
#include "FixtureProfile.h"

// ============================================================================
// DMX Controller Audio Processor
// ============================================================================
class DMXControllerProcessor : public juce::AudioProcessor,
                                private juce::HighResolutionTimer,
                                private juce::MidiInputCallback
{
public:
    DMXControllerProcessor();
    ~DMXControllerProcessor() override;

    // --- AudioProcessor overrides ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // =======================================================================
    // Public state
    // =======================================================================
    std::vector<FixtureConfig> fixtures;
    int activeFixture = 0;
    PatternBank& currentBank();

    // Protects mutation / read of `fixtures` and their PatternBanks across
    // the message thread (UI), audio thread (processBlock), and the
    // HighResolutionTimer thread. Any UI code that reassigns / resizes a
    // Pattern, mutates fix.numSegments, fix.profileIndex, or the fixtures
    // vector itself MUST hold this lock. The consumer paths
    // (computeDmxState, advanceStep, MIDI clock step, host transport reset)
    // also acquire it.
    juce::CriticalSection dataLock;

    void addFixture();
    void removeFixture(int idx);
    void duplicateFixture(int idx);
    void renumberDefaultFixtures();

    // Export / import single fixture as JSON (returns XML-style JUCE var string)
    juce::String serializeFixture(int idx) const;
    bool         deserializeFixtureInto(int idx, const juce::String& json);

    // Export / import a single pattern as JSON
    juce::String serializePattern(int fixtureIdx, int patIdx) const;
    bool         deserializePatternInto(int fixtureIdx, int patIdx, const juce::String& json);

    // Transport
    std::atomic<bool> isPlaying      {false};
    // 0 = Off (don't reset on stop)
    // 1 = 1st Step (reset currentStep to 0 on stop)
    // 2 = Last Step (reset currentStep to numSteps-1 on stop; first MIDI
    //     clock tick after start wraps cleanly back to step 0, which
    //     avoids the "first step is skipped" problem on clock start).
    std::atomic<int>  autoResetMode  {0};
    std::atomic<int>  currentStep    {0};
    std::atomic<bool> needsInitialSend{false};
    double bpm = 120.0;

    // Clock source: 0=Internal, 1=MIDI Clock
    std::atomic<int>  clockSource{0};

    // ==== Live global controls ====
    std::atomic<float> masterDimmer {1.0f};   // 0..1
    std::atomic<float> hueShiftDeg  {0.0f};   // -180..+180
    std::atomic<bool>  blackoutActive {false};
    // ==== Flood (single-colour live override) ====
    // floodMode: if true, clicking a colour button floods instead of painting.
    // floodActive: is a flood currently being output?
    // floodColor: packed 0x00RRGGBB of the current flood colour.
    std::atomic<bool>     floodMode   {false};
    std::atomic<bool>     floodActive {false};
    std::atomic<uint32_t> floodColor  {0};
    std::atomic<float> swing         {0.0f};  // 0..0.5

    // ==== Undo / Redo ====
    struct HistoryEntry {
        int  fixtureIdx;
        int  patternIdx;
        Pattern snap;
    };
    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    void snapshot();   // push current pattern state to undo, clear redo
    bool undo();
    bool redo();

    // ==== Scene snapshots A..D ====
    struct Scene { juce::MemoryBlock data; bool occupied = false; };
    Scene scenes[4];
    void storeScene(int idx);
    bool loadScene(int idx);

    // Song
    Song       song;
    SongPlayer songPlayer;
    bool       songModeActive = false;

    // Crossfade
    int crossfadeSteps = 0;

    // ==== Expanded MIDI Learn ====
    enum class MidiTarget : int {
        None = 0,
        PatternSelect,    // param = pattern index
        BpmCC,            // CC → BPM
        BrightnessCC,     // CC → grid brightness (stored in live editor)
        MasterDimCC,      // CC → master dimmer
        HueCC,            // CC → hue shift
        SatCC,            // CC → saturation
        SwingCC,          // CC → swing
        BlackoutToggle,   // note → toggle blackout
        FillAll,          // note → fill all with active colour
        Generate,         // note → run a generator (param=generator id)
        SceneLoad,        // note → load scene (param=0..3)
        Panic             // note → panic blackout
    };
    struct MidiMapping {
        int msgType;   // 0x90=note, 0xB0=CC, 0xC0=PC
        int data1;     // note#, cc#, or program#
        MidiTarget target = MidiTarget::PatternSelect;
        int param = 0; // pattern index / scene index / generator id
    };
    std::vector<MidiMapping> midiMappings;
    std::atomic<bool> midiLearnActive{false};
    MidiTarget midiLearnTargetType = MidiTarget::PatternSelect;
    int        midiLearnTargetParam = -1;

    std::atomic<bool> previewRequested{false};

    // GUI-callable transport
    void startPlayback();
    void stopPlayback();
    void resetPlayback();
    void panicBlackout();

    // Tap-tempo support
    void tapTempo();
    std::vector<double> tapTimes_;

    // MIDI learn helpers called by editor for arbitrary targets
    void beginMidiLearn(MidiTarget target, int param);
    void clearMidiMapping(MidiTarget target, int param);
    bool findMappingFor(MidiTarget target, int param, MidiMapping& out) const;

    // Brightness input (CC mapped) — editor subscribes via polling
    std::atomic<float> brightnessLive {1.0f};

    // ======================================================================
    // Direct MIDI devices
    // ======================================================================
    juce::StringArray  getMidiOutputDeviceNames();
    void               setMidiOutputDevice(const juce::String& name);
    juce::String       getMidiOutputDeviceName() const { return currentMidiOutName_; }

    juce::StringArray  getMidiInputDeviceNames();
    void               setMidiInputDevice(const juce::String& name);
    juce::String       getMidiInputDeviceName() const { return currentMidiInName_; }

    void pushPreview();

private:
    // --- audio / timing ---
    double sampleRate_     = 44100.0;
    double sampleCounter_  = 0.0;
    int    midiClockCount_ = 0;

    // --- host transport tracking (for DAW sync) ---
    std::atomic<bool>  hostIsPlaying_{false};
    std::atomic<double> hostBpm_{120.0};
    int prevHostStep_ = -1;
    bool prevHostPlaying_ = false;

    // --- DMX delta tracking ---
    int dmxState_    [128];
    int prevDmxState_[128];

    // --- Crossfade ---
    std::vector<RGBColor> crossfadeFrom_;
    int crossfadeProgress_ = 0;

    // --- Direct MIDI I/O ---
    juce::CriticalSection             midiOutLock_;
    std::unique_ptr<juce::MidiOutput> directMidiOut_;
    juce::String                      currentMidiOutName_;

    juce::CriticalSection             midiInLock_;
    std::unique_ptr<juce::MidiInput>  directMidiIn_;
    juce::String                      currentMidiInName_;

    // --- HighResolutionTimer ---
    void hiResTimerCallback() override;
    void updateClockTimer();

    // --- MidiInputCallback ---
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    // --- Core helpers ---
    void advanceStep();
    void computeDmxState();
    void emitDmxDelta(juce::MidiBuffer* buf, int sampleOffset);
    void parseIncomingMidi(const juce::MidiMessage& msg);
    static int dmxToVelocity(int dmx);
    std::vector<RGBColor> applyCrossfade(const std::vector<RGBColor>& colors);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DMXControllerProcessor)
};
