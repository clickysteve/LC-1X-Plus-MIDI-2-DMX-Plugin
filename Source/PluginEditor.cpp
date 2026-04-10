#include "PluginEditor.h"
#include "UserProfileStore.h"

using namespace juce;

// ============================================================================
// Shared colour helpers for editor preview components
// ============================================================================
static RGBColor rotateHue(RGBColor c, float deg) {
    if (deg == 0.0f) return c;
    float r = c.r / 255.0f, gg = c.g / 255.0f, b = c.b / 255.0f;
    float mx = std::max({r, gg, b}), mn = std::min({r, gg, b});
    float v = mx, d = mx - mn;
    float s = mx == 0.0f ? 0.0f : d / mx;
    float h = 0.0f;
    if (d != 0.0f) {
        if      (mx == r)  h = 60.0f * std::fmod(((gg - b) / d), 6.0f);
        else if (mx == gg) h = 60.0f * (((b - r) / d) + 2.0f);
        else               h = 60.0f * (((r - gg) / d) + 4.0f);
    }
    if (h < 0) h += 360.0f;
    h = std::fmod(h + deg + 360.0f, 360.0f);
    float hh2 = h / 60.0f;
    float cc = v * s;
    float xx = cc * (1.0f - std::abs(std::fmod(hh2, 2.0f) - 1.0f));
    float m  = v - cc;
    float rr, gr, br;
    if      (hh2 < 1) { rr = cc; gr = xx; br = 0;  }
    else if (hh2 < 2) { rr = xx; gr = cc; br = 0;  }
    else if (hh2 < 3) { rr = 0;  gr = cc; br = xx; }
    else if (hh2 < 4) { rr = 0;  gr = xx; br = cc; }
    else if (hh2 < 5) { rr = xx; gr = 0;  br = cc; }
    else              { rr = cc; gr = 0;  br = xx; }
    return {(uint8_t)std::clamp((int)std::round((rr + m) * 255), 0, 255),
            (uint8_t)std::clamp((int)std::round((gr + m) * 255), 0, 255),
            (uint8_t)std::clamp((int)std::round((br + m) * 255), 0, 255)};
}

static RGBColor unpackFloodColor(uint32_t packed) {
    return { (uint8_t)((packed >> 16) & 0xFF),
             (uint8_t)((packed >>  8) & 0xFF),
             (uint8_t)( packed        & 0xFF) };
}

static uint32_t packFloodColor(RGBColor c) {
    return (((uint32_t)c.r) << 16) | (((uint32_t)c.g) << 8) | (uint32_t)c.b;
}

// ############################################################################
//  GridComponent
// ############################################################################

void GridComponent::recalcSize() {
    auto* pat = proc.currentBank().current();
    if (!pat) return;

    if (zoomFit) {
        // Fit both dimensions to the parent viewport
        auto* vp = getParentComponent();
        int vpW = vp ? vp->getWidth()  : 800;
        int vpH = vp ? vp->getHeight() : 400;
        float availW = (float)vpW - Theme::LABEL_W - 6;
        float availH = (float)vpH - Theme::HEADER_H - 6;
        float scaleW = availW / (pat->numSteps    * (float)Theme::BASE_CELL_W);
        float scaleH = availH / (pat->numSegments * (float)Theme::BASE_CELL_H);
        float scale  = juce::jlimit(0.25f, 4.0f, std::min(scaleW, scaleH));
        cellW = Theme::BASE_CELL_W * scale;
        cellH = Theme::BASE_CELL_H * scale;
    } else {
        cellW = Theme::BASE_CELL_W * zoom;
        cellH = Theme::BASE_CELL_H * zoom;
    }

    int w = Theme::LABEL_W + (int)(pat->numSteps    * cellW) + 6;
    int h = Theme::HEADER_H + (int)(pat->numSegments * cellH) + 6;
    setSize(w, h);
}

void GridComponent::paint(Graphics& g) {
    auto* pat = proc.currentBank().current();
    if (!pat) { g.fillAll(Theme::GRID_BG); return; }

    int steps = pat->numSteps;
    int segs  = pat->numSegments;
    int lw    = Theme::LABEL_W;
    int hh    = Theme::HEADER_H;

    g.fillAll(Theme::GRID_BG);

    // ---- Live global modifiers (mirrored from the processor) ----
    const float  hueDeg     = proc.hueShiftDeg.load();
    const bool   flood      = proc.floodActive.load();
    const auto   floodCol   = unpackFloodColor(proc.floodColor.load());
    const bool   blackout   = proc.blackoutActive.load();

    // Step headers
    for (int s = 0; s < steps; s++) {
        float x   = lw + s * cellW;
        bool beat = (s % pat->subdiv == 0);
        g.setColour(beat ? Theme::ACCENT : Theme::FG_DIM);
        g.setFont(beat ? 13.0f : 10.5f);
        g.drawText(String(s + 1), (int)x, 0, (int)cellW, hh, Justification::centred);
    }

    // Segment labels
    g.setFont(11.0f);
    for (int seg = 0; seg < segs; seg++) {
        float y = hh + seg * cellH;
        g.setColour(Theme::FG_COLOR);
        g.drawText("S" + String(seg + 1), 0, (int)y, lw, (int)cellH, Justification::centred);
    }

    // Grid cells (with live hue shift + flood + blackout applied visually)
    for (int s = 0; s < steps; s++) {
        float x   = lw + s * cellW;
        bool beat = (s % pat->subdiv == 0);
        for (int seg = 0; seg < segs; seg++) {
            float y = hh + seg * cellH;
            auto c  = pat->getColor(s, seg);

            // Apply live global modifiers so the grid preview matches the
            // actual DMX output
            if (blackout) {
                c = {0, 0, 0};
            } else if (flood) {
                // Flood replaces the colour entirely (hue shift does
                // nothing to a "user flooded red" command)
                c = floodCol;
            } else if (hueDeg != 0.0f) {
                c = rotateHue(c, hueDeg);
            }

            // Grid-visualisation-only brightness floor. The scroll-wheel
            // dim handler floors at ~DMX 8 so you can always scroll back
            // up, and the hardware will dutifully output a dim colour —
            // but on the editor canvas, DMX 8 is indistinguishable from
            // "empty cell" against the dark background. Boost the
            // displayed colour so any cell with any light in it is
            // clearly visible. This does NOT affect DMX output.
            if (!blackout) {
                const int cpeak = std::max({c.r, c.g, c.b});
                if (cpeak > 0 && cpeak < 80) {
                    const float bump = 80.0f / (float)cpeak;
                    c.r = (uint8_t)std::clamp((int)std::round(c.r * bump), 0, 255);
                    c.g = (uint8_t)std::clamp((int)std::round(c.g * bump), 0, 255);
                    c.b = (uint8_t)std::clamp((int)std::round(c.b * bump), 0, 255);
                }
            }

            g.setColour(Colour(c.r, c.g, c.b));
            g.fillRect(x + 1.0f, y + 1.0f, cellW - 2.0f, cellH - 2.0f);

            g.setColour(beat ? Theme::GRID_LINE_BEAT : Theme::GRID_LINE);
            g.drawRect(x, y, cellW, cellH, 1.0f);
        }
    }

    // Playhead
    int step = proc.currentStep.load();
    if (step >= 0 && step < steps) {
        float x = lw + step * cellW;
        g.setColour(Theme::STEP_HIGHLIGHT);
        g.drawRect(x, (float)hh, cellW, segs * cellH, 2.0f);
    }

    // Range (multi-column) selection — highlight first
    if (selStart >= 0 && selEnd >= 0 && selEnd > selStart) {
        int a = std::max(0, selStart);
        int b = std::min(steps - 1, selEnd);
        float x = lw + a * cellW;
        float w = (b - a + 1) * cellW;
        g.setColour(Colours::white.withAlpha(0.15f));
        g.fillRect(x, (float)hh, w, segs * cellH);
        g.setColour(Theme::ACCENT);
        g.drawRect(x, (float)hh, w, segs * cellH, 2.0f);
    }

    // Selection
    g.setColour(Colours::white);
    if (selectedStep >= 0 && selectedSeg >= 0 &&
        selectedStep < steps && selectedSeg < segs)
    {
        float x = lw + selectedStep * cellW;
        float y = hh + selectedSeg * cellH;
        g.drawRect(x, y, cellW, cellH, 2.0f);
    }
    else if (selectedStep >= 0 && selectedStep < steps && selectedSeg < 0 &&
             !(selStart >= 0 && selEnd > selStart))
    {
        // Whole column
        float x = lw + selectedStep * cellW;
        g.drawRect(x, (float)hh, cellW, segs * cellH, 2.0f);
    }
    else if (selectedSeg >= 0 && selectedSeg < segs && selectedStep < 0) {
        // Whole row
        float y = hh + selectedSeg * cellH;
        g.drawRect((float)lw, y, steps * cellW, cellH, 2.0f);
    }
}

std::pair<int,int> GridComponent::cellAt(Point<float> pos) const {
    auto* pat = proc.currentBank().current();
    if (!pat) return {-1, -1};
    int step = (int)((pos.x - Theme::LABEL_W) / cellW);
    int seg  = (int)((pos.y - Theme::HEADER_H) / cellH);
    if (step < 0 || step >= pat->numSteps || seg < 0 || seg >= pat->numSegments)
        return {-1, -1};
    return {step, seg};
}

void GridComponent::applyColor(int step, int seg) {
    auto* pat = proc.currentBank().current();
    if (!pat) return;
    if (eraseMode) {
        pat->setColor(step, seg, {0, 0, 0});
    } else {
        RGBColor c = activeColor;
        // The palette brightness slider is 0..1 in perceptual space, but
        // the actual RGB scaling has to happen in linear (DMX) space —
        // the LED is roughly linear in PWM duty cycle while the eye is
        // ~gamma 2.2. Without this conversion the slider barely looks
        // like it's doing anything across the top half of its travel.
        const float linear = std::pow(std::clamp(brightness, 0.0f, 1.0f), 2.2f);
        c.r = (uint8_t)std::clamp((int)std::round(c.r * linear), 0, 255);
        c.g = (uint8_t)std::clamp((int)std::round(c.g * linear), 0, 255);
        c.b = (uint8_t)std::clamp((int)std::round(c.b * linear), 0, 255);
        pat->setColor(step, seg, c);
    }
    repaint();
    if (step == proc.currentStep.load())
        proc.pushPreview();
}

void GridComponent::mouseDown(const MouseEvent& e) {
    auto* pat = proc.currentBank().current();
    if (!pat) return;

    const int lw = Theme::LABEL_W;
    const int hh = Theme::HEADER_H;
    const int steps = pat->numSteps;
    const int segs  = pat->numSegments;

    // Click in X-axis step header → select entire column (shift extends range)
    if (e.position.y < hh && e.position.x >= lw) {
        int s = (int)((e.position.x - lw) / cellW);
        if (s >= 0 && s < steps) {
            if (e.mods.isShiftDown() && selectedStep >= 0) {
                selStart = std::min(selectedStep, s);
                selEnd   = std::max(selectedStep, s);
            } else {
                selectedStep = s;
                selectedSeg  = -1;
                selStart = selEnd = s;
            }
            repaint();
        }
        return;
    }
    // Click in Y-axis segment label → select entire row
    if (e.position.x < lw && e.position.y >= hh) {
        int sg = (int)((e.position.y - hh) / cellH);
        if (sg >= 0 && sg < segs) {
            selectedStep = -1;
            selectedSeg  = sg;
            selStart = selEnd = -1;
            repaint();
        }
        return;
    }

    // Otherwise — normal cell paint
    auto [step, seg] = cellAt(e.position);
    if (step < 0) return;
    proc.snapshot();  // one snapshot per stroke
    selectedStep = step;
    selectedSeg  = seg;
    selStart = selEnd = -1;  // clear range when painting
    applyColor(step, seg);
}

void GridComponent::mouseDrag(const MouseEvent& e) {
    // Only paint when dragging inside the grid area — don't treat header
    // drags as a paint stroke.
    if (e.position.x < Theme::LABEL_W || e.position.y < Theme::HEADER_H) return;
    auto [step, seg] = cellAt(e.position);
    if (step < 0) return;
    applyColor(step, seg);
}

