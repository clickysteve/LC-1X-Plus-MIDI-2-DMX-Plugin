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
                                private juce::MidiInputCallback,
                                private juce::AsyncUpdater,
                                private juce::AudioProcessorValueTreeState::Listener
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
    std::atomic<int> activeFixture {0};
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
    // Default = Last Step so new instances behave correctly with MIDI
    // clock out of the box (no "first step skipped" surprise).
    std::atomic<int>  autoResetMode  {2};
    std::atomic<int>  currentStep    {0};
    std::atomic<double> bpm {120.0};

    // Clock source: 0=Internal, 1=MIDI Clock, 2=Host Sync (sample-accurate
    // PPQ tracking of the DAW playhead; follows tempo changes, loops and
    // locates, and applies swing exactly on the grid).
    // Default = MIDI Clock since that's the intended use on a MIDI track.
    std::atomic<int>  clockSource{1};

    // Last known tempo reported by the DAW (via AudioPlayHead). Updated
    // every processBlock and read by the editor so the BPM display can
    // reflect the host's tempo while MIDI clock mode is active.
    std::atomic<double> hostBpm {120.0};

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
    std::atomic<bool> songModeActive {false};

    // Crossfade (steps over which a pattern change blends from the old
    // pattern's colours to the new pattern's; 0 = off)
    std::atomic<int> crossfadeSteps {0};

    // Select a pattern in the active fixture's bank, capturing the outgoing
    // colours first so applyCrossfade() can blend into the new pattern.
    // Safe to call from any thread (takes dataLock; recursive).
    void selectPatternWithCrossfade(int idx);

    // Incremented every time a scene is loaded via a MIDI mapping so the
    // editor's timer can notice and refresh its controls.
    std::atomic<int> sceneLoadBroadcast {0};

    // =======================================================================
    // Host-automatable parameters (APVTS)
    //
    // The atomics above (masterDimmer, hueShiftDeg, swing, blackoutActive,
    // floodActive, floodColor) remain the realtime-read copies; the
    // parameters are the host-facing source of truth and parameterChanged()
    // mirrors every change into the corresponding atomic. UI and MIDI-learn
    // both write through the parameters so host automation always agrees
    // with what the plugin is doing.
    // =======================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Select a pattern AND reflect it into the "Pattern" parameter so the
    // host sees manual selections. Use this from UI / MIDI paths.
    void selectPatternNotifyingHost(int idx);

    // Set flood state through the parameters (colorIdx -1 = leave colour).
    void setFloodParams(bool active, int colorIdx);

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
    // Atomic: incremented by whichever thread delivers MIDI clock (audio
    // thread for host MIDI, MIDI-input callback thread for direct devices).
    std::atomic<int> midiClockCount_ {0};

    // --- host transport tracking (for auto-reset on DAW stop) ---
    bool prevHostPlaying_ = false;

    // --- Host Sync (clockSource == 2) ---
    // Index of the last step boundary we emitted, as an UNWRAPPED global
    // step count derived from the playhead PPQ position. Invalidated on
    // stop, backward jumps (loops) and large forward jumps (locates) so
    // playback re-locks to the grid instead of machine-gunning catch-up
    // steps.
    juce::int64 lastHostStep_  = -1;
    bool        hostStepValid_ = false;
    void applyHostStep(juce::int64 globalStep);   // caller holds dataLock

    // --- APVTS plumbing ---
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncAtomicsFromParams();

    // Live previews driven by parameter moves are coalesced to ~40 Hz.
    // Without this, a smooth automation ramp fires one preview per audio
    // block (~86/s at 512 samples) and each one re-sends every changed
    // channel — enough to saturate a 31250-baud MIDI link.
    std::atomic<bool> previewDirty_ {false};
    bool   previewRetryPending_ = false;
    double lastPreviewMs_       = 0.0;
    static constexpr double kPreviewIntervalMs = 25.0;
    void   servicePreview();
    // Lets a delayed callback know the processor is still alive.
    std::shared_ptr<std::atomic<bool>> aliveFlag_
        { std::make_shared<std::atomic<bool>>(true) };
    juce::AudioParameterFloat*  pMasterDim_   = nullptr;
    juce::AudioParameterFloat*  pHue_         = nullptr;
    juce::AudioParameterFloat*  pSwing_       = nullptr;
    juce::AudioParameterBool*   pBlackout_    = nullptr;
    juce::AudioParameterBool*   pFloodActive_ = nullptr;
    juce::AudioParameterChoice* pFloodColor_  = nullptr;
    juce::AudioParameterInt*    pPattern_     = nullptr;

    // --- deferred scene load (MIDI SceneLoad mappings) ---
    // setStateInformation is far too heavy for the audio / MIDI threads
    // (XML parse, allocation, MIDI device open), so parseIncomingMidi only
    // records the request here and triggers an async update; the actual
    // load happens on the message thread in handleAsyncUpdate().
    std::atomic<int> pendingSceneLoad_ {-1};
    void handleAsyncUpdate() override;

    // --- host MIDI output routing ---
    // Non-null only while processBlock is running (set/cleared under
    // dataLock). emitDmxDelta additionally writes into this buffer so the
    // host's MIDI FX chain receives the DMX note stream too.
    juce::MidiBuffer* hostMidiOut_  = nullptr;
    int               hostSamplePos_ = 0;
    // Reused across blocks (pre-sized in prepareToPlay) so building the
    // outgoing DMX stream doesn't allocate on the audio thread.
    juce::MidiBuffer  generatedMidi_;

    // --- DMX delta tracking ---
    int dmxState_    [128];
    int prevDmxState_[128];

    // --- Crossfade ---
    // Fixed-size (not std::vector) so capturing the outgoing colours on a
    // pattern change never allocates — song mode and the Pattern parameter
    // both trigger this from the audio thread.
    static constexpr int kMaxSegments     = 64;
    static constexpr int kMaxChannelPairs = 512;
    RGBColor crossfadeFrom_[kMaxSegments] {};
    int      crossfadeFromCount_ = 0;
    int      crossfadeProgress_  = 0;

    // Scratch buffers for computeDmxState(). Only touched under dataLock,
    // so a single shared copy is safe and keeps the audio thread's stack
    // (and the allocator) out of the picture entirely.
    RGBColor           colorBuf_[kMaxSegments] {};
    std::pair<int,int> pairBuf_[kMaxChannelPairs] {};

    // --- Direct MIDI I/O ---
    juce::CriticalSection             midiOutLock_;
    std::unique_ptr<juce::MidiOutput> directMidiOut_;
    juce::String                      currentMidiOutName_;

    // ---- Realtime-safe DMX output path ----------------------------------
    // emitDmxDelta() can run on the audio thread (host sync / MIDI clock),
    // where a CoreMIDI sendMessageNow() syscall — or blocking on
    // midiOutLock_ while the message thread is inside openDevice() — is a
    // dropout waiting to happen. Instead the realtime side pushes packed
    // 3-byte messages into a lock-free FIFO and a dedicated sender thread
    // performs the actual device writes. DMX refreshes at ~44 Hz, so the
    // sub-millisecond handoff latency is inaudible (and invisible).
    class MidiOutSender : private juce::Thread {
    public:
        explicit MidiOutSender(DMXControllerProcessor& o);
        ~MidiOutSender() override;

        /// Realtime-safe: never allocates, never blocks. Returns false if
        /// the queue is full (caller then forces a full resend).
        bool push(juce::uint8 status, juce::uint8 d1, juce::uint8 d2) noexcept;

    private:
        void run() override;

        static constexpr int kCapacity = 8192;
        DMXControllerProcessor& owner_;
        juce::AbstractFifo      fifo_ { kCapacity };
        std::array<juce::uint32, (size_t)kCapacity> slots_ {};
        juce::WaitableEvent     wake_;
    };
    std::unique_ptr<MidiOutSender> midiSender_;
    void sendPackedToDevice(juce::uint32 packed);   // sender thread only

    // Set when the FIFO overflowed: the next emit re-sends every channel so
    // the hardware can't be left holding a stale value.
    std::atomic<bool> outputDesynced_ {false};

    juce::CriticalSection             midiInLock_;
    std::unique_ptr<juce::MidiInput>  directMidiIn_;
    juce::String                      currentMidiInName_;

    // --- HighResolutionTimer ---
    void hiResTimerCallback() override;
    void updateClockTimer();

    // --- MidiInputCallback ---
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    // --- State serialisation ---
    // getStateInformation forwards here with includeScenes = true.
    // storeScene uses includeScenes = false so scene snapshots don't nest
    // the other scenes' blobs inside themselves (which made the saved
    // state grow geometrically with every store).
    void writeState(juce::MemoryBlock& dest, bool includeScenes);

    // --- Core helpers ---
    void advanceStep();
    // advanceFades: only a real step advance moves a crossfade along. Live
    // previews (parameter moves, UI edits) recompute the same frame and
    // must NOT consume fade progress, or a fader wiggle would blow through
    // an 8-step fade in a few milliseconds.
    void computeDmxState(bool advanceFades = false);
    void emitDmxDelta(juce::MidiBuffer* buf, int sampleOffset);
    void parseIncomingMidi(const juce::MidiMessage& msg);
    static int dmxToVelocity(int dmx);
    void applyCrossfadeInPlace(RGBColor* colors, int n, bool advance);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DMXControllerProcessor)
};
