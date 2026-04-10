#pragma once
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>
#include <string>
#include <cstdint>

// ============================================================================
// RGB Color
// ============================================================================
struct RGBColor {
    uint8_t r = 0, g = 0, b = 0;

    bool operator==(const RGBColor& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const RGBColor& o) const { return !(*this == o); }
    bool isBlack() const { return r == 0 && g == 0 && b == 0; }

    static RGBColor lerp(const RGBColor& a, const RGBColor& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return {
            (uint8_t)(a.r + (int)((b.r - a.r) * t)),
            (uint8_t)(a.g + (int)((b.g - a.g) * t)),
            (uint8_t)(a.b + (int)((b.b - a.b) * t))
        };
    }

    static RGBColor hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = v - c;
        float r1, g1, b1;
        if      (h < 60)  { r1 = c; g1 = x; b1 = 0; }
        else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
        else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
        else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
        else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
        else              { r1 = c; g1 = 0; b1 = x; }
        return {
            (uint8_t)((r1 + m) * 255),
            (uint8_t)((g1 + m) * 255),
            (uint8_t)((b1 + m) * 255)
        };
    }
};

// Preset colour palette
static const RGBColor PRESET_COLORS[] = {
    {255,   0,   0},   // Red
    {255,  80,   0},   // Orange
    {255, 255,   0},   // Yellow
    {  0, 255,   0},   // Green
    {  0, 255, 255},   // Cyan
    {  0,   0, 255},   // Blue
    {128,   0, 255},   // Purple
    {255,   0, 128},   // Pink
    {255, 255, 255},   // White
    {  0,   0,   0},   // Black (off)
};
static constexpr int NUM_PRESET_COLORS = 10;

// ============================================================================
// Pattern — a grid of [step][segment] = RGBColor
// ============================================================================
class Pattern {
public:
    std::string name;
    int numSteps;
    int numSegments;
    int subdiv;          // subdivisions per beat (e.g. 4 = sixteenth notes)

    Pattern(const std::string& n = "Pattern 1", int steps = 16, int segs = 8, int sub = 4)
        : name(n), numSteps(steps), numSegments(segs), subdiv(sub),
          grid_(steps, std::vector<RGBColor>(segs))
    {}

    // --- Accessors ---
    RGBColor getColor(int step, int seg) const {
        if (step >= 0 && step < numSteps && seg >= 0 && seg < numSegments)
            return grid_[step][seg];
        return {};
    }

    void setColor(int step, int seg, RGBColor c) {
        if (step >= 0 && step < numSteps && seg >= 0 && seg < numSegments)
            grid_[step][seg] = c;
    }

    std::vector<RGBColor> getStepColors(int step) const {
        if (step >= 0 && step < numSteps)
            return grid_[step];
        return std::vector<RGBColor>(numSegments);
    }

    // --- Resize ---
    void setSteps(int n) {
        n = std::clamp(n, 1, 64);
        grid_.resize(n, std::vector<RGBColor>(numSegments));
        numSteps = n;
    }

    // --- Editing tools ---
    void fillAll(RGBColor c) {
        for (auto& step : grid_)
            for (auto& seg : step)
                seg = c;
    }

    // Fill all segments of a single step (vertical column)
    void fillStep(int step, RGBColor c) {
        if (step < 0 || step >= numSteps) return;
        for (auto& seg : grid_[step]) seg = c;
    }

    // Fill one segment across all steps (horizontal row)
    void fillSegment(int seg, RGBColor c) {
        if (seg < 0 || seg >= numSegments) return;
        for (auto& step : grid_) step[seg] = c;
    }

    std::vector<RGBColor> copyStep(int step) const { return getStepColors(step); }

    void pasteStep(int step, const std::vector<RGBColor>& data) {
        if (step < 0 || step >= numSteps) return;
        int n = std::min((int)data.size(), numSegments);
        for (int i = 0; i < n; i++)
            grid_[step][i] = data[i];
    }

    // --- Range copy / paste (for multi-step selections) ---
    std::vector<std::vector<RGBColor>> copyRange(int startStep, int endStep) const {
        if (startStep > endStep) std::swap(startStep, endStep);
        startStep = std::clamp(startStep, 0, numSteps - 1);
        endStep   = std::clamp(endStep,   0, numSteps - 1);
        std::vector<std::vector<RGBColor>> out;
        for (int s = startStep; s <= endStep; ++s)
            out.push_back(grid_[s]);
        return out;
    }