void GridComponent::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w) {
    auto [step, seg] = cellAt(e.position);
    if (step < 0) return;
    auto* pat = proc.currentBank().current();
    if (!pat) return;

    // Accumulate wheel delta and only emit a brightness step once the
    // accumulator crosses a small threshold. This unifies notched mouse
    // wheels (one big deltaY per notch) and trackpad smooth-scroll (many
    // tiny deltaYs per flick): both end up producing roughly the same
    // perceived rate of brightness change, and a single aggressive flick
    // can't blow a cell straight to black.
    scrollAccum_ += w.deltaY;
    constexpr float threshold = 0.08f;    // ≈ one mouse-wheel notch
    if (std::abs(scrollAccum_) < threshold) return;

    const float stepDelta = scrollAccum_;
    scrollAccum_ = 0.0f;

    auto c = pat->getColor(step, seg);
    // Black cells have no hue to scale — the erase tool made them black,
    // so use the paint tool to bring them back. We also avoid driving a
    // colored cell *to* black via scroll (see min-perceptual clamp below).
    if (c.isBlack()) return;

    float peak = (float)std::max({c.r, c.g, c.b});
    if (peak <= 0.0f) return;

    // Work in perceptual (gamma-corrected) space so each accumulated
    // wheel-step feels like a uniform brightness change. LEDs are ~linear
    // in PWM duty cycle but human vision is ~gamma 2.2 — a linear DMX
    // scroll barely looks dimmer across the top half of the range.
    constexpr float gamma = 2.2f;
    float perceptual = std::pow(peak / 255.0f, 1.0f / gamma);      // 0..1

    // Sign preserved from the old code: on Steve's setup, scroll-up
    // yields negative deltaY = brighter. A multiplier of ~1.2 over the
    // 0.08 threshold gives ~10% perceptual change per emitted step,
    // which is roughly 10 steps from full to the dim floor.
    perceptual -= stepDelta * 1.2f;

    // Floor the scroll-down at a perceptual level that still produces a
    // visible DMX value (≈DMX 15). This prevents scroll gestures from
    // trapping cells at pure black where isBlack() would early-return
    // and the user couldn't bring them back without repainting.
    perceptual = std::clamp(perceptual, 0.22f, 1.0f);

    const float newPeak = std::pow(perceptual, gamma) * 255.0f;
    const float scale   = newPeak / peak;

    c.r = (uint8_t)std::clamp((int)std::round(c.r * scale), 0, 255);
    c.g = (uint8_t)std::clamp((int)std::round(c.g * scale), 0, 255);
    c.b = (uint8_t)std::clamp((int)std::round(c.b * scale), 0, 255);
    pat->setColor(step, seg, c);
    repaint();

    // If we just edited the currently-playing step, push a live preview
    // so the hardware updates in real time (matches applyColor()).
    if (step == proc.currentStep.load())
        proc.pushPreview();
}

// ############################################################################
//  BarPreviewComponent
// ############################################################################
void BarPreviewComponent::paint(Graphics& g) {
    g.fillAll(Theme::BG_SECONDARY);
    g.setColour(Theme::BORDER);
    g.drawRect(getLocalBounds(), 1);

    auto* pat = proc.currentBank().current();
    if (!pat) return;

    int segs = pat->numSegments;
    int step = proc.currentStep.load();
    auto colors = pat->getStepColors(step);

    // Live global modifiers — match grid + DMX output
    const float  hueDeg   = proc.hueShiftDeg.load();
    const bool   flood    = proc.floodActive.load();
    const auto   floodCol = unpackFloodColor(proc.floodColor.load());
    const bool   blackout = proc.blackoutActive.load();
    const float  master   = proc.masterDimmer.load();

    float pad   = 6.0f;
    float segW  = (getWidth() - pad * 2.0f) / segs;
    float y1    = pad, y2 = getHeight() - pad;

    for (int i = 0; i < segs && i < (int)colors.size(); i++) {
        float x1 = pad + i * segW + 1.0f;
        float x2 = x1 + segW - 2.0f;
        auto c = colors[i];

        if (blackout)            c = {0, 0, 0};
        else if (flood)          c = floodCol;
        else if (hueDeg != 0.0f) c = rotateHue(c, hueDeg);

        // Apply master dimmer so the bar also dims with the slider
        c.r = (uint8_t)std::clamp((int)std::round(c.r * master), 0, 255);
        c.g = (uint8_t)std::clamp((int)std::round(c.g * master), 0, 255);
        c.b = (uint8_t)std::clamp((int)std::round(c.b * master), 0, 255);

        int brightness = c.r + c.g + c.b;
        if (brightness > 30) {
            auto glow = Colour(c.r, c.g, c.b).brighter(0.3f).withAlpha(0.25f);
            g.setColour(glow);
            g.fillRect(x1 - 2, y1 - 2, x2 - x1 + 4, y2 - y1 + 4);
        }

        g.setColour(Colour(c.r, c.g, c.b));
        g.fillRect(x1, y1, x2 - x1, y2 - y1);

        g.setColour(Theme::BORDER);
        g.drawRect(x1, y1, x2 - x1, y2 - y1, 1.0f);

        g.setColour(brightness > 380 ? Theme::BG_PRIMARY : Theme::FG_DIM);
        g.setFont(10.0f);
        g.drawText(String(i + 1), (int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1),
                   Justification::centred);
    }
}

// ############################################################################
//  SongTimelineComponent
// ############################################################################
static constexpr int kBlockW = 100;
static constexpr int kBlockH = 50;

int SongTimelineComponent::blockAt(float x) const {
    int idx = (int)(x / kBlockW);
    if (idx >= 0 && idx < (int)proc.song.blocks.size())
        return idx;
    return -1;
}

void SongTimelineComponent::paint(Graphics& g) {
    g.fillAll(Theme::BG_SECONDARY);

    auto& song = proc.song;
    // Content size is driven by the editor (see updateSongTimelineSize).
    // We deliberately do NOT call setSize from paint - calling it here
    // can recurse into more paints and made the layout fragile when the
    // timeline was placed inside a Viewport.

    for (int i = 0; i < (int)song.blocks.size(); i++) {
        auto& blk = song.blocks[i];
        int x = i * kBlockW + 4;
        int y = 4;
        int w = kBlockW - 8;
        int h = kBlockH - 8;

        bool sel   = (i == selectedBlock);
        bool isCur = proc.songModeActive && (i == proc.songPlayer.currentBlock);

        g.setColour(isCur ? Theme::ACCENT.darker(0.5f)
                          : (sel ? Theme::BG_TERTIARY.brighter(0.15f) : Theme::BG_TERTIARY));
        g.fillRoundedRectangle((float)x, (float)y, (float)w, (float)h, 4.0f);

        g.setColour(sel ? Theme::ACCENT : Theme::BORDER);
        g.drawRoundedRectangle((float)x, (float)y, (float)w, (float)h, 4.0f, sel ? 2.0f : 1.0f);

        auto& bank = proc.fixtures[proc.activeFixture].patternBank;
        if (blk.patternIndex >= 0 && blk.patternIndex < (int)bank.patterns.size()) {
            auto& pat = bank.patterns[blk.patternIndex];
            int stripH = 6;
            int segs = pat.numSegments;
            float segW = (float)w / segs;
            for (int seg = 0; seg < segs; seg++) {
                auto c = pat.getColor(0, seg);
                g.setColour(Colour(c.r, c.g, c.b));
                g.fillRect(x + (int)(seg * segW), y, (int)segW + 1, stripH);
            }
        }

        g.setColour(Theme::FG_COLOR);
        g.setFont(11.0f);
        String label = "P" + String(blk.patternIndex + 1);
        g.drawText(label, x, y + 8, w, 18, Justification::centred);

        g.setColour(Theme::FG_DIM);
        g.setFont(10.0f);
        g.drawText(String(juce::CharPointer_UTF8("\xc3\x97")) + String(blk.repeats),
                   x, y + 24, w, 16, Justification::centred);
    }
}

void SongTimelineComponent::mouseDown(const MouseEvent& e) {
    selectedBlock = blockAt(e.position.x);
    repaint();
}

// ############################################################################
//  DMXControllerEditor
// ############################################################################

