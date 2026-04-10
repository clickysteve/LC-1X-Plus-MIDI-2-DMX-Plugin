#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

// ============================================================================
// Grid Component — the step sequencer grid (placed inside a Viewport)
// ============================================================================
class GridComponent : public juce::Component {
public:
    DMXControllerProcessor& proc;
    int selectedStep = -1, selectedSeg = -1;
    // Multi-step range selection (columns)
    int selStart = -1, selEnd = -1;
    RGBColor activeColor {255, 0, 0};
    float    brightness  = 1.0f;
    bool     eraseMode   = false;

    float cellW = Theme::BASE_CELL_W, cellH = Theme::BASE_CELL_H;
    float zoom  = 1.0f;
    bool  zoomFit = true;

    // Accumulates wheel deltaY between emitted brightness steps so that
    // trackpad smooth-scroll and notched mouse wheels both produce the
    // same perceived rate of change. See mouseWheelMove().
    float scrollAccum_ = 0.0f;

    explicit GridComponent(DMXControllerProcessor& p) : proc(p) {}

    void recalcSize();
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;

    std::pair<int,int> cellAt(juce::Point<float> pos) const;
    void applyColor(int step, int seg);
};

// ============================================================================
// Bar Preview — LED strip visualisation
// ============================================================================
class BarPreviewComponent : public juce::Component {
public:
    DMXControllerProcessor& proc;
    explicit BarPreviewComponent(DMXControllerProcessor& p) : proc(p) {}
    void paint(juce::Graphics& g) override;
};

// ============================================================================
// Song Timeline
// ============================================================================
class SongTimelineComponent : public juce::Component {
public:
    DMXControllerProcessor& proc;
    int selectedBlock = -1;

    explicit SongTimelineComponent(DMXControllerProcessor& p) : proc(p) {}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    int blockAt(float x) const;
};