    void pasteRange(int startStep, const std::vector<std::vector<RGBColor>>& data) {
        if (startStep < 0) return;
        for (size_t i = 0; i < data.size(); ++i) {
            int s = startStep + (int)i;
            if (s >= numSteps) break;
            int n = std::min((int)data[i].size(), numSegments);
            for (int seg = 0; seg < n; ++seg)
                grid_[s][seg] = data[i][seg];
        }
    }

    void mirror() { std::reverse(grid_.begin(), grid_.end()); }

    void shiftLeft() {
        if (numSteps <= 1) return;
        auto first = grid_[0];
        for (int i = 0; i < numSteps - 1; i++) grid_[i] = grid_[i + 1];
        grid_[numSteps - 1] = first;
    }

    void shiftRight() {
        if (numSteps <= 1) return;
        auto last = grid_[numSteps - 1];
        for (int i = numSteps - 1; i > 0; i--) grid_[i] = grid_[i - 1];
        grid_[0] = last;
    }

    void randomize(const RGBColor* palette, int paletteSize) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, paletteSize - 1);
        for (auto& step : grid_)
            for (auto& seg : step)
                seg = palette[dist(gen)];
    }

    // --- Built-in generators ---
    static Pattern chase(int steps, int segs, RGBColor color) {
        Pattern p("Chase", steps, segs);
        for (int s = 0; s < steps; s++)
            p.setColor(s, s % segs, color);
        return p;
    }

    static Pattern rainbow(int steps, int segs) {
        Pattern p("Rainbow", steps, segs);
        for (int s = 0; s < steps; s++)
            for (int seg = 0; seg < segs; seg++) {
                float hue = std::fmod((float)(s * segs + seg) / (steps * segs) * 360.0f, 360.0f);
                p.setColor(s, seg, RGBColor::hsvToRgb(hue, 1.0f, 1.0f));
            }
        return p;
    }

    static Pattern strobe(int steps, int segs, RGBColor color) {
        Pattern p("Strobe", steps, segs);
        for (int s = 0; s < steps; s += 2)
            for (int seg = 0; seg < segs; seg++)
                p.setColor(s, seg, color);
        return p;
    }

    static Pattern alternating(int steps, int segs, RGBColor a, RGBColor b) {
        Pattern p("Alternating", steps, segs);
        for (int s = 0; s < steps; s++)
            for (int seg = 0; seg < segs; seg++)
                p.setColor(s, seg, ((s + seg) % 2 == 0) ? a : b);
        return p;
    }

private:
    std::vector<std::vector<RGBColor>> grid_;
};

// ============================================================================
// Pattern Bank — manages a collection of patterns
// ============================================================================
class PatternBank {
public:
    std::vector<Pattern> patterns;
    int currentIndex = 0;
    int currentStep  = 0;

    PatternBank() {
        patterns.emplace_back("Pattern 1", 16, 8, 4);
    }

    Pattern*       current()       { return validIdx(currentIndex) ? &patterns[currentIndex] : nullptr; }
    const Pattern* current() const { return validIdx(currentIndex) ? &patterns[currentIndex] : nullptr; }

    int addPattern(int segs = 8) {
        int n = (int)patterns.size() + 1;
        patterns.emplace_back("Pattern " + std::to_string(n), 16, segs, 4);
        return (int)patterns.size() - 1;
    }

    int duplicatePattern() {
        if (auto* p = current()) {
            patterns.push_back(*p);
            patterns.back().name += " (copy)";
            return (int)patterns.size() - 1;
        }
        return -1;
    }

    void deletePattern(int idx) {
        if (patterns.size() <= 1 || !validIdx(idx)) return;
        patterns.erase(patterns.begin() + idx);
        if (currentIndex >= (int)patterns.size())
            currentIndex = (int)patterns.size() - 1;
    }

    void select(int idx) {
        if (validIdx(idx)) currentIndex = idx;
    }

    void reset() { currentStep = 0; }

private:
    bool validIdx(int i) const { return i >= 0 && i < (int)patterns.size(); }
};