DMXControllerEditor::DMXControllerEditor(DMXControllerProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      barPreview(p), grid(p), songTimeline(p)
{
    setLookAndFeel(&darkLnf);
    setSize(1180, 880);
    setResizable(true, true);
    setResizeLimits(1000, 720, 2200, 1400);

    // ========== Row 1: Transport ==========
    addAndMakeVisible(playBtn);
    addAndMakeVisible(stopBtn);
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(autoResetLabel);
    addAndMakeVisible(autoResetSelector);
    addAndMakeVisible(blackoutBtn);
    addAndMakeVisible(tapBtn);
    addAndMakeVisible(panicBtn);
    addAndMakeVisible(undoBtn);
    addAndMakeVisible(redoBtn);

    autoResetLabel.setJustificationType(Justification::centredRight);
    autoResetSelector.addItem("Off",       1);
    autoResetSelector.addItem("1st Step",  2);
    autoResetSelector.addItem("Last Step", 3);
    autoResetSelector.setTooltip(
        "Auto-reset on stop:\n"
        "  Off       - leave playback where it is\n"
        "  1st Step  - rewind to step 1\n"
        "  Last Step - park at the last step; the first MIDI clock tick\n"
        "              after Start wraps cleanly back to step 1 (recommended\n"
        "              if the first step is being skipped when MIDI clock\n"
        "              is received).");
    autoResetSelector.setSelectedId(proc.autoResetMode.load() + 1, dontSendNotification);
    autoResetSelector.onChange = [this] {
        proc.autoResetMode.store(autoResetSelector.getSelectedId() - 1);
    };

    playBtn.setColour(TextButton::buttonColourId,  Theme::GREEN_ACCENT.darker(0.3f));
    stopBtn.setColour(TextButton::buttonColourId,  Theme::BG_TERTIARY);
    resetBtn.setColour(TextButton::buttonColourId, Theme::BG_TERTIARY);
    tapBtn.setColour(TextButton::buttonColourId,   Theme::BG_TERTIARY);
    undoBtn.setColour(TextButton::buttonColourId,  Theme::BG_TERTIARY);
    redoBtn.setColour(TextButton::buttonColourId,  Theme::BG_TERTIARY);
    panicBtn.setColour(TextButton::buttonColourId, Theme::RED_ACCENT.darker(0.6f));
    panicBtn.setColour(TextButton::textColourOffId, Theme::RED_ACCENT);

    playBtn.onClick  = [this] {
        if (proc.isPlaying.load()) { proc.stopPlayback(); playBtn.setButtonText("PLAY"); }
        else { proc.startPlayback(); playBtn.setButtonText("PAUSE"); }
    };
    stopBtn.onClick  = [this] { proc.stopPlayback(); playBtn.setButtonText("PLAY"); };
    resetBtn.onClick = [this] { proc.resetPlayback(); };
    blackoutBtn.onClick = [this] {
        proc.blackoutActive.store(blackoutBtn.getToggleState());
        proc.pushPreview();
    };
    tapBtn.onClick   = [this] { proc.tapTempo(); bpmSlider.setValue(proc.bpm, dontSendNotification); };
    panicBtn.onClick = [this] { proc.panicBlackout(); blackoutBtn.setToggleState(true, dontSendNotification); };
    undoBtn.onClick  = [this] { if (proc.undo()) { grid.repaint(); barPreview.repaint(); } };
    redoBtn.onClick  = [this] { if (proc.redo()) { grid.repaint(); barPreview.repaint(); } };

    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(bpmLabel);
    bpmSlider.setRange(20.0, 300.0, 0.1);
    bpmSlider.setValue(proc.bpm);
    bpmSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 50, 20);
    bpmSlider.setSliderStyle(Slider::LinearHorizontal);
    bpmSlider.onValueChange = [this] {
        // Don't let the host-tempo mirror in MIDI Clock mode clobber
        // proc.bpm — that value is the "internal" tempo and should
        // survive a round-trip through MIDI Clock mode unchanged.
        if (proc.clockSource.load() != 1)
            proc.bpm = bpmSlider.getValue();
    };
    bpmLabel.setText("BPM", dontSendNotification);

    addAndMakeVisible(clockSrcLabel);
    addAndMakeVisible(clockSrcSelector);
    clockSrcSelector.addItem("Internal",   1);
    clockSrcSelector.addItem("MIDI Clock", 2);
    // Clamp any legacy saved state of "Sync DAW" (=2) back to Internal.
    if (proc.clockSource.load() >= 2) proc.clockSource.store(0);
    clockSrcSelector.setSelectedId(proc.clockSource.load() + 1, dontSendNotification);

    // Apply initial BPM-slider state based on current clock source. This
    // mirrors what the clockSrcSelector.onChange lambda does and guarantees
    // the slider is in a known-good state on editor open regardless of the
    // saved clockSource value. This is the ONLY place other than the
    // clockSrcSelector.onChange that touches bpmSlider's enabled/alpha —
    // the 30 Hz timer must never touch it or it will fight with the user's
    // mouse drags.
    {
        const bool midiClock = proc.clockSource.load() == 1;
        bpmSlider.setEnabled(true);
        bpmSlider.setAlpha(midiClock ? 0.4f : 1.0f);
        bpmSlider.setInterceptsMouseClicks(true, true);
        tapBtn.setEnabled(!midiClock);
        tapBtn.setAlpha(midiClock ? 0.4f : 1.0f);
    }

    clockSrcSelector.onChange = [this] {
        int v = clockSrcSelector.getSelectedId() - 1;
        proc.stopPlayback();
        proc.clockSource.store(v);
        playBtn.setButtonText("PLAY");

        // Apply BPM-slider visual state immediately on change. We do this
        // here (on the message thread, in response to a user action) rather
        // than in the timer so we never interrupt a live mouse drag.
        const bool midiClock = v == 1;
        bpmSlider.setEnabled(true);       // always enabled: setEnabled(false)
                                          // suppresses setValue() repaints
                                          // which would break the MIDI Clock
                                          // host-tempo mirror.
        bpmSlider.setAlpha(midiClock ? 0.4f : 1.0f);
        bpmSlider.setInterceptsMouseClicks(true, true);
        tapBtn.setEnabled(!midiClock);
        tapBtn.setAlpha(midiClock ? 0.4f : 1.0f);
    };

    // ========== Row 2: MIDI In/Out ==========
    addAndMakeVisible(midiInLabel);
    addAndMakeVisible(midiInSelector);
    addAndMakeVisible(midiOutLabel);
    addAndMakeVisible(midiOutSelector);
    addAndMakeVisible(midiOutRefreshBtn);

    midiInSelector.onChange = [this] {
        int id = midiInSelector.getSelectedId();
        if (id == 1)
            proc.setMidiInputDevice({});
        else if (id > 1 && id - 2 < cachedMidiInDevices_.size())
            proc.setMidiInputDevice(cachedMidiInDevices_[id - 2]);
    };
    midiOutSelector.onChange = [this] {
        int id = midiOutSelector.getSelectedId();
        if (id == 1)
            proc.setMidiOutputDevice({});
        else if (id > 1 && id - 2 < cachedMidiOutDevices_.size())
            proc.setMidiOutputDevice(cachedMidiOutDevices_[id - 2]);
    };
    midiOutRefreshBtn.onClick = [this] { refreshMidiDeviceList(); };

    addAndMakeVisible(fixtureLabel);
    addAndMakeVisible(fixtureSelector);
    addAndMakeVisible(addFixBtn);
    addAndMakeVisible(delFixBtn);
    addAndMakeVisible(dupFixBtn);
    addAndMakeVisible(renameFixBtn);
    addAndMakeVisible(exportFixBtn);
    addAndMakeVisible(importFixBtn);
    addAndMakeVisible(segsLabel_);
    addAndMakeVisible(segsSlider);
    addAndMakeVisible(dmxStartLabel);
    addAndMakeVisible(dmxStartSlider);
    addAndMakeVisible(profileSelector);
    addAndMakeVisible(fixBrightLabel);
    addAndMakeVisible(fixBrightSlider);

    fixBrightSlider.setRange(0.0, 2.0, 0.01);
    fixBrightSlider.setValue(1.0);
    fixBrightSlider.setSliderStyle(Slider::LinearHorizontal);
    fixBrightSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 40, 22);
    fixBrightSlider.setDoubleClickReturnValue(true, 1.0);
    fixBrightSlider.setTooltip("Per-fixture brightness trim. 1.00 = unchanged, "
                               "below 1.00 dims only this fixture, above 1.00 boosts it. "
                               "Useful when one fixture is brighter/dimmer than others in the rig.");
    fixBrightLabel.setTooltip(fixBrightSlider.getTooltip());
    fixBrightSlider.onValueChange = [this] {
        proc.fixtures[proc.activeFixture].brightnessOffset = (float)fixBrightSlider.getValue();
    };

    dupFixBtn.onClick = [this] {
        proc.duplicateFixture(proc.activeFixture);
        proc.activeFixture = (int)proc.fixtures.size() - 1;
        refreshFixtureSelector();
        applyFixtureEdit();
        refreshAll();
    };
    exportFixBtn.onClick = [this] {
        auto json = proc.serializeFixture(proc.activeFixture);
        auto chooser = std::make_shared<FileChooser>("Export Fixture",
                File::getSpecialLocation(File::userDocumentsDirectory),
                "*.json");
        chooser->launchAsync(FileBrowserComponent::saveMode
                           | FileBrowserComponent::canSelectFiles
                           | FileBrowserComponent::warnAboutOverwriting,
            [chooser, json](const FileChooser& fc) {
                auto f = fc.getResult();
                if (f != File{}) {
                    if (!f.getFileName().containsIgnoreCase(".json"))
                        f = f.withFileExtension(".json");
                    f.replaceWithText(json);
                }
            });
    };
    importFixBtn.onClick = [this] {
        auto chooser = std::make_shared<FileChooser>("Import Fixture",
                File::getSpecialLocation(File::userDocumentsDirectory),
                "*.json");
        chooser->launchAsync(FileBrowserComponent::openMode
                           | FileBrowserComponent::canSelectFiles,
            [this, chooser](const FileChooser& fc) {
                auto f = fc.getResult();
                if (f != File{} && f.existsAsFile()) {
                    auto txt = f.loadFileAsString();
                    if (proc.deserializeFixtureInto(proc.activeFixture, txt)) {
                        refreshFixtureSelector();
                        applyFixtureEdit();
                        refreshAll();
                    }
                }
            });
    };

    fixtureSelector.onChange = [this] {
        int idx = fixtureSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < (int)proc.fixtures.size()) {
            proc.activeFixture = idx;
            applyFixtureEdit();
            refreshAll();
        }
    };
    addFixBtn.onClick = [this] {
        proc.addFixture();
        proc.activeFixture = (int)proc.fixtures.size() - 1;
        refreshFixtureSelector();
        applyFixtureEdit();
        refreshAll();
    };
    delFixBtn.onClick = [this] {
        proc.removeFixture(proc.activeFixture);
        refreshFixtureSelector();
        applyFixtureEdit();
        refreshAll();
    };
    renameFixBtn.onClick = [this] {
        auto* aw = new AlertWindow("Rename Fixture",
                                   "Enter a new name:",
                                   AlertWindow::NoIcon);
        aw->addTextEditor("name",
            String(proc.fixtures[proc.activeFixture].name), {});
        aw->addButton("OK",     1, KeyPress(KeyPress::returnKey));
        aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
        aw->enterModalState(true,
            ModalCallbackFunction::create([this, aw](int result) {
                if (result == 1) {
                    auto name = aw->getTextEditorContents("name").trim();
                    if (name.isNotEmpty()) {
                        proc.fixtures[proc.activeFixture].name = name.toStdString();
                        refreshFixtureSelector();
                    }
                }
            }), true /* deleteWhenDismissed */);
    };

    segsSlider.setRange(1, 16, 1);
    segsSlider.setSliderStyle(Slider::IncDecButtons);
    segsSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 32, 22);
    segsSlider.onValueChange = [this] {
        int n = (int)segsSlider.getValue();
        {
            // Take the data lock before touching any pattern — the
            // HighResolutionTimer and audio thread read these fields
            // concurrently and reassigning a Pattern (destroying/rebuilding
            // its grid_ vector) while a reader is mid-access crashes.
            const juce::ScopedLock l(proc.dataLock);
            auto& fix = proc.fixtures[proc.activeFixture];
            if (n == fix.numSegments) return;
            fix.numSegments = n;
            // Resize all patterns in this fixture's bank
            for (auto& pat : fix.patternBank.patterns) {
                // Rebuild grid preserving existing contents
                auto copy = pat;
                pat = Pattern(copy.name, copy.numSteps, n, copy.subdiv);
                for (int s = 0; s < copy.numSteps; s++)
                    for (int seg = 0; seg < std::min(n, copy.numSegments); seg++)
                        pat.setColor(s, seg, copy.getColor(s, seg));
            }
        }
        refreshAll();
    };

    // 1-based in the UI (matches how DMX addresses are talked about on the
    // fixture hardware and every DMX manual), 0-based in storage.
    dmxStartSlider.setRange(1, 121, 1);
    dmxStartSlider.setSliderStyle(Slider::IncDecButtons);
    dmxStartSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 40, 22);
    dmxStartSlider.onValueChange = [this] {
        const juce::ScopedLock l(proc.dataLock);
        proc.fixtures[proc.activeFixture].dmxStart = (int)dmxStartSlider.getValue() - 1;
    };

    refreshProfileSelector();

    addAndMakeVisible(newProfileBtn);
    addAndMakeVisible(delProfileBtn);
    // Short labels so the two buttons fit next to the profile selector on
    // every window width. Hover tooltips explain what they do.
    newProfileBtn.setButtonText("+");
    delProfileBtn.setButtonText("-");
    newProfileBtn.setTooltip("Create a new custom fixture profile "
                             "(persisted across sessions).");
    delProfileBtn.setTooltip("Delete the currently selected user profile. "
                             "Built-in profiles cannot be deleted.");
    newProfileBtn.onClick = [this] { showNewProfileDialog(); };
    delProfileBtn.onClick = [this] { confirmAndDeleteCurrentUserProfile(); };

    profileSelector.onChange = [this] {
        bool rebuilt = false;
        int  lockedSegs = 0;
        {
            const juce::ScopedLock l(proc.dataLock);
            auto& fix = proc.fixtures[proc.activeFixture];
            fix.profileIndex = profileSelector.getSelectedId() - 1;

            // Some profiles lock the segment count (e.g. single-segment Par Can).
            const auto& prof = fix.profile();
            lockedSegs = prof.fixedSegments;
            if (prof.fixedSegments > 0 && fix.numSegments != prof.fixedSegments) {
                fix.numSegments = prof.fixedSegments;
                // Rebuild every pattern to the new segment count
                for (auto& pat : fix.patternBank.patterns) {
                    auto copy = pat;
                    pat = Pattern(copy.name, copy.numSteps, prof.fixedSegments, copy.subdiv);
                    for (int s = 0; s < copy.numSteps; s++)
                        for (int seg = 0; seg < std::min(prof.fixedSegments, copy.numSegments); seg++)
                            pat.setColor(s, seg, copy.getColor(s, seg));
                }
                rebuilt = true;
            }
        }

        if (rebuilt) {
            applyFixtureEdit();   // re-sync segsSlider
            refreshAll();
        }

        // Disable the segs slider while the profile dictates segments.
        segsSlider.setEnabled(lockedSegs == 0);
    };

    // ========== Row 3: Bar Preview ==========
    addAndMakeVisible(barPreview);

    // ========== Grid inside viewport ==========
    gridViewport.setViewedComponent(&grid, false);
    gridViewport.setScrollBarsShown(true, true);
    addAndMakeVisible(gridViewport);
    grid.recalcSize();

    // ========== Row 4: Colour palette ==========
    for (int i = 0; i < kNumColorBtns; i++) {
        addAndMakeVisible(colorBtns[i]);
        auto c = PRESET_COLORS[i];
        colorBtns[i].setColour(TextButton::buttonColourId, Colour(c.r, c.g, c.b));
        colorBtns[i].setColour(TextButton::textColourOffId,
            (c.r + c.g + c.b > 380) ? Colours::black : Colours::white);
        colorBtns[i].onClick = [this, i] { selectColor(i); };
    }

    addAndMakeVisible(brightnessSlider);
    addAndMakeVisible(brightnessLabel);
    brightnessSlider.setRange(0.0, 1.0, 0.01);
    brightnessSlider.setValue(1.0);
    brightnessSlider.setSliderStyle(Slider::LinearHorizontal);
    brightnessSlider.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
    brightnessSlider.onValueChange = [this] { grid.brightness = (float)brightnessSlider.getValue(); };

    addAndMakeVisible(fillBtn);
    addAndMakeVisible(clearBtn);
    addAndMakeVisible(eraseBtn);
    addAndMakeVisible(genBtn);

    auto showFillOrClearMenu = [this] (bool clearing) {
        PopupMenu m;
        bool haveStep = grid.selectedStep >= 0;
        bool haveSeg  = grid.selectedSeg  >= 0;
        if (clearing) {
            m.addItem(1, "Clear All");
            m.addItem(2, "Clear Step (column)",  haveStep);
            m.addItem(3, "Clear Segment (row)",  haveSeg);
        } else {
            m.addItem(1, "Fill All");
            m.addItem(2, "Fill Step (column)",   haveStep);
            m.addItem(3, "Fill Segment (row)",   haveSeg);
        }

        m.showMenuAsync(PopupMenu::Options().withTargetComponent(clearing ? clearBtn : fillBtn),
            [this, clearing](int result) {
                auto* pat = proc.currentBank().current();
                if (!pat || result == 0) return;

                RGBColor paintC;
                if (clearing) {
                    paintC = {0, 0, 0};
                } else {
                    RGBColor c = grid.activeColor;
                    float b = grid.brightness;
                    c.r = (uint8_t)(c.r * b); c.g = (uint8_t)(c.g * b); c.b = (uint8_t)(c.b * b);
                    paintC = c;
                }

                switch (result) {
                    case 1: pat->fillAll(paintC);                       break;
                    case 2: pat->fillStep(grid.selectedStep, paintC);   break;
                    case 3: pat->fillSegment(grid.selectedSeg, paintC); break;
                }
                grid.repaint();
                barPreview.repaint();
            });
    };
    fillBtn .onClick = [this, showFillOrClearMenu] { proc.snapshot(); showFillOrClearMenu(false); };
    clearBtn.onClick = [this, showFillOrClearMenu] { proc.snapshot(); showFillOrClearMenu(true);  };
    eraseBtn.setClickingTogglesState(true);
    eraseBtn.onClick = [this] { grid.eraseMode = eraseBtn.getToggleState(); };
    genBtn.onClick = [this] { showGeneratorMenu(); };

    // ========== Row 5: Tools + Live ==========
    addAndMakeVisible(copyBtn);
    addAndMakeVisible(pasteBtn);
    addAndMakeVisible(mirrorBtn);
    addAndMakeVisible(shiftLBtn);
    addAndMakeVisible(shiftRBtn);
    addAndMakeVisible(randBtn);

    copyBtn.onClick = [this] {
        auto* pat = proc.currentBank().current();
        if (!pat) return;
        if (grid.selStart >= 0 && grid.selEnd >= 0) {
            rangeClipboard_ = pat->copyRange(grid.selStart, grid.selEnd);
            stepClipboard_.clear();
        } else if (grid.selectedStep >= 0) {
            stepClipboard_ = pat->copyStep(grid.selectedStep);
            rangeClipboard_.clear();
        }
    };
    pasteBtn.onClick = [this] {
        auto* pat = proc.currentBank().current();
        if (!pat) return;
        proc.snapshot();
        int target = (grid.selStart >= 0) ? grid.selStart : grid.selectedStep;
        if (target < 0) return;
        if (!rangeClipboard_.empty())
            pat->pasteRange(target, rangeClipboard_);
        else if (!stepClipboard_.empty())
            pat->pasteStep(target, stepClipboard_);
        grid.repaint();
    };
    mirrorBtn.onClick = [this] {
        if (auto* pat = proc.currentBank().current()) { proc.snapshot(); pat->mirror(); grid.repaint(); }
    };
    shiftLBtn.onClick = [this] {
        if (auto* pat = proc.currentBank().current()) { proc.snapshot(); pat->shiftLeft(); grid.repaint(); }
    };
    shiftRBtn.onClick = [this] {
        if (auto* pat = proc.currentBank().current()) { proc.snapshot(); pat->shiftRight(); grid.repaint(); }
    };
    randBtn.onClick = [this] {
        if (auto* pat = proc.currentBank().current()) {
            proc.snapshot();
            pat->randomize(PRESET_COLORS, NUM_PRESET_COLORS - 1);
            grid.repaint();
        }
    };

    addAndMakeVisible(midiLearnBtn);
    addAndMakeVisible(fadeSelector);
    addAndMakeVisible(fadeLabel);
    midiLearnBtn.setClickingTogglesState(true);
    midiLearnBtn.onClick = [this] {
        if (midiLearnBtn.getToggleState())
            proc.beginMidiLearn(DMXControllerProcessor::MidiTarget::PatternSelect,
                                proc.currentBank().currentIndex);
        else
            proc.midiLearnActive.store(false);
    };
    fadeSelector.addItem("0",  1);
    fadeSelector.addItem("2",  2);
    fadeSelector.addItem("4",  3);
    fadeSelector.addItem("8",  4);
    fadeSelector.addItem("16", 5);
    fadeSelector.setSelectedId(1);
    fadeSelector.onChange = [this] {
        int steps[] = {0, 2, 4, 8, 16};
        proc.crossfadeSteps = steps[fadeSelector.getSelectedId() - 1];
    };

    addAndMakeVisible(zoomLabel);
    addAndMakeVisible(zoomSelector);
    zoomSelector.addItem("Fit",  1);
    zoomSelector.addItem("50%",  2);
    zoomSelector.addItem("75%",  3);
    zoomSelector.addItem("100%", 4);
    zoomSelector.addItem("125%", 5);
    zoomSelector.addItem("150%", 6);
    zoomSelector.addItem("200%", 7);
    zoomSelector.addItem("300%", 8);
    zoomSelector.setSelectedId(1, dontSendNotification);
    zoomSelector.onChange = [this] {
        int id = zoomSelector.getSelectedId();
        float zooms[] = {1.0f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f};
        grid.zoomFit = (id == 1);
        if (!grid.zoomFit) grid.zoom = zooms[id - 1];
        grid.recalcSize();
        grid.repaint();
    };

    // ========== Live Controls Row ==========
    addAndMakeVisible(masterDimLabel);
    addAndMakeVisible(masterDimSlider);
    addAndMakeVisible(hueLabel);
    addAndMakeVisible(hueSlider);
    addAndMakeVisible(hueResetBtn);
    addAndMakeVisible(swingLabel);
    addAndMakeVisible(swingSlider);

    hueResetBtn.setTooltip("Reset hue shift to zero");
    hueResetBtn.onClick = [this] {
        hueSlider.setValue(0.0, sendNotificationSync);
        proc.hueShiftDeg.store(0.0f);
        proc.pushPreview();
        grid.repaint();
        barPreview.repaint();
    };

    auto cfgLiveSlider = [](Slider& s, double lo, double hi, double val,
                            const String& suffix, int decimals)
    {
        s.setRange(lo, hi, std::pow(10.0, -decimals));
        s.setValue(val);
        s.setSliderStyle(Slider::LinearHorizontal);
        s.setTextBoxStyle(Slider::TextBoxRight, false, 46, 20);
        s.setNumDecimalPlacesToDisplay(decimals);
        s.setTextValueSuffix(suffix);
    };
    cfgLiveSlider(masterDimSlider, 0.0,     1.0,   proc.masterDimmer.load(), "",    2);
    cfgLiveSlider(hueSlider,      -180.0, 180.0,   proc.hueShiftDeg.load(),
                  juce::String::charToString((juce_wchar)0x00B0), 0);
    hueSlider.setDoubleClickReturnValue(true, 0.0);
    masterDimSlider.setDoubleClickReturnValue(true, 1.0);
    // Swing: DAW-style 50..75% where 50% = straight (no swing), 75% = triplet feel
    swingSlider.setRange(50.0, 75.0, 0.5);
    swingSlider.setValue(50.0 + proc.swing.load() * 50.0);  // 0 → 50, 0.5 → 75
    swingSlider.setSliderStyle(Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(Slider::TextBoxRight, false, 50, 20);
    swingSlider.setTextValueSuffix(" %");
    swingSlider.setNumDecimalPlacesToDisplay(1);
    swingSlider.setDoubleClickReturnValue(true, 50.0);

    // Scenes section label
    addAndMakeVisible(scenesLabel);
    scenesLabel  .setJustificationType(Justification::centredRight);

    masterDimSlider.onValueChange = [this] { proc.masterDimmer.store((float)masterDimSlider.getValue()); proc.pushPreview(); };
    hueSlider      .onValueChange = [this] { proc.hueShiftDeg.store ((float)hueSlider.getValue());       proc.pushPreview(); };
    // Swing: UI 50..75%, internal 0..0.5  (50 → 0, 75 → 0.5)
    swingSlider    .onValueChange = [this] {
        double pct = swingSlider.getValue();
        proc.swing.store((float)((pct - 50.0) * 0.02));
    };

    // FLOOD toggle — when enabled, the next colour button click becomes a
    // temporary flood override instead of selecting the paint colour.
    addAndMakeVisible(floodToggleBtn);
    floodToggleBtn.setClickingTogglesState(true);
    floodToggleBtn.setTooltip("FLOOD mode. When ON, clicking a colour button "
                              "floods the whole rig with that colour until "
                              "you click the same colour again or turn FLOOD off.");
    floodToggleBtn.setColour(TextButton::buttonOnColourId, Colour(0xffc05000));
    floodToggleBtn.setToggleState(proc.floodMode.load(), dontSendNotification);
    floodToggleBtn.onClick = [this] {
        const bool on = floodToggleBtn.getToggleState();
        proc.floodMode.store(on);
        if (!on) {
            // Turning flood mode off clears any active flood
            proc.floodActive.store(false);
            proc.floodColor.store(0);
            proc.pushPreview();
            grid.repaint();
            barPreview.repaint();
        }
    };

    static const char* sceneLetters[4] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; i++) {
        sceneBtns[i].setButtonText(sceneLetters[i]);
        addAndMakeVisible(sceneBtns[i]);
        sceneBtns[i].onClick = [this, i] {
            if (proc.loadScene(i)) {
                refreshFixtureSelector();
                applyFixtureEdit();
                refreshAll();
            }
        };

        sceneStoreBtns[i].setButtonText("S");
        sceneStoreBtns[i].setTooltip("Store scene " + String(sceneLetters[i]));
        addAndMakeVisible(sceneStoreBtns[i]);
        sceneStoreBtns[i].onClick = [this, i] {
            proc.storeScene(i);
            sceneBtns[i].setColour(TextButton::buttonColourId, Theme::ACCENT.darker(0.4f));
        };
    }

    // ========== Bottom row: Pattern + Song ==========
    addAndMakeVisible(patternSelector);
    addAndMakeVisible(newPatBtn);
    addAndMakeVisible(dupPatBtn);
    addAndMakeVisible(delPatBtn);
    addAndMakeVisible(patMoreBtn);
    addAndMakeVisible(stepsMinBtn);
    addAndMakeVisible(stepsPlusBtn);
    addAndMakeVisible(stepsLabel);
    addAndMakeVisible(subdivSelector);

    patMoreBtn.setTooltip("More: Rename, Export, Import pattern");
    patMoreBtn.onClick = [this] {
        PopupMenu m;
        m.addItem(1, "Rename Pattern...");
        m.addSeparator();
        m.addItem(2, "Export Pattern to JSON...");
        m.addItem(3, "Import Pattern from JSON...");
        m.showMenuAsync(PopupMenu::Options().withTargetComponent(&patMoreBtn),
            [this](int r) {
                if (r == 1) {
                    auto* pat = proc.currentBank().current();
                    if (!pat) return;
                    auto* aw = new AlertWindow("Rename Pattern", "Enter a new name:", AlertWindow::NoIcon);
                    aw->addTextEditor("name", String(pat->name), {});
                    aw->addButton("OK",     1, KeyPress(KeyPress::returnKey));
                    aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
                    aw->enterModalState(true, ModalCallbackFunction::create([this, aw](int result) {
                        if (result == 1) {
                            auto name = aw->getTextEditorContents("name").trim();
                            if (name.isNotEmpty())
                                if (auto* p = proc.currentBank().current()) {
                                    p->name = name.toStdString();
                                    refreshPatternSelector();
                                }
                        }
                    }), true);
                }
                else if (r == 2) {
                    auto* fc = new FileChooser("Export Pattern", File(), "*.json");
                    fc->launchAsync(FileBrowserComponent::saveMode
                                    | FileBrowserComponent::canSelectFiles
                                    | FileBrowserComponent::warnAboutOverwriting,
                        [this, fc](const FileChooser& c) {
                            std::unique_ptr<FileChooser> keep(fc);
                            auto f = c.getResult();
                            if (f == File()) return;
                            if (!f.hasFileExtension("json")) f = f.withFileExtension("json");
                            auto json = proc.serializePattern(proc.activeFixture,
                                                              proc.currentBank().currentIndex);
                            f.replaceWithText(json);
                        });
                }
                else if (r == 3) {
                    auto* fc = new FileChooser("Import Pattern", File(), "*.json");
                    fc->launchAsync(FileBrowserComponent::openMode
                                    | FileBrowserComponent::canSelectFiles,
                        [this, fc](const FileChooser& c) {
                            std::unique_ptr<FileChooser> keep(fc);
                            auto f = c.getResult();
                            if (f == File()) return;
                            auto txt = f.loadFileAsString();
                            if (proc.deserializePatternInto(proc.activeFixture,
                                                            proc.currentBank().currentIndex, txt)) {
                                refreshPatternSelector();
                                grid.recalcSize();
                                grid.repaint();
                            } else {
                                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                    "Import failed", "Could not parse pattern JSON.");
                            }
                        });
                }
            });
    };

    patternSelector.onChange = [this] {
        int idx = patternSelector.getSelectedId() - 1;
        if (idx >= 0) {
            proc.currentBank().select(idx);
            grid.recalcSize();
            grid.repaint();
            refreshStepsLabel();
        }
    };
    newPatBtn.onClick = [this] {
        int segs = proc.fixtures[proc.activeFixture].numSegments;
        int idx = proc.currentBank().addPattern(segs);
        proc.currentBank().select(idx);
        refreshPatternSelector();
        grid.recalcSize(); grid.repaint();
    };
    dupPatBtn.onClick = [this] {
        int idx = proc.currentBank().duplicatePattern();
        if (idx >= 0) proc.currentBank().select(idx);
        refreshPatternSelector();
        grid.recalcSize(); grid.repaint();
    };
    delPatBtn.onClick = [this] {
        proc.currentBank().deletePattern(proc.currentBank().currentIndex);
        refreshPatternSelector();
        grid.recalcSize(); grid.repaint();
    };
    stepsMinBtn.onClick  = [this] {
        if (auto* pp = proc.currentBank().current()) {
            pp->setSteps(pp->numSteps - 1); grid.recalcSize(); grid.repaint(); refreshStepsLabel();
        }
    };
    stepsPlusBtn.onClick = [this] {
        if (auto* pp = proc.currentBank().current()) {
            pp->setSteps(pp->numSteps + 1); grid.recalcSize(); grid.repaint(); refreshStepsLabel();
        }
    };

    subdivSelector.addItem("1/4",   1);
    subdivSelector.addItem("1/8",   2);
    subdivSelector.addItem("1/8T",  3);
    subdivSelector.addItem("1/16",  4);
    subdivSelector.addItem("1/16T", 5);
    subdivSelector.addItem("1/32",  6);
    subdivSelector.addItem("1/32T", 7);
    subdivSelector.setSelectedId(4);   // default 1/16
    subdivSelector.onChange = [this] {
        // Steps per beat:
        // 1/4=1, 1/8=2, 1/8T=3, 1/16=4, 1/16T=6, 1/32=8, 1/32T=12
        int divs[] = {1, 2, 3, 4, 6, 8, 12};
        if (auto* pp = proc.currentBank().current()) {
            pp->subdiv = divs[subdivSelector.getSelectedId() - 1];
            grid.recalcSize();
            grid.repaint();
        }
    };

    // ========== Song ==========
    addAndMakeVisible(songModeBtn);
    addAndMakeVisible(addBlockBtn);
    addAndMakeVisible(remBlockBtn);
    addAndMakeVisible(dupBlockBtn);
    addAndMakeVisible(repPlusBtn);
    addAndMakeVisible(repMinusBtn);
    // Song timeline lives inside a viewport so it can scroll horizontally
    // and we can auto-follow the currently playing block during song
    // playback. The timeline component sizes itself (see
    // updateSongTimelineSize()) to fit N blocks; the viewport handles
    // the scrolling.
    songViewport.setViewedComponent(&songTimeline, false);
    songViewport.setScrollBarsShown(false, true);  // horizontal only
    addAndMakeVisible(songViewport);

    addAndMakeVisible(songFollowBtn);
    songFollowBtn.setClickingTogglesState(true);
    songFollowBtn.setToggleState(true, dontSendNotification);
    songFollowBtn.setTooltip(
        "Auto-scroll the song timeline to keep the currently playing "
        "block in view during song-mode playback. Turn off if you want "
        "to scroll/edit the song manually while it's running.");

    songModeBtn.onClick = [this] { proc.songModeActive = songModeBtn.getToggleState(); };
    addBlockBtn.onClick = [this] {
        proc.song.addBlock(proc.currentBank().currentIndex);
        updateSongTimelineSize();
        songTimeline.repaint();
    };
    remBlockBtn.onClick = [this] {
        if (songTimeline.selectedBlock >= 0) {
            proc.song.removeBlock(songTimeline.selectedBlock);
            songTimeline.selectedBlock = -1;
            updateSongTimelineSize();
            songTimeline.repaint();
        }
    };
    dupBlockBtn.onClick = [this] {
        if (songTimeline.selectedBlock >= 0) {
            proc.song.duplicateBlock(songTimeline.selectedBlock);
            updateSongTimelineSize();
            songTimeline.repaint();
        }
    };
    repPlusBtn.onClick = [this] {
        int idx = songTimeline.selectedBlock;
        if (idx >= 0 && idx < (int)proc.song.blocks.size()) {
            proc.song.blocks[idx].repeats++;
            songTimeline.repaint();
        }
    };
    repMinusBtn.onClick = [this] {
        int idx = songTimeline.selectedBlock;
        if (idx >= 0 && idx < (int)proc.song.blocks.size()) {
            auto& r = proc.song.blocks[idx].repeats;
            if (r > 1) r--;
            songTimeline.repaint();
        }
    };

    // Init
    refreshMidiDeviceList();
    refreshFixtureSelector();
    refreshPatternSelector();
    refreshStepsLabel();
    applyFixtureEdit();
    selectColor(0);

    startTimerHz(30);
}