// ============================================================================
// Main Editor
// ============================================================================
class DMXControllerEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit DMXControllerEditor(DMXControllerProcessor&);
    ~DMXControllerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    DMXControllerProcessor& proc;
    DarkLookAndFeel darkLnf;

    // ---- Sub‑components ----
    BarPreviewComponent     barPreview;
    GridComponent           grid;
    juce::Viewport          gridViewport;
    SongTimelineComponent   songTimeline;
    juce::Viewport          songViewport;
    juce::ToggleButton      songFollowBtn{"Follow"};

    // ---- Row 1: Transport ----
    juce::TextButton playBtn{"PLAY"}, stopBtn{"STOP"}, resetBtn{"RESET"};
    juce::Label        autoResetLabel{"", "Auto-reset:"};
    juce::ComboBox     autoResetSelector;
    juce::ToggleButton blackoutBtn{"BLACKOUT"};
    juce::TextButton tapBtn{"TAP"};
    juce::TextButton panicBtn{"PANIC"};
    juce::TextButton undoBtn{juce::CharPointer_UTF8("\xe2\x86\xb6")};  // ↶
    juce::TextButton redoBtn{juce::CharPointer_UTF8("\xe2\x86\xb7")};  // ↷
    juce::Slider     bpmSlider;
    juce::Label      bpmLabel;
    juce::Label      clockSrcLabel{"", "Clock:"};
    juce::ComboBox   clockSrcSelector;

    // ---- Row 2: MIDI In/Out ----
    juce::Label      midiInLabel{"",  "MIDI In:"};
    juce::ComboBox   midiInSelector;
    juce::Label      midiOutLabel{"", "MIDI Out:"};
    juce::ComboBox   midiOutSelector;
    juce::TextButton midiOutRefreshBtn{juce::CharPointer_UTF8("\xe2\x86\xbb")};  // ↻

    juce::Label      fixtureLabel{"", "Fixture:"};
    juce::ComboBox   fixtureSelector;
    juce::TextButton addFixBtn{"+"}, delFixBtn{"-"}, dupFixBtn{"Dup"};
    juce::TextButton exportFixBtn{"Export"}, importFixBtn{"Import"};
    juce::Label      segsLabel_{"", "Seg"};
    juce::Slider     segsSlider;
    juce::Label      dmxStartLabel{"", "DMX@"};
    juce::Slider     dmxStartSlider;
    juce::ComboBox   profileSelector;
    juce::TextButton newProfileBtn{"+Profile"};
    juce::TextButton delProfileBtn{"-Profile"};
    juce::Label      fixBrightLabel{"", "Fix Trim"};
    juce::Slider     fixBrightSlider;

    // ---- Row 3: Color palette ----
    static constexpr int kNumColorBtns = NUM_PRESET_COLORS;
    juce::TextButton colorBtns[NUM_PRESET_COLORS];
    juce::Slider     brightnessSlider;
    juce::Label      brightnessLabel{"", "Bright"};
    juce::TextButton fillBtn{"Fill"}, clearBtn{"Clear"}, eraseBtn{"Eraser"};
    juce::TextButton genBtn{"Generate"};
    juce::TextButton renameFixBtn{"Rename"};

    // ---- Row 4: Tools + Live ----
    juce::TextButton copyBtn{"Copy"}, pasteBtn{"Paste"};
    juce::TextButton mirrorBtn{"Mirror"},
                     shiftLBtn{juce::CharPointer_UTF8("\xe2\x86\x90")},  // ←
                     shiftRBtn{juce::CharPointer_UTF8("\xe2\x86\x92")},  // →
                     randBtn{"Random"};
    juce::TextButton midiLearnBtn{"MIDI Learn"};
    juce::ComboBox   fadeSelector;
    juce::Label      fadeLabel{"", "Fade"};
    juce::Label      zoomLabel{"", "Zoom"};
    juce::ComboBox   zoomSelector;

    // ---- Live controls row ----
    juce::Label      masterDimLabel{"", "Master"};
    juce::Slider     masterDimSlider;
    juce::Label      hueLabel{"", "Hue"};
    juce::Slider     hueSlider;
    juce::TextButton hueResetBtn{juce::CharPointer_UTF8("\xe2\x86\xbb")};  // ↻
    juce::Label      swingLabel{"", "Swing"};
    juce::Slider     swingSlider;
    juce::ToggleButton floodToggleBtn{"FLOOD"};
    juce::Label      scenesLabel{"", "Scenes:"};
    juce::TextButton sceneBtns[4];    // A B C D recall
    juce::TextButton sceneStoreBtns[4]; // small store-into-scene

    // ---- Bottom row: Pattern selector ----
    juce::ComboBox   patternSelector;
    juce::TextButton newPatBtn{"+"}, dupPatBtn{"Dup"}, delPatBtn{"Del"};
    juce::TextButton patMoreBtn{juce::CharPointer_UTF8("\xe2\x8b\xaf")};  // ⋯
    juce::TextButton stepsMinBtn{"-"}, stepsPlusBtn{"+"};
    juce::Label      stepsLabel;
    juce::ComboBox   subdivSelector;

    // ---- Song controls ----
    juce::ToggleButton songModeBtn{"Song"};
    juce::TextButton   addBlockBtn{"+Block"}, remBlockBtn{"-Block"}, dupBlockBtn{"DupBlk"};
    juce::TextButton   repPlusBtn{"+"}, repMinusBtn{"-"};

    // Step clipboard (single column or range of columns)
    std::vector<RGBColor> stepClipboard_;
    std::vector<std::vector<RGBColor>> rangeClipboard_;

    // Cached MIDI device lists (refreshed every 2s)
    juce::StringArray cachedMidiOutDevices_;
    juce::StringArray cachedMidiInDevices_;
    int refreshCounter_ = 0;

    // Timer‑based UI refresh
    void timerCallback() override;

    // Helpers
    void refreshPatternSelector();
    void refreshStepsLabel();
    void refreshSubdivSelector();
    void refreshFixtureSelector();
    void refreshMidiDeviceList();
    void refreshAll();
    void selectColor(int idx);
    void applyFixtureEdit();
    void showGeneratorMenu();
    void updateSongTimelineSize();
    void showNewProfileDialog();
    void confirmAndDeleteCurrentUserProfile();
    void refreshProfileSelector();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DMXControllerEditor)
};