DMXControllerEditor::~DMXControllerEditor() {
    setLookAndFeel(nullptr);
}

// ============================================================================
// Layout
// ============================================================================
void DMXControllerEditor::resized() {
    auto area = getLocalBounds().reduced(6);
    int rowH = 28;
    int gap  = 4;

    // Row 1: Transport
    {
        auto row = area.removeFromTop(rowH);
        playBtn     .setBounds(row.removeFromLeft(64));  row.removeFromLeft(gap);
        stopBtn     .setBounds(row.removeFromLeft(54));  row.removeFromLeft(gap);
        resetBtn    .setBounds(row.removeFromLeft(60));  row.removeFromLeft(gap);
        autoResetLabel   .setBounds(row.removeFromLeft(74));
        autoResetSelector.setBounds(row.removeFromLeft(100)); row.removeFromLeft(gap);
        tapBtn      .setBounds(row.removeFromLeft(46));  row.removeFromLeft(10);
        bpmLabel    .setBounds(row.removeFromLeft(34));
        bpmSlider   .setBounds(row.removeFromLeft(200)); row.removeFromLeft(10);

        clockSrcLabel   .setBounds(row.removeFromLeft(46));
        clockSrcSelector.setBounds(row.removeFromLeft(120));

        panicBtn   .setBounds(row.removeFromRight(68));  row.removeFromRight(gap);
        blackoutBtn.setBounds(row.removeFromRight(100));
    }
    area.removeFromTop(gap);

    // Row 2: MIDI In + MIDI Out
    {
        auto row = area.removeFromTop(rowH);
        midiInLabel      .setBounds(row.removeFromLeft(56));
        midiInSelector   .setBounds(row.removeFromLeft(200)); row.removeFromLeft(12);

        midiOutLabel     .setBounds(row.removeFromLeft(60));
        midiOutSelector  .setBounds(row.removeFromLeft(200)); row.removeFromLeft(gap);
        midiOutRefreshBtn.setBounds(row.removeFromLeft(26));
    }
    area.removeFromTop(gap);

    // Row 3: Fixture Controls
    {
        auto row = area.removeFromTop(rowH);
        fixtureLabel   .setBounds(row.removeFromLeft(52));
        fixtureSelector.setBounds(row.removeFromLeft(170)); row.removeFromLeft(gap);
        addFixBtn      .setBounds(row.removeFromLeft(24));  row.removeFromLeft(2);
        delFixBtn      .setBounds(row.removeFromLeft(24));  row.removeFromLeft(2);
        dupFixBtn      .setBounds(row.removeFromLeft(40));  row.removeFromLeft(2);
        renameFixBtn   .setBounds(row.removeFromLeft(58));  row.removeFromLeft(2);
        exportFixBtn   .setBounds(row.removeFromLeft(50));  row.removeFromLeft(2);
        importFixBtn   .setBounds(row.removeFromLeft(50));  row.removeFromLeft(8);

        segsLabel_    .setBounds(row.removeFromLeft(28));
        segsSlider    .setBounds(row.removeFromLeft(80)); row.removeFromLeft(gap);
        dmxStartLabel .setBounds(row.removeFromLeft(48));
        dmxStartSlider.setBounds(row.removeFromLeft(90)); row.removeFromLeft(gap);
        profileSelector.setBounds(row.removeFromLeft(150)); row.removeFromLeft(2);
        newProfileBtn .setBounds(row.removeFromLeft(22));   row.removeFromLeft(2);
        delProfileBtn .setBounds(row.removeFromLeft(22));
    }
    area.removeFromTop(gap);

    // Row 3: Bar Preview
    barPreview.setBounds(area.removeFromTop(Theme::BAR_H));
    area.removeFromTop(gap);

    // Row 4: FLOOD + Colour palette + brightness + fill/erase/gen + undo/redo
    {
        auto row = area.removeFromTop(rowH);

        // FLOOD toggle on the far left — positioned above Copy in Row 5
        floodToggleBtn.setBounds(row.removeFromLeft(60));
        row.removeFromLeft(10);

        int btnW = 28;
        for (int i = 0; i < kNumColorBtns; i++) {
            colorBtns[i].setBounds(row.removeFromLeft(btnW));
            row.removeFromLeft(2);
        }
        row.removeFromLeft(10);
        brightnessLabel .setBounds(row.removeFromLeft(44));
        brightnessSlider.setBounds(row.removeFromLeft(110));
        row.removeFromLeft(10);
        fillBtn .setBounds(row.removeFromLeft(44)); row.removeFromLeft(gap);
        clearBtn.setBounds(row.removeFromLeft(50)); row.removeFromLeft(gap);
        eraseBtn.setBounds(row.removeFromLeft(54)); row.removeFromLeft(gap);
        genBtn  .setBounds(row.removeFromLeft(80)); row.removeFromLeft(10);
        undoBtn .setBounds(row.removeFromLeft(32)); row.removeFromLeft(2);
        redoBtn .setBounds(row.removeFromLeft(32));
    }
    area.removeFromTop(gap);

    // Row 5: Tools + Live + Zoom
    {
        auto row = area.removeFromTop(rowH);
        copyBtn  .setBounds(row.removeFromLeft(50)); row.removeFromLeft(gap);
        pasteBtn .setBounds(row.removeFromLeft(50)); row.removeFromLeft(gap);
        shiftLBtn.setBounds(row.removeFromLeft(28)); row.removeFromLeft(gap);
        shiftRBtn.setBounds(row.removeFromLeft(28)); row.removeFromLeft(gap);
        mirrorBtn.setBounds(row.removeFromLeft(55)); row.removeFromLeft(gap);
        randBtn  .setBounds(row.removeFromLeft(62)); row.removeFromLeft(10);

        zoomLabel   .setBounds(row.removeFromLeft(40));
        zoomSelector.setBounds(row.removeFromLeft(90));

        repMinusBtn .setBounds(row.removeFromRight(24)); row.removeFromRight(2);
        repPlusBtn  .setBounds(row.removeFromRight(24)); row.removeFromRight(gap);
        fadeSelector.setBounds(row.removeFromRight(50)); row.removeFromRight(2);
        fadeLabel   .setBounds(row.removeFromRight(34)); row.removeFromRight(gap);
        midiLearnBtn.setBounds(row.removeFromRight(85));
    }
    area.removeFromTop(gap);

    // Row 6a: Live sliders (Master / Hue / Swing / Fix Trim)
    {
        auto row = area.removeFromTop(rowH);
        masterDimLabel .setBounds(row.removeFromLeft(52));
        masterDimSlider.setBounds(row.removeFromLeft(190)); row.removeFromLeft(gap);
        hueLabel       .setBounds(row.removeFromLeft(34));
        hueSlider      .setBounds(row.removeFromLeft(170));
        hueResetBtn    .setBounds(row.removeFromLeft(22)); row.removeFromLeft(gap);
        swingLabel     .setBounds(row.removeFromLeft(50));
        swingSlider    .setBounds(row.removeFromLeft(190)); row.removeFromLeft(10);

        fixBrightLabel .setBounds(row.removeFromLeft(56));
        fixBrightSlider.setBounds(row);
    }
    area.removeFromTop(gap);

    // Bottom: Pattern + Song controls + Scenes + Song timeline
    auto bottomArea = area.removeFromBottom(rowH + kBlockH + gap);

    {
        auto row = bottomArea.removeFromTop(rowH);

        // Scenes on the far right of the pattern row
        for (int i = 3; i >= 0; i--) {
            sceneStoreBtns[i].setBounds(row.removeFromRight(20));
            sceneBtns[i]    .setBounds(row.removeFromRight(30));
            row.removeFromRight(4);
        }
        row.removeFromRight(gap);
        scenesLabel.setBounds(row.removeFromRight(60));
        row.removeFromRight(10);

        patternSelector.setBounds(row.removeFromLeft(128)); row.removeFromLeft(gap);
        newPatBtn      .setBounds(row.removeFromLeft(24));  row.removeFromLeft(2);
        dupPatBtn      .setBounds(row.removeFromLeft(44));  row.removeFromLeft(2);
        delPatBtn      .setBounds(row.removeFromLeft(40));  row.removeFromLeft(2);
        patMoreBtn     .setBounds(row.removeFromLeft(28));  row.removeFromLeft(gap);

        row.removeFromLeft(6);
        stepsMinBtn  .setBounds(row.removeFromLeft(22)); row.removeFromLeft(2);
        stepsLabel   .setBounds(row.removeFromLeft(62)); row.removeFromLeft(2);
        stepsPlusBtn .setBounds(row.removeFromLeft(22)); row.removeFromLeft(gap);
        subdivSelector.setBounds(row.removeFromLeft(68)); row.removeFromLeft(gap);

        songModeBtn  .setBounds(row.removeFromLeft(58)); row.removeFromLeft(gap);
        addBlockBtn  .setBounds(row.removeFromLeft(62)); row.removeFromLeft(gap);
        remBlockBtn  .setBounds(row.removeFromLeft(60)); row.removeFromLeft(gap);
        dupBlockBtn  .setBounds(row.removeFromLeft(64)); row.removeFromLeft(gap);
        songFollowBtn.setBounds(row.removeFromLeft(76));
    }
    bottomArea.removeFromTop(gap);

    songViewport.setBounds(bottomArea);
    updateSongTimelineSize();

    // Main area: grid viewport
    area.removeFromBottom(gap);
    gridViewport.setBounds(area);
    grid.recalcSize();
}

// ============================================================================
// Paint
// ============================================================================
void DMXControllerEditor::paint(Graphics& g) {
    g.fillAll(Theme::BG_PRIMARY);
}

// ============================================================================
// Keyboard
// ============================================================================
bool DMXControllerEditor::keyPressed(const KeyPress& key) {
    // Esc → panic blackout
    if (key == KeyPress::escapeKey) {
        proc.panicBlackout();
        blackoutBtn.setToggleState(true, dontSendNotification);
        return true;
    }

    const bool cmd   = key.getModifiers().isCommandDown();
    const bool shift = key.getModifiers().isShiftDown();
    if (cmd && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')) {
        if (shift) { if (proc.redo()) { grid.repaint(); barPreview.repaint(); } }
        else       { if (proc.undo()) { grid.repaint(); barPreview.repaint(); } }
        return true;
    }

    int num = key.getTextCharacter() - '1';
    if (num >= 0 && num < (int)proc.currentBank().patterns.size()) {
        proc.currentBank().select(num);
        refreshPatternSelector();
        grid.recalcSize();
        grid.repaint();
        return true;
    }
    return false;
}

// ============================================================================
// Timer (30 fps UI refresh)
// ============================================================================
void DMXControllerEditor::timerCallback() {
    grid.repaint();
    barPreview.repaint();
    songTimeline.repaint();

    bool playing = proc.isPlaying.load();
    playBtn.setButtonText(playing ? "PAUSE" : "PLAY");
    playBtn.setColour(TextButton::buttonColourId,
        playing ? Theme::ACCENT.darker(0.3f) : Theme::GREEN_ACCENT.darker(0.3f));

    // BPM display:
    //  - Internal clock: the timer MUST NOT touch bpmSlider at all.
    //    setAlpha / setEnabled / setValue during a live mouse drag
    //    interrupts the slider's drag handling and makes it feel
    //    unresponsive. All enable/alpha state is applied in
    //    clockSrcSelector.onChange (user action, message thread) instead.
    //  - MIDI Clock: mirror the host tempo every tick. The slider stays
    //    setEnabled(true) (set in onChange) because setEnabled(false)
    //    makes JUCE's Slider skip repaints on setValue(), which would
    //    break the host-tempo mirror. The slider's onValueChange lambda
    //    refuses to write proc.bpm when clockSource == 1, so any
    //    accidental drag is a no-op.
    if (proc.clockSource.load() == 1) {
        const double shown = proc.hostBpm.load();
        if (std::abs(bpmSlider.getValue() - shown) > 0.01)
            bpmSlider.setValue(shown, dontSendNotification);
    }

    // Song-mode auto-follow: nudge the song timeline viewport so the
    // currently playing block stays visible.
    if (songFollowBtn.getToggleState() && proc.songModeActive) {
        const int cur = proc.songPlayer.currentBlock;
        if (cur >= 0 && cur < (int)proc.song.blocks.size()) {
            const int blockX = cur * kBlockW;
            const int viewW  = songViewport.getViewWidth();
            const int curX   = songViewport.getViewPositionX();
            const int pad    = kBlockW;  // keep ~one block of lead-in visible
            if (blockX < curX + pad)
                songViewport.setViewPosition(std::max(0, blockX - pad), 0);
            else if (blockX + kBlockW > curX + viewW - pad)
                songViewport.setViewPosition(std::max(0, blockX + kBlockW - viewW + pad), 0);
        }
    }

    blackoutBtn.setToggleState(proc.blackoutActive.load(), dontSendNotification);
    midiLearnBtn.setToggleState(proc.midiLearnActive.load(), dontSendNotification);
    {
        int wantId = proc.autoResetMode.load() + 1;
        if (autoResetSelector.getSelectedId() != wantId)
            autoResetSelector.setSelectedId(wantId, dontSendNotification);
    }
    floodToggleBtn.setToggleState(proc.floodMode.load(), dontSendNotification);

    for (int i = 0; i < 4; i++) {
        sceneBtns[i].setColour(TextButton::buttonColourId,
            proc.scenes[i].occupied ? Theme::ACCENT.darker(0.5f) : Theme::BG_TERTIARY);
    }

    // Highlight the colour button that matches an active flood (if any)
    {
        const bool floodOn = proc.floodActive.load();
        const uint32_t cur = proc.floodColor.load();
        for (int i = 0; i < kNumColorBtns; i++) {
            auto pc = PRESET_COLORS[i];
            const bool match = floodOn && packFloodColor(pc) == cur;
            colorBtns[i].setColour(TextButton::buttonColourId,
                match ? Colour(pc.r, pc.g, pc.b).brighter(0.4f)
                      : Colour(pc.r, pc.g, pc.b));
        }
    }

    // Refresh the MIDI device lists every ~2 seconds
    if (++refreshCounter_ >= 60) {
        refreshCounter_ = 0;
        auto outList = proc.getMidiOutputDeviceNames();
        auto inList  = proc.getMidiInputDeviceNames();
        if (outList != cachedMidiOutDevices_ || inList != cachedMidiInDevices_)
            refreshMidiDeviceList();
    }
}

// ============================================================================
// Helpers
// ============================================================================
void DMXControllerEditor::refreshPatternSelector() {
    patternSelector.clear(dontSendNotification);
    auto& bank = proc.currentBank();
    for (int i = 0; i < (int)bank.patterns.size(); i++)
        patternSelector.addItem(String(bank.patterns[i].name), i + 1);
    patternSelector.setSelectedId(bank.currentIndex + 1, dontSendNotification);
    refreshStepsLabel();
    refreshSubdivSelector();
}

void DMXControllerEditor::refreshStepsLabel() {
    if (auto* p = proc.currentBank().current())
        stepsLabel.setText(String(p->numSteps) + " steps", dontSendNotification);
}

void DMXControllerEditor::refreshSubdivSelector() {
    auto* p = proc.currentBank().current();
    if (!p) return;
    int divs[] = {1, 2, 3, 4, 6, 8, 12};
    for (int i = 0; i < 7; i++) {
        if (divs[i] == p->subdiv) {
            subdivSelector.setSelectedId(i + 1, dontSendNotification);
            return;
        }
    }
}

void DMXControllerEditor::refreshFixtureSelector() {
    fixtureSelector.clear(dontSendNotification);
    for (int i = 0; i < (int)proc.fixtures.size(); i++)
        fixtureSelector.addItem(String(proc.fixtures[i].name), i + 1);
    fixtureSelector.setSelectedId(proc.activeFixture + 1, dontSendNotification);
}

void DMXControllerEditor::refreshMidiDeviceList() {
    // Outputs
    cachedMidiOutDevices_ = proc.getMidiOutputDeviceNames();
    String currentOut = proc.getMidiOutputDeviceName();
    midiOutSelector.clear(dontSendNotification);
    midiOutSelector.addItem("(none)", 1);
    for (int i = 0; i < cachedMidiOutDevices_.size(); i++)
        midiOutSelector.addItem(cachedMidiOutDevices_[i], i + 2);
    if (currentOut.isEmpty()) {
        midiOutSelector.setSelectedId(1, dontSendNotification);
    } else {
        int idx = cachedMidiOutDevices_.indexOf(currentOut);
        midiOutSelector.setSelectedId(idx >= 0 ? idx + 2 : 1, dontSendNotification);
    }

    // Inputs
    cachedMidiInDevices_ = proc.getMidiInputDeviceNames();
    String currentIn = proc.getMidiInputDeviceName();
    midiInSelector.clear(dontSendNotification);
    midiInSelector.addItem("(none)", 1);
    for (int i = 0; i < cachedMidiInDevices_.size(); i++)
        midiInSelector.addItem(cachedMidiInDevices_[i], i + 2);
    if (currentIn.isEmpty()) {
        midiInSelector.setSelectedId(1, dontSendNotification);
    } else {
        int idx = cachedMidiInDevices_.indexOf(currentIn);
        midiInSelector.setSelectedId(idx >= 0 ? idx + 2 : 1, dontSendNotification);
    }
}

void DMXControllerEditor::applyFixtureEdit() {
    auto& fix = proc.fixtures[proc.activeFixture];
    segsSlider    .setValue(fix.numSegments, dontSendNotification);
    dmxStartSlider.setValue(fix.dmxStart + 1, dontSendNotification);  // UI is 1-based
    profileSelector.setSelectedId(fix.profileIndex + 1, dontSendNotification);
    fixBrightSlider.setValue(fix.brightnessOffset, dontSendNotification);
    segsSlider.setEnabled(fix.profile().fixedSegments == 0);
}

void DMXControllerEditor::selectColor(int idx) {
    if (idx < 0 || idx >= NUM_PRESET_COLORS) return;

    // FLOOD mode: clicks go to the flood buffer instead of the paint colour
    if (proc.floodMode.load()) {
        const bool isEraseColor = (idx == NUM_PRESET_COLORS - 1);
        if (isEraseColor) {
            // Clicking the "off/black" tile turns the flood off (not a black flood)
            proc.floodActive.store(false);
            proc.floodColor.store(0);
        } else {
            const auto newColor = PRESET_COLORS[idx];
            const uint32_t packed = packFloodColor(newColor);
            // Toggle off if clicking the same colour that's already flooding
            if (proc.floodActive.load() && proc.floodColor.load() == packed) {
                proc.floodActive.store(false);
                proc.floodColor.store(0);
            } else {
                proc.floodColor.store(packed);
                proc.floodActive.store(true);
            }
        }
        proc.pushPreview();
        grid.repaint();
        barPreview.repaint();
        return;
    }

    // Normal paint-colour selection
    grid.activeColor = PRESET_COLORS[idx];
    grid.eraseMode   = (idx == NUM_PRESET_COLORS - 1);
    eraseBtn.setToggleState(grid.eraseMode, dontSendNotification);
}

void DMXControllerEditor::refreshAll() {
    refreshPatternSelector();
    grid.recalcSize();
    grid.repaint();
    barPreview.repaint();
    songTimeline.repaint();
}

void DMXControllerEditor::showGeneratorMenu() {
    PopupMenu m;

    PopupMenu classic;
    classic.addItem(1, "Chase");
    classic.addItem(2, "Reverse Chase");
    classic.addItem(5, "Strobe");
    classic.addItem(6, "Alternating");
    classic.addItem(7, "Ping-Pong");
    classic.addItem(8, "Larson Scanner (KITT)");
    classic.addItem(9, "Theater Chase");
    classic.addItem(10, "Color Wipe");
    m.addSubMenu("Classic", classic);

    PopupMenu rainbow;
    rainbow.addItem(3,  "Rainbow Sweep");
    rainbow.addItem(4,  "Rainbow Per-Segment");
    rainbow.addItem(11, "Rainbow Chase");
    rainbow.addItem(12, "Rainbow Cycle (all segs)");
    m.addSubMenu("Rainbow", rainbow);

    PopupMenu fx;
    fx.addItem(13, "Sparkle");
    fx.addItem(14, "Twinkle");
    fx.addItem(15, "Fire / Flicker");
    fx.addItem(16, "Meteor Rain");
    fx.addItem(17, "Heartbeat");
    fx.addItem(18, "Sine Pulse");
    fx.addItem(19, "Random Blocks");
    fx.addItem(20, "Breathing");
    fx.addItem(21, "Police (Red/Blue)");
    m.addSubMenu("FX", fx);

    PopupMenu crazy;
    crazy.addItem(30, "Plasma");
    crazy.addItem(31, "Lava Lamp");
    crazy.addItem(32, "Matrix Rain");
    crazy.addItem(33, "Lightning Storm");
    crazy.addItem(34, "Aurora Borealis");
    crazy.addItem(35, "Fireworks");
    crazy.addItem(36, "Bouncing Ball");
    crazy.addItem(37, "Comet Tail");
    crazy.addItem(38, "Sparks");
    crazy.addItem(39, "Barber Pole");
    crazy.addItem(40, "Interference");
    crazy.addItem(41, "DNA Helix");
    crazy.addItem(42, "Rave Strobe");
    crazy.addItem(43, "Disco Ball");
    crazy.addItem(44, "Ocean Waves");
    crazy.addItem(45, "Glitch");
    crazy.addItem(46, "VU Meter");
    crazy.addItem(47, "Checkerboard Flash");
    crazy.addItem(48, "Confetti Explosion");
    crazy.addItem(49, "Cylon Eye");
    crazy.addItem(50, "Rainbow Plasma");
    crazy.addItem(51, "Psychedelic Swirl");
    m.addSubMenu("Crazy", crazy);

    m.addSeparator();
    m.addItem(100, "Clear Pattern");

    m.showMenuAsync(PopupMenu::Options().withTargetComponent(genBtn),
        [this](int result) {
            auto* pat = proc.currentBank().current();
            if (!pat) return;
            proc.snapshot();
            int steps = pat->numSteps;
            int segs  = pat->numSegments;
            RGBColor c = grid.activeColor;

            switch (result) {
                case 1: {
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++)
                        pat->setColor(s, s % segs, c);
                    break;
                }
                case 2: {
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++)
                        pat->setColor(s, (segs - 1 - (s % segs)), c);
                    break;
                }
                case 3: {
                    for (int s = 0; s < steps; s++) {
                        float hue = ((float)s / steps) * 360.0f;
                        RGBColor col = RGBColor::hsvToRgb(hue, 1.0f, 1.0f);
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, col);
                    }
                    break;
                }
                case 4: {
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float hue = std::fmod(((float)(s * segs + seg) / (steps * segs)) * 360.0f, 360.0f);
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    break;
                }
                case 5: {
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s += 2)
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, c);
                    break;
                }
                case 6: {
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, ((s + seg) % 2 == 0) ? c : RGBColor{255,255,255});
                    break;
                }
                case 7: {
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int pos = s % (2 * std::max(1, segs - 1));
                        if (pos >= segs) pos = 2 * (segs - 1) - pos;
                        pat->setColor(s, pos, c);
                    }
                    break;
                }
                case 8: {
                    // Larson / KITT — thicker ping-pong with tail
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int pos = s % (2 * std::max(1, segs - 1));
                        if (pos >= segs) pos = 2 * (segs - 1) - pos;
                        for (int t = 0; t < 4; t++) {
                            int p = pos - t;
                            if (p >= 0 && p < segs) {
                                float f = 1.0f - t * 0.28f;
                                if (f < 0) f = 0;
                                pat->setColor(s, p, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                            }
                        }
                    }
                    break;
                }
                case 9: {
                    // Theater chase — every 3rd segment on, shifts each step
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++)
                            if (((seg + s) % 3) == 0)
                                pat->setColor(s, seg, c);
                    break;
                }
                case 10: {
                    // Color wipe — fill one more segment per step
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int cnt = (s * segs / std::max(1, steps)) + 1;
                        for (int seg = 0; seg < std::min(cnt, segs); seg++)
                            pat->setColor(s, seg, c);
                    }
                    break;
                }
                case 11: {
                    // Rainbow chase
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        float hue = ((float)s / steps) * 360.0f;
                        RGBColor rc = RGBColor::hsvToRgb(hue, 1.0f, 1.0f);
                        pat->setColor(s, s % segs, rc);
                    }
                    break;
                }
                case 12: {
                    // Rainbow cycle — every step fills all segments with a hue
                    for (int s = 0; s < steps; s++) {
                        float hue = ((float)s / steps) * 360.0f;
                        RGBColor rc = RGBColor::hsvToRgb(hue, 1.0f, 1.0f);
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, rc);
                    }
                    break;
                }
                case 13: {
                    // Sparkle — random white flashes on black
                    juce::Random r;
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int hits = std::max(1, segs / 4);
                        for (int k = 0; k < hits; k++) {
                            int seg = r.nextInt(segs);
                            pat->setColor(s, seg, {255, 255, 255});
                        }
                    }
                    break;
                }
                case 14: {
                    // Twinkle — random segments at random brightness of active colour
                    juce::Random r;
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            if (r.nextFloat() < 0.4f) {
                                float f = r.nextFloat();
                                pat->setColor(s, seg, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                            } else pat->setColor(s, seg, {0,0,0});
                        }
                    break;
                }
                case 15: {
                    // Fire / flicker — warm random on every cell
                    juce::Random r;
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            int rr = 180 + r.nextInt(76);
                            int gg = r.nextInt(120);
                            int bb = 0;
                            pat->setColor(s, seg, {(uint8_t)rr, (uint8_t)gg, (uint8_t)bb});
                        }
                    break;
                }
                case 16: {
                    // Meteor rain — bright head moving across, fading tail behind
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int head = (s * segs / std::max(1, steps));
                        for (int t = 0; t < 5; t++) {
                            int p = head - t;
                            if (p >= 0 && p < segs) {
                                float f = 1.0f - t * 0.2f;
                                pat->setColor(s, p, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                            }
                        }
                    }
                    break;
                }
                case 17: {
                    // Heartbeat — double pulse rhythm
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int phase = s % 8;
                        float f = 0.0f;
                        if (phase == 0) f = 1.0f;
                        else if (phase == 1) f = 0.35f;
                        else if (phase == 2) f = 0.85f;
                        else if (phase == 3) f = 0.2f;
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                    }
                    break;
                }
                case 18: {
                    // Sine pulse — smooth brightness wave
                    for (int s = 0; s < steps; s++) {
                        float f = 0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * s / std::max(1, steps));
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                    }
                    break;
                }
                case 19: {
                    // Random blocks — each step a random solid colour
                    juce::Random r;
                    for (int s = 0; s < steps; s++) {
                        RGBColor rc = PRESET_COLORS[r.nextInt(NUM_PRESET_COLORS - 1)];
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, rc);
                    }
                    break;
                }
                case 20: {
                    // Breathing — slow fade in/out triangle wave
                    for (int s = 0; s < steps; s++) {
                        float t = (float)s / std::max(1, steps);
                        float f = 1.0f - std::abs(t * 2.0f - 1.0f);
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, {(uint8_t)(c.r*f), (uint8_t)(c.g*f), (uint8_t)(c.b*f)});
                    }
                    break;
                }
                case 21: {
                    // Police — alternating red/blue halves, flipping every step
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        bool flip = (s % 2) != 0;
                        for (int seg = 0; seg < segs; seg++) {
                            bool leftHalf = seg < segs / 2;
                            bool red = flip ? leftHalf : !leftHalf;
                            pat->setColor(s, seg, red ? RGBColor{255,0,0} : RGBColor{0,0,255});
                        }
                    }
                    break;
                }
                case 30: {
                    // Plasma — smooth interfering sine waves mapped to hue
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs);
                            float t = (float)s   / std::max(1, steps);
                            float v = std::sin(x * 6.28f + t * 6.28f)
                                    + std::sin((x + t) * 9.42f)
                                    + std::sin(std::sqrt((x-0.5f)*(x-0.5f) + (t-0.5f)*(t-0.5f)) * 18.0f);
                            float hue = std::fmod((v * 60.0f + 360.0f), 360.0f);
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    break;
                }
                case 31: {
                    // Lava lamp — blobs drifting with warm orange/red palette
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs);
                            float t = (float)s   / std::max(1, steps);
                            float v = 0.5f + 0.5f * std::sin(x * 10.0f + std::sin(t * 6.28f) * 4.0f);
                            v = std::pow(v, 2.0f);
                            int rr = (int)(255 * v);
                            int gg = (int)(80  * v);
                            pat->setColor(s, seg, {(uint8_t)rr, (uint8_t)gg, 0});
                        }
                    break;
                }
                case 32: {
                    // Matrix rain — green drips falling through columns
                    juce::Random r(42);
                    pat->fillAll({0,0,0});
                    int nDrops = std::max(1, segs / 2);
                    for (int d = 0; d < nDrops; d++) {
                        int col = r.nextInt(segs);
                        int start = r.nextInt(steps);
                        int len = 3 + r.nextInt(5);
                        for (int k = 0; k < len; k++) {
                            int s = (start + k) % steps;
                            int g = 255 - k * (255 / len);
                            pat->setColor(s, col, {0, (uint8_t)g, 0});
                        }
                    }
                    break;
                }
                case 33: {
                    // Lightning storm — mostly black with random bright white flashes
                    juce::Random r;
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        if (r.nextFloat() < 0.12f) {
                            float intensity = 0.5f + r.nextFloat() * 0.5f;
                            uint8_t v = (uint8_t)(255 * intensity);
                            for (int seg = 0; seg < segs; seg++)
                                pat->setColor(s, seg, {v, v, v});
                        }
                    }
                    break;
                }
                case 34: {
                    // Aurora borealis — slow drifting greens/teals/purples
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs);
                            float t = (float)s   / std::max(1, steps);
                            float v = 0.5f + 0.5f * std::sin(x * 3.14f + t * 6.28f);
                            float hue = 120.0f + 60.0f * std::sin(t * 3.14f + x * 2.0f);
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 0.9f, v));
                        }
                    break;
                }
                case 35: {
                    // Fireworks — random bursts radiating from a centre segment
                    juce::Random r;
                    pat->fillAll({0,0,0});
                    int nBursts = std::max(2, steps / 6);
                    for (int b = 0; b < nBursts; b++) {
                        int s0 = r.nextInt(steps);
                        int centre = r.nextInt(segs);
                        float hue = r.nextFloat() * 360.0f;
                        RGBColor col = RGBColor::hsvToRgb(hue, 1.0f, 1.0f);
                        for (int k = 0; k < 4; k++) {
                            int s = (s0 + k) % steps;
                            float f = 1.0f - k * 0.25f;
                            for (int dir = -k; dir <= k; dir++) {
                                int p = centre + dir;
                                if (p >= 0 && p < segs)
                                    pat->setColor(s, p, {(uint8_t)(col.r*f),(uint8_t)(col.g*f),(uint8_t)(col.b*f)});
                            }
                        }
                    }
                    break;
                }
                case 36: {
                    // Bouncing ball — gravity-like vertical bounce mapped to seg position
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        float t = (float)s / std::max(1, steps) * 6.28f;
                        float y = std::abs(std::sin(t));
                        int pos = (int)(y * (segs - 1));
                        pat->setColor(s, pos, c);
                    }
                    break;
                }
                case 37: {
                    // Comet tail — long fading tail chases across
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int head = (s * segs / std::max(1, steps));
                        for (int t = 0; t < 8; t++) {
                            int p = head - t;
                            if (p >= 0 && p < segs) {
                                float f = std::pow(1.0f - t / 8.0f, 2.0f);
                                pat->setColor(s, p, {(uint8_t)(c.r*f),(uint8_t)(c.g*f),(uint8_t)(c.b*f)});
                            }
                        }
                    }
                    break;
                }
                case 38: {
                    // Sparks — tiny random short-lived spots of active colour
                    juce::Random r;
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++)
                            if (r.nextFloat() < 0.08f) {
                                float f = 0.5f + r.nextFloat() * 0.5f;
                                pat->setColor(s, seg, {(uint8_t)(c.r*f),(uint8_t)(c.g*f),(uint8_t)(c.b*f)});
                            }
                    break;
                }
                case 39: {
                    // Barber pole — diagonal stripes of alternating colours
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            int band = ((seg + s) / 2) % 3;
                            RGBColor col;
                            if (band == 0)      col = {255, 0, 0};
                            else if (band == 1) col = {255, 255, 255};
                            else                col = {0, 0, 255};
                            pat->setColor(s, seg, col);
                        }
                    break;
                }
                case 40: {
                    // Interference — two sine waves combined
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float a = std::sin(seg * 0.7f + s * 0.2f);
                            float b = std::sin(seg * 0.3f - s * 0.15f);
                            float v = 0.5f + 0.5f * (a + b) * 0.5f;
                            pat->setColor(s, seg, {(uint8_t)(c.r*v),(uint8_t)(c.g*v),(uint8_t)(c.b*v)});
                        }
                    break;
                }
                case 41: {
                    // DNA helix — two sine waves crossing, different colours
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        float t = (float)s / std::max(1, steps) * 6.28f * 2.0f;
                        int p1 = (int)((0.5f + 0.5f * std::sin(t))        * (segs - 1));
                        int p2 = (int)((0.5f + 0.5f * std::sin(t + 3.14f))* (segs - 1));
                        pat->setColor(s, p1, {255,   0,   0});
                        pat->setColor(s, p2, {  0,   0, 255});
                    }
                    break;
                }
                case 42: {
                    // Rave strobe — alternating full colour / black every step
                    for (int s = 0; s < steps; s++) {
                        bool on = (s % 2) == 0;
                        for (int seg = 0; seg < segs; seg++)
                            pat->setColor(s, seg, on ? c : RGBColor{0,0,0});
                    }
                    break;
                }
                case 43: {
                    // Disco ball — random segs different random hues each step
                    juce::Random r;
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float hue = r.nextFloat() * 360.0f;
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    break;
                }
                case 44: {
                    // Ocean waves — deep blue with white foam caps
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs);
                            float t = (float)s   / std::max(1, steps);
                            float v = 0.5f + 0.5f * std::sin(x * 12.0f + t * 6.28f * 2.0f);
                            int b = 100 + (int)(155 * v);
                            int g = 50  + (int)(100 * v);
                            int rr = v > 0.9f ? 200 : 0;
                            pat->setColor(s, seg, {(uint8_t)rr, (uint8_t)g, (uint8_t)b});
                        }
                    break;
                }
                case 45: {
                    // Glitch — mostly one colour with sudden random garbage
                    juce::Random r;
                    for (int s = 0; s < steps; s++) {
                        bool glitch = r.nextFloat() < 0.25f;
                        for (int seg = 0; seg < segs; seg++) {
                            if (glitch && r.nextFloat() < 0.6f) {
                                pat->setColor(s, seg, {(uint8_t)r.nextInt(256),
                                                       (uint8_t)r.nextInt(256),
                                                       (uint8_t)r.nextInt(256)});
                            } else pat->setColor(s, seg, c);
                        }
                    }
                    break;
                }
                case 46: {
                    // VU meter — bars growing with sine envelope
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        float env = 0.5f + 0.5f * std::sin((float)s / std::max(1, steps) * 6.28f * 3.0f);
                        int level = (int)(env * segs);
                        for (int seg = 0; seg < level; seg++) {
                            float f = (float)seg / std::max(1, segs);
                            RGBColor col;
                            if (f < 0.6f)      col = {0, 255, 0};
                            else if (f < 0.85f) col = {255, 200, 0};
                            else               col = {255, 0, 0};
                            pat->setColor(s, seg, col);
                        }
                    }
                    break;
                }
                case 47: {
                    // Checkerboard flash — invert checkerboard every step
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            bool on = ((seg + s) & 1) == 0;
                            pat->setColor(s, seg, on ? c : RGBColor{255,255,255});
                        }
                    break;
                }
                case 48: {
                    // Confetti explosion — random coloured single-cell pops
                    juce::Random r;
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int pops = 1 + r.nextInt(3);
                        for (int k = 0; k < pops; k++) {
                            int seg = r.nextInt(segs);
                            float hue = r.nextFloat() * 360.0f;
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    }
                    break;
                }
                case 49: {
                    // Cylon eye — single bright centre ping-pong with gradient
                    pat->fillAll({0,0,0});
                    for (int s = 0; s < steps; s++) {
                        int pos = s % (2 * std::max(1, segs - 1));
                        if (pos >= segs) pos = 2 * (segs - 1) - pos;
                        for (int seg = 0; seg < segs; seg++) {
                            int dist = std::abs(seg - pos);
                            float f = std::max(0.0f, 1.0f - dist * 0.35f);
                            pat->setColor(s, seg, {(uint8_t)(255*f), 0, 0});
                        }
                    }
                    break;
                }
                case 50: {
                    // Rainbow plasma — plasma but full hue cycling
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs);
                            float t = (float)s   / std::max(1, steps);
                            float v = std::sin(x * 12.0f + t * 6.28f)
                                    + std::sin((x - t) * 9.42f + 3.0f);
                            float hue = std::fmod((v * 90.0f + t * 720.0f + 360.0f), 360.0f);
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    break;
                }
                case 51: {
                    // Psychedelic swirl — radial-ish swirl hue pattern
                    for (int s = 0; s < steps; s++)
                        for (int seg = 0; seg < segs; seg++) {
                            float x = (float)seg / std::max(1, segs) - 0.5f;
                            float t = (float)s   / std::max(1, steps);
                            float angle = std::atan2(x, t - 0.5f);
                            float hue = std::fmod((angle * 180.0f / 3.14159f + t * 720.0f + 360.0f), 360.0f);
                            pat->setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
                        }
                    break;
                }
                case 100: pat->fillAll({0,0,0}); break;
            }
            grid.repaint();
            barPreview.repaint();
        });
}

// ============================================================================
// Song timeline content sizing (driven by the editor, not the component)
// ============================================================================
void DMXControllerEditor::updateSongTimelineSize() {
    // Content height follows the viewport's inner area; width grows
    // with block count but never shrinks below the visible area.
    const int h       = std::max(kBlockH + 8, songViewport.getHeight());
    const int blocks  = (int)proc.song.blocks.size();
    const int minContentW = blocks * kBlockW + 20;
    const int viewW   = std::max(1, songViewport.getViewWidth());
    const int w       = std::max(minContentW, viewW);
    if (songTimeline.getWidth() != w || songTimeline.getHeight() != h)
        songTimeline.setSize(w, h);
}

// ============================================================================
// Profile selector populator (includes built-ins + user-created profiles)
// ============================================================================
void DMXControllerEditor::refreshProfileSelector() {
    profileSelector.clear(dontSendNotification);
    const auto& all = getFixtureProfiles();
    for (int i = 0; i < (int)all.size(); ++i) {
        juce::String label = all[i].name;
        if (!all[i].isBuiltin) label = "* " + label;   // mark user profiles
        profileSelector.addItem(label, i + 1);
    }
    // Keep the active fixture's selection in sync after a rebuild.
    if (proc.activeFixture >= 0 && proc.activeFixture < (int)proc.fixtures.size())
        profileSelector.setSelectedId(
            proc.fixtures[proc.activeFixture].profileIndex + 1,
            dontSendNotification);
}

// ============================================================================
// "New Custom Profile" dialog
// ============================================================================
void DMXControllerEditor::showNewProfileDialog() {
    auto* aw = new AlertWindow("New Fixture Profile",
        "Define a new fixture profile. Layout is a string of channel "
        "letters: r/g/b (colour), w (white), d (dimmer).\n"
        "Examples:\n"
        "  rgb    - 3ch RGB\n"
        "  drgb   - 4ch dim + RGB (par can style)\n"
        "  rgbw   - 4ch RGBW",
        AlertWindow::NoIcon);

    aw->addTextEditor("name",       "My Fixture",                   "Name:");
    aw->addTextEditor("layout",     "drgb",                         "Layout:");
    aw->addTextEditor("chPerSeg",   "4",                            "Channels per segment:");
    aw->addTextEditor("fixedSegs",  "0",                            "Fixed segments (0 = user-settable):");

    juce::StringArray yesNo;
    yesNo.add("No");
    yesNo.add("Yes");
    aw->addComboBox("hasDim",       yesNo, "Has master dim channel?");
    aw->addComboBox("dimAlwaysMax", yesNo, "Lock dim channel to 100%? (par can style)");

    aw->addTextEditor("description", "",                            "Description (optional):");

    aw->addButton("Create", 1, KeyPress(KeyPress::returnKey));
    aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));

    aw->enterModalState(true,
        ModalCallbackFunction::create([this, aw](int result) {
            if (result != 1) return;

            FixtureProfile prof;
            prof.name               = aw->getTextEditorContents("name").trim().toStdString();
            auto layoutStr          = aw->getTextEditorContents("layout").trim().toLowerCase();
            prof.channelsPerSegment = aw->getTextEditorContents("chPerSeg").getIntValue();
            prof.fixedSegments      = aw->getTextEditorContents("fixedSegs").getIntValue();
            prof.hasMasterDim       = (aw->getComboBoxComponent("hasDim")->getSelectedItemIndex() == 1);
            prof.dimAlwaysMax       = (aw->getComboBoxComponent("dimAlwaysMax")->getSelectedItemIndex() == 1);
            prof.description        = aw->getTextEditorContents("description").trim().toStdString();
            prof.isBuiltin          = false;

            // Convert layout string to vector<char>, ignoring unknown
            // characters so the user can't smuggle in garbage that
            // would misroute DMX channels.
            for (int i = 0; i < layoutStr.length(); ++i) {
                juce_wchar ch = layoutStr[i];
                if (ch == 'r' || ch == 'g' || ch == 'b' || ch == 'w' || ch == 'd')
                    prof.channelLayout.push_back((char)ch);
            }

            // Sanity checks
            if (prof.name.empty()) {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                    "Missing name", "Please enter a name for the profile.");
                return;
            }
            if (prof.channelLayout.empty()) {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                    "Invalid layout",
                    "Layout must contain at least one of: r, g, b, w, d.");
                return;
            }
            if (prof.channelsPerSegment < (int)prof.channelLayout.size())
                prof.channelsPerSegment = (int)prof.channelLayout.size();

            {
                // Mutate the shared profile list under the data lock
                // so the audio thread never observes a half-built entry.
                const ScopedLock l(proc.dataLock);
                getFixtureProfiles().push_back(std::move(prof));
            }
            UserProfileStore::save();
            refreshProfileSelector();
        }), true /* deleteWhenDismissed */);
}

// ============================================================================
// Delete currently-selected user profile (built-ins are protected)
// ============================================================================
void DMXControllerEditor::confirmAndDeleteCurrentUserProfile() {
    const int id = profileSelector.getSelectedId();
    if (id <= 0) return;
    const int idx = id - 1;
    auto& all = getFixtureProfiles();
    if (idx < 0 || idx >= (int)all.size()) return;
    if (all[idx].isBuiltin) {
        AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon,
            "Built-in profile",
            "Built-in profiles cannot be deleted.");
        return;
    }

    const juce::String name(all[idx].name);
    AlertWindow::showOkCancelBox(AlertWindow::WarningIcon,
        "Delete profile?",
        "Delete custom profile \"" + name + "\"?\n\n"
        "Any fixtures currently using it will fall back to the first "
        "built-in profile.",
        "Delete", "Cancel", nullptr,
        ModalCallbackFunction::create([this, idx](int result) {
            if (result != 1) return;  // cancelled
            auto& list = getFixtureProfiles();
            if (idx < 0 || idx >= (int)list.size() || list[idx].isBuiltin)
                return;

            {
                const ScopedLock l(proc.dataLock);
                list.erase(list.begin() + idx);
                // Remap fixtures whose profileIndex is now dangling or
                // has shifted. Anything >= idx was pushed down by 1;
                // anything that equalled idx falls back to profile 0.
                for (auto& fix : proc.fixtures) {
                    if (fix.profileIndex == idx)      fix.profileIndex = 0;
                    else if (fix.profileIndex > idx)  fix.profileIndex -= 1;
                }
            }
            UserProfileStore::save();
            refreshProfileSelector();
            applyFixtureEdit();
            refreshAll();
        }));
}
