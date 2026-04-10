#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <cctype>
#include <algorithm>

// ============================================================================
// Construction
// ============================================================================
DMXControllerProcessor::DMXControllerProcessor()
    : AudioProcessor(BusesProperties())
{
    std::memset(dmxState_,     0, sizeof(dmxState_));
    std::memset(prevDmxState_, -1, sizeof(prevDmxState_));

    fixtures.emplace_back("Fixture 1", 8, 0, 0);
}

DMXControllerProcessor::~DMXControllerProcessor() {
    stopTimer();
    {
        const juce::ScopedLock l(midiInLock_);
        if (directMidiIn_) directMidiIn_->stop();
        directMidiIn_.reset();
    }
    {
        const juce::ScopedLock l(midiOutLock_);
        directMidiOut_.reset();
    }
}

PatternBank& DMXControllerProcessor::currentBank() {
    return fixtures[activeFixture].patternBank;
}

void DMXControllerProcessor::addFixture() {
    const juce::ScopedLock l(dataLock);
    int nextDmx = 0;
    for (auto& f : fixtures) {
        int end = f.dmxStart + f.numSegments * f.profile().channelsPerSegment;
        if (end > nextDmx) nextDmx = end;
    }
    fixtures.emplace_back("Fixture " + std::to_string(fixtures.size() + 1),
                          8, nextDmx, 0);
    renumberDefaultFixtures();
}

void DMXControllerProcessor::removeFixture(int idx) {
    const juce::ScopedLock l(dataLock);
    if (fixtures.size() <= 1) return;
    if (idx < 0 || idx >= (int)fixtures.size()) return;
    fixtures.erase(fixtures.begin() + idx);
    if (activeFixture >= (int)fixtures.size())
        activeFixture = (int)fixtures.size() - 1;
    renumberDefaultFixtures();
}

void DMXControllerProcessor::renumberDefaultFixtures() {
    // Only rename fixtures that still have the auto-generated "Fixture N"
    // name — leave any user-renamed fixtures alone.
    for (size_t i = 0; i < fixtures.size(); ++i) {
        auto& name = fixtures[i].name;
        bool isDefault = name.rfind("Fixture ", 0) == 0;
        if (isDefault) {
            // verify the rest is digits
            bool onlyDigits = name.size() > 8;
            for (size_t k = 8; k < name.size() && onlyDigits; ++k)
                if (!std::isdigit((unsigned char)name[k])) onlyDigits = false;
            if (onlyDigits)
                name = "Fixture " + std::to_string(i + 1);
        }
    }
}

// ============================================================================
// Prepare / Release
// ============================================================================
void DMXControllerProcessor::prepareToPlay(double sr, int) {
    sampleRate_    = sr;
    sampleCounter_ = 0.0;
}

void DMXControllerProcessor::releaseResources() {}

// ============================================================================
// MIDI Output device management
// ============================================================================
juce::StringArray DMXControllerProcessor::getMidiOutputDeviceNames() {
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add(d.name);
    return names;
}

void DMXControllerProcessor::setMidiOutputDevice(const juce::String& name) {
    const juce::ScopedLock l(midiOutLock_);
    directMidiOut_.reset();
    currentMidiOutName_ = {};

    if (name.isEmpty() || name == "(none)") return;

    for (auto& d : juce::MidiOutput::getAvailableDevices()) {
        if (d.name == name) {
            directMidiOut_ = juce::MidiOutput::openDevice(d.identifier);
            if (directMidiOut_) {
                currentMidiOutName_ = name;
                std::memset(prevDmxState_, -1, sizeof(prevDmxState_));
            }
            return;
        }
    }
}

// ============================================================================
// MIDI Input device management
// ============================================================================
juce::StringArray DMXControllerProcessor::getMidiInputDeviceNames() {
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        names.add(d.name);
    return names;
}

void DMXControllerProcessor::setMidiInputDevice(const juce::String& name) {
    const juce::ScopedLock l(midiInLock_);
    if (directMidiIn_) directMidiIn_->stop();
    directMidiIn_.reset();
    currentMidiInName_ = {};

    if (name.isEmpty() || name == "(none)") return;

    for (auto& d : juce::MidiInput::getAvailableDevices()) {
        if (d.name == name) {
            directMidiIn_ = juce::MidiInput::openDevice(d.identifier, this);
            if (directMidiIn_) {
                directMidiIn_->start();
                currentMidiInName_ = name;
            }
            return;
        }
    }
}

void DMXControllerProcessor::handleIncomingMidiMessage(juce::MidiInput*,
                                                       const juce::MidiMessage& msg) {
    parseIncomingMidi(msg);
}

// ============================================================================
// Process Block
// ============================================================================
void DMXControllerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    buffer.clear();

    // Parse incoming MIDI from the host (always)
    for (auto metadata : midi)
        parseIncomingMidi(metadata.getMessage());
    midi.clear();

    // ---- Host transport tracking (for auto-reset on DAW stop) ----
    // We don't sync step timing to the host — only watch the isPlaying
    // transition so that pressing STOP in the DAW can reset the pattern
    // when Auto-reset is enabled.
    bool hostPlaying = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition())
            hostPlaying = pos->getIsPlaying();
    }
    hostIsPlaying_.store(hostPlaying);

    if (prevHostPlaying_ && !hostPlaying) {
        // DAW transport just stopped
        const int mode = autoResetMode.load();
        if (mode != 0) {
            const juce::ScopedLock l(dataLock);
            for (auto& fix : fixtures) {
                int last = 0;
                if (auto* pat = fix.patternBank.current())
                    last = std::max(0, pat->numSteps - 1);
                int target = (mode == 2) ? last : 0;
                fix.patternBank.currentStep = target;
            }
            // Sync the global step to the active fixture's target
            if (activeFixture >= 0 && activeFixture < (int)fixtures.size())
                currentStep.store(fixtures[activeFixture].patternBank.currentStep);
            sampleCounter_ = 0.0;
            prevHostStep_  = -1;
            midiClockCount_ = 0;
            if (songModeActive) songPlayer.reset();
            computeDmxState();
            emitDmxDelta(nullptr, 0);
        }
    }
    prevHostPlaying_ = hostPlaying;
}

// ============================================================================
// MIDI parsing (clock / transport / learn / triggers)
// ============================================================================
void DMXControllerProcessor::parseIncomingMidi(const juce::MidiMessage& msg) {
    int src = clockSource.load();

    // MIDI Clock (24 ppqn) — only when in MIDI Clock mode
    if (msg.isMidiClock() && src == 1 && isPlaying.load()) {
        midiClockCount_++;
        const juce::ScopedLock l(dataLock);
        if (auto* pat = currentBank().current()) {
            int clocksPerStep = std::max(1, (int)std::round(24.0 / pat->subdiv));
            if (midiClockCount_ % clocksPerStep == 0) {
                advanceStep();
                computeDmxState();
                emitDmxDelta(nullptr, 0);
            }
        }
        return;
    }

    // Transport (MIDI Clock mode only)
    if (src == 1) {
        if (msg.isMidiStart()) {
            // Seed the step position based on auto-reset mode so that the
            // very first MIDI clock tick lands on step 0:
            //  - Last Step mode: start at numSteps-1; first tick wraps to 0.
            //  - Otherwise:      start at 0; but because the first clock
            //                    advances, step 0 will be briefly skipped.
            //                    (Last Step mode is the recommended fix.)
            {
                const juce::ScopedLock l(dataLock);
                const int mode = autoResetMode.load();
                for (auto& fix : fixtures) {
                    int last = 0;
                    if (auto* pat = fix.patternBank.current())
                        last = std::max(0, pat->numSteps - 1);
                    fix.patternBank.currentStep = (mode == 2) ? last : 0;
                }
                if (activeFixture >= 0 && activeFixture < (int)fixtures.size())
                    currentStep.store(fixtures[activeFixture].patternBank.currentStep);
                else
                    currentStep.store(0);
            }
            isPlaying.store(true);
            midiClockCount_ = 0;
            needsInitialSend.store(true);
            return;
        }
        if (msg.isMidiStop())     {
            isPlaying.store(false);
            const int mode = autoResetMode.load();
            if (mode != 0) {
                // Rewind on every fixture so the next play starts cleanly.
                // Mode 1 = first step, Mode 2 = last step (so first clock
                // tick after Start wraps back to step 0).
                const juce::ScopedLock l(dataLock);
                for (auto& fix : fixtures) {
                    int last = 0;
                    if (auto* pat = fix.patternBank.current())
                        last = std::max(0, pat->numSteps - 1);
                    fix.patternBank.currentStep = (mode == 2) ? last : 0;
                }
                if (activeFixture >= 0 && activeFixture < (int)fixtures.size())
                    currentStep.store(fixtures[activeFixture].patternBank.currentStep);
                else
                    currentStep.store(0);
                sampleCounter_  = 0.0;
                prevHostStep_   = -1;
                midiClockCount_ = 0;
                if (songModeActive) songPlayer.reset();
                computeDmxState();
                emitDmxDelta(nullptr, 0);
            }
            return;
        }
        if (msg.isMidiContinue()) { isPlaying.store(true);  return; }
    }

    // Note / CC / PC — always used for learn + triggers
    if (msg.isNoteOn() || msg.isController() || msg.isProgramChange()) {
        int type  = msg.isNoteOn() ? 0x90 : (msg.isController() ? 0xB0 : 0xC0);
        int data1 = msg.isNoteOn()      ? msg.getNoteNumber()
                  : msg.isController()  ? msg.getControllerNumber()
                  :                       msg.getProgramChangeNumber();
        int value = msg.isController() ? msg.getControllerValue()
                  : msg.isNoteOn()     ? msg.getVelocity()
                  :                      0;

        if (midiLearnActive.load()) {
            // Store new mapping; if one exists for same target/param, overwrite
            midiMappings.erase(std::remove_if(midiMappings.begin(), midiMappings.end(),
                [this](const MidiMapping& m) {
                    return m.target == midiLearnTargetType && m.param == midiLearnTargetParam;
                }), midiMappings.end());
            midiMappings.push_back({type, data1, midiLearnTargetType, midiLearnTargetParam});
            midiLearnActive.store(false);
            return;
        }

        // Dispatch on any matching mappings
        for (auto& m : midiMappings) {
            if (m.msgType != type || m.data1 != data1) continue;
            float norm = value / 127.0f;
            switch (m.target) {
                case MidiTarget::PatternSelect:
                    currentBank().select(m.param);
                    break;
                case MidiTarget::BpmCC:
                    bpm = 40.0 + norm * 260.0;  // 40..300
                    break;
                case MidiTarget::BrightnessCC:
                    brightnessLive.store(norm);
                    break;
                case MidiTarget::MasterDimCC:
                    masterDimmer.store(norm);
                    break;
                case MidiTarget::HueCC:
                    hueShiftDeg.store((norm - 0.5f) * 360.0f);
                    break;
                case MidiTarget::SatCC:
                    // Saturation removed from UI — no-op (kept for MIDI map back-compat)
                    break;
                case MidiTarget::SwingCC:
                    swing.store(norm * 0.5f);
                    break;
                case MidiTarget::BlackoutToggle:
                    if (msg.isNoteOn())
                        blackoutActive.store(!blackoutActive.load());
                    break;
                case MidiTarget::FillAll:
                    if (msg.isNoteOn()) {
                        const juce::ScopedLock l(dataLock);
                        if (auto* pat = currentBank().current())
                            pat->fillAll({255,255,255});
                    }
                    break;
                case MidiTarget::Generate:
                    // Simple: trigger chase with white
                    if (msg.isNoteOn()) {
                        const juce::ScopedLock l(dataLock);
                        if (auto* pat = currentBank().current())
                            *pat = Pattern::chase(pat->numSteps, pat->numSegments, {255,255,255});
                    }
                    break;
                case MidiTarget::SceneLoad:
                    if (msg.isNoteOn()) loadScene(m.param);
                    break;
                case MidiTarget::Panic:
                    if (msg.isNoteOn()) panicBlackout();
                    break;
                case MidiTarget::None:
                    break;
            }
        }
    }
}

void DMXControllerProcessor::beginMidiLearn(MidiTarget target, int param) {
    midiLearnTargetType  = target;
    midiLearnTargetParam = param;
    midiLearnActive.store(true);
}

void DMXControllerProcessor::clearMidiMapping(MidiTarget target, int param) {
    midiMappings.erase(std::remove_if(midiMappings.begin(), midiMappings.end(),
        [target, param](const MidiMapping& m) {
            return m.target == target && m.param == param;
        }), midiMappings.end());
}

bool DMXControllerProcessor::findMappingFor(MidiTarget target, int param, MidiMapping& out) const {
    for (const auto& m : midiMappings)
        if (m.target == target && m.param == param) { out = m; return true; }
    return false;
}

// ============================================================================
// HighResolutionTimer — internal clock only
// ============================================================================
void DMXControllerProcessor::hiResTimerCallback() {
    if (!isPlaying.load() || clockSource.load() != 0) return;

    {
        const juce::ScopedLock l(dataLock);
        advanceStep();
        computeDmxState();
        emitDmxDelta(nullptr, 0);
    }

    updateClockTimer();
}

void DMXControllerProcessor::updateClockTimer() {
    auto* pat = currentBank().current();
    if (!pat || bpm <= 0.0 || pat->subdiv <= 0) return;

    double stepMs = 60000.0 / (bpm * pat->subdiv);

    // Swing: odd-indexed steps are delayed by (swing * stepMs).
    // So the interval before firing an odd step is longer; before firing
    // an even step it's shorter.
    double sw = (double)swing.load();
    int nextStep = (currentStep.load() + 1) % pat->numSteps;
    double delta = stepMs * sw;
    double interval = (nextStep % 2 == 1) ? stepMs + delta : stepMs - delta;

    startTimer(juce::jmax(1, (int)std::round(interval)));
}

// ============================================================================
// Step advance
// ============================================================================
void DMXControllerProcessor::advanceStep() {
    auto* pat = currentBank().current();
    if (!pat) return;

    int newStep = (currentStep.load() + 1) % pat->numSteps;
    currentStep.store(newStep);
    currentBank().currentStep = newStep;

    if (songModeActive && newStep == 0) {
        bool continues = songPlayer.advanceBlock(song);
        if (continues) {
            int newPat = songPlayer.getCurrentPatternIndex(song);
            if (newPat != currentBank().currentIndex)
                currentBank().select(newPat);
        } else if (!song.loop) {
            isPlaying.store(false);
            stopTimer();
        }
    }
}

// ============================================================================
// Compute DMX state
// ============================================================================
static RGBColor applyHueSat(RGBColor c, float hueDeg, float satMul) {
    if (hueDeg == 0.0f && satMul == 1.0f) return c;
    // RGB -> HSV
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    float v = mx;
    float d = mx - mn;
    float s = mx == 0.0f ? 0.0f : d / mx;
    float h = 0.0f;
    if (d != 0.0f) {
        if      (mx == r) h = 60.0f * std::fmod(((g - b) / d), 6.0f);
        else if (mx == g) h = 60.0f * (((b - r) / d) + 2.0f);
        else              h = 60.0f * (((r - g) / d) + 4.0f);
    }
    if (h < 0) h += 360.0f;
    h = std::fmod(h + hueDeg + 360.0f, 360.0f);
    s = std::clamp(s * satMul, 0.0f, 1.0f);
    return RGBColor::hsvToRgb(h, s, v);
}

void DMXControllerProcessor::computeDmxState() {
    std::memset(dmxState_, 0, sizeof(dmxState_));

    if (blackoutActive.load()) return;      // hard kill, but transport keeps running
    // Flood is a live override that should output even when stopped
    if (!isPlaying.load() && !previewRequested.load() && !floodActive.load()) return;

    int step = currentStep.load();

    const float    master    = masterDimmer.load();
    const float    hue       = hueShiftDeg.load();
    const bool     flood     = floodActive.load();
    const uint32_t floodPacked = floodColor.load();
    const RGBColor floodCol  = {
        (uint8_t)((floodPacked >> 16) & 0xFF),
        (uint8_t)((floodPacked >>  8) & 0xFF),
        (uint8_t)( floodPacked        & 0xFF)
    };

    auto applyTrim = [](RGBColor c, float trim) {
        c.r = (uint8_t)std::clamp((int)std::round(c.r * trim), 0, 255);
        c.g = (uint8_t)std::clamp((int)std::round(c.g * trim), 0, 255);
        c.b = (uint8_t)std::clamp((int)std::round(c.b * trim), 0, 255);
        return c;
    };

    for (auto& fixture : fixtures) {
        std::vector<RGBColor> colors;
        if (flood) {
            // FLOOD is an ALL-fixtures override. Every fixture in the rig
            // gets the flood colour, mapped through its OWN channel layout
            // (mapColorsToDmx honours {d,r,g,b} par cans, RGBW fixtures,
            // etc.), so mixed rigs flood correctly across differing
            // channel offsets.
            colors.assign(std::max(1, fixture.numSegments), floodCol);
        } else {
            auto* pat = fixture.patternBank.current();
            if (!pat) continue;      // normal playback still needs a pattern
            colors = pat->getStepColors(step);
            colors = applyCrossfade(colors);
        }

        // Apply hue shift (skipped for flood so the chosen colour stays
        // exact), then master + per-fixture brightness
        float trim = master * fixture.brightnessOffset;
        for (auto& c : colors) {
            if (!flood) c = applyHueSat(c, hue, 1.0f);
            c = applyTrim(c, trim);
        }

        // Always route through the fixture's profile so each fixture flood
        // lands on the correct DMX channels. (Previously there was a 3ch
        // fast path here that assumed a {r,g,b} layout — removed so a
        // flood on any layout, including dim-channel par cans, is
        // guaranteed to target the right channels.)
        auto pairs = fixture.mapColorsToDmx(colors);
        for (auto& [off, val] : pairs) {
            int ch = fixture.dmxStart + off;
            if (ch >= 0 && ch < 128)
                dmxState_[ch] = (uint8_t)std::clamp(val, 0, 255);
        }
    }
}

// ============================================================================
// Emit DMX delta
// ============================================================================
void DMXControllerProcessor::emitDmxDelta(juce::MidiBuffer* buf, int sampleOffset) {
    constexpr int midiChannel = 1;

    const juce::ScopedLock l(midiOutLock_);

    for (int ch = 0; ch < 128; ch++) {
        int vel = dmxToVelocity(dmxState_[ch]);
        if (vel == prevDmxState_[ch])
            continue;
        prevDmxState_[ch] = vel;

        juce::MidiMessage m = (vel > 0)
            ? juce::MidiMessage::noteOn(midiChannel, ch, (juce::uint8)vel)
            : juce::MidiMessage::noteOff(midiChannel, ch);

        if (buf) buf->addEvent(m, sampleOffset);
        if (directMidiOut_) directMidiOut_->sendMessageNow(m);
    }
}

int DMXControllerProcessor::dmxToVelocity(int dmx) {
    if (dmx <= 0) return 0;
    return std::min(127, (dmx + 1) / 2);
}

std::vector<RGBColor> DMXControllerProcessor::applyCrossfade(const std::vector<RGBColor>& colors) {
    if (crossfadeFrom_.empty() || crossfadeSteps <= 0)
        return colors;
    if (crossfadeProgress_ >= crossfadeSteps) {
        crossfadeFrom_.clear();
        return colors;
    }

    float t = (float)crossfadeProgress_ / crossfadeSteps;
    crossfadeProgress_++;

    std::vector<RGBColor> blended;
    blended.reserve(colors.size());
    for (int i = 0; i < (int)colors.size(); i++) {
        if (i < (int)crossfadeFrom_.size())
            blended.push_back(RGBColor::lerp(crossfadeFrom_[i], colors[i], t));
        else
            blended.push_back(colors[i]);
    }
    return blended;
}

// ============================================================================
// Preview
// ============================================================================
void DMXControllerProcessor::pushPreview() {
    const juce::ScopedLock l(dataLock);
    previewRequested.store(true);
    computeDmxState();
    emitDmxDelta(nullptr, 0);
    previewRequested.store(false);
}

// ============================================================================
// Transport
// ============================================================================
void DMXControllerProcessor::startPlayback() {
    sampleCounter_ = 0.0;
    needsInitialSend.store(true);

    isPlaying.store(true);
    if (songModeActive) songPlayer.reset();

    {
        const juce::ScopedLock l(dataLock);
        computeDmxState();
        emitDmxDelta(nullptr, 0);
    }

    if (clockSource.load() == 0) updateClockTimer();
}

void DMXControllerProcessor::stopPlayback() {
    stopTimer();
    isPlaying.store(false);

    const juce::ScopedLock l(dataLock);
    // Always zero the DMX output on stop
    std::memset(dmxState_, 0, sizeof(dmxState_));
    emitDmxDelta(nullptr, 0);

    const int mode = autoResetMode.load();
    if (mode != 0) {
        // Rewind playback on every fixture.
        // Mode 1 = first step, Mode 2 = last step (so that the first
        // MIDI clock tick after the next Start wraps back to step 0).
        for (auto& fix : fixtures) {
            int last = 0;
            if (auto* pat = fix.patternBank.current())
                last = std::max(0, pat->numSteps - 1);
            fix.patternBank.currentStep = (mode == 2) ? last : 0;
        }
        if (activeFixture >= 0 && activeFixture < (int)fixtures.size())
            currentStep.store(fixtures[activeFixture].patternBank.currentStep);
        else
            currentStep.store(0);
        sampleCounter_ = 0.0;
        prevHostStep_  = -1;
        midiClockCount_ = 0;
        if (songModeActive) songPlayer.reset();
        // Recompute so the grid playhead visibly snaps back into place
        computeDmxState();
    }
}

void DMXControllerProcessor::resetPlayback() {
    const juce::ScopedLock l(dataLock);
    currentStep.store(0);
    currentBank().reset();
    sampleCounter_ = 0.0;
    prevHostStep_ = -1;
    if (songModeActive) songPlayer.reset();

    computeDmxState();
    emitDmxDelta(nullptr, 0);
}

// ============================================================================
// State persistence
// ============================================================================
void DMXControllerProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto xml = std::make_unique<juce::XmlElement>("DMXControllerState");
    xml->setAttribute("bpm",           bpm);
    xml->setAttribute("clockSource",   clockSource.load());
    xml->setAttribute("songMode",      songModeActive);
    xml->setAttribute("crossfade",     crossfadeSteps);
    xml->setAttribute("midiOutDevice", currentMidiOutName_);
    xml->setAttribute("midiInDevice",  currentMidiInName_);
    xml->setAttribute("activeFixture", activeFixture);
    xml->setAttribute("masterDim",     (double)masterDimmer.load());
    xml->setAttribute("hueShift",      (double)hueShiftDeg.load());
    xml->setAttribute("swing",         (double)swing.load());
    xml->setAttribute("autoResetMode", (int)autoResetMode.load());
    xml->setAttribute("floodMode",       (bool)floodMode.load());

    auto* fixXml = xml->createNewChildElement("Fixtures");
    for (auto& fix : fixtures) {
        auto* f = fixXml->createNewChildElement("Fixture");
        f->setAttribute("name",     juce::String(fix.name));
        f->setAttribute("segments", fix.numSegments);
        f->setAttribute("dmxStart", fix.dmxStart);
        f->setAttribute("profile",  fix.profileIndex);

        f->setAttribute("brightOff", (double)fix.brightnessOffset);

        auto* patsXml = f->createNewChildElement("Patterns");
        for (auto& pat : fix.patternBank.patterns) {
            auto* p = patsXml->createNewChildElement("Pattern");
            p->setAttribute("name",    juce::String(pat.name));
            p->setAttribute("steps",   pat.numSteps);
            p->setAttribute("segs",    pat.numSegments);
            p->setAttribute("subdiv",  pat.subdiv);

            juce::String grid;
            for (int s = 0; s < pat.numSteps; s++)
                for (int seg = 0; seg < pat.numSegments; seg++) {
                    auto c = pat.getColor(s, seg);
                    grid += juce::String::formatted("%02x%02x%02x,", c.r, c.g, c.b);
                }
            p->setAttribute("grid", grid);
        }
    }

    auto* songXml = xml->createNewChildElement("Song");
    songXml->setAttribute("loop", song.loop);
    for (auto& blk : song.blocks) {
        auto* b = songXml->createNewChildElement("Block");
        b->setAttribute("pat", blk.patternIndex);
        b->setAttribute("rep", blk.repeats);
    }

    auto* mapXml = xml->createNewChildElement("MidiMappings");
    for (auto& m : midiMappings) {
        auto* mx = mapXml->createNewChildElement("M");
        mx->setAttribute("type",   m.msgType);
        mx->setAttribute("d1",     m.data1);
        mx->setAttribute("target", (int)m.target);
        mx->setAttribute("param",  m.param);
    }

    auto* scnXml = xml->createNewChildElement("Scenes");
    for (int i = 0; i < 4; i++) {
        if (scenes[i].occupied) {
            auto* s = scnXml->createNewChildElement("S");
            s->setAttribute("i", i);
            s->setAttribute("data", scenes[i].data.toBase64Encoding());
        }
    }

    copyXmlToBinary(*xml, dest);
}

void DMXControllerProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || xml->getTagName() != "DMXControllerState") return;

    const juce::ScopedLock l(dataLock);
    bpm              = xml->getDoubleAttribute("bpm", 120.0);
    clockSource.store(xml->getIntAttribute("clockSource",
                        xml->getBoolAttribute("internalClock", true) ? 0 : 1));
    songModeActive   = xml->getBoolAttribute("songMode", false);
    crossfadeSteps   = xml->getIntAttribute("crossfade", 0);
    activeFixture    = xml->getIntAttribute("activeFixture", 0);
    masterDimmer.store((float)xml->getDoubleAttribute("masterDim", 1.0));
    hueShiftDeg.store ((float)xml->getDoubleAttribute("hueShift",  0.0));
    swing.store      ((float)xml->getDoubleAttribute("swing",     0.0));
    // Back-compat: legacy "autoResetOnStop" bool maps to mode 1 (first step).
    if (xml->hasAttribute("autoResetMode"))
        autoResetMode.store(xml->getIntAttribute("autoResetMode", 0));
    else
        autoResetMode.store(xml->getBoolAttribute("autoResetOnStop", false) ? 1 : 0);
    floodMode.store(xml->getBoolAttribute("floodMode", false));
    // Flood output itself is a transient live state and is NOT restored
    floodActive.store(false);
    floodColor.store(0);
    auto savedOut    = xml->getStringAttribute("midiOutDevice");
    auto savedIn     = xml->getStringAttribute("midiInDevice");

    if (auto* fixXml = xml->getChildByName("Fixtures")) {
        fixtures.clear();
        for (auto* f : fixXml->getChildIterator()) {
            FixtureConfig fix;
            fix.name         = f->getStringAttribute("name").toStdString();
            fix.numSegments  = f->getIntAttribute("segments", 8);
            fix.dmxStart     = f->getIntAttribute("dmxStart", 0);
            fix.profileIndex = f->getIntAttribute("profile", 0);
            fix.brightnessOffset = (float)f->getDoubleAttribute("brightOff", 1.0);

            if (auto* patsXml = f->getChildByName("Patterns")) {
                fix.patternBank.patterns.clear();
                for (auto* p : patsXml->getChildIterator()) {
                    Pattern pat(
                        p->getStringAttribute("name").toStdString(),
                        p->getIntAttribute("steps", 16),
                        p->getIntAttribute("segs", 8),
                        p->getIntAttribute("subdiv", 4));

                    auto gridStr = p->getStringAttribute("grid");
                    juce::StringArray tokens;
                    tokens.addTokens(gridStr, ",", "");
                    int idx = 0;
                    for (int s = 0; s < pat.numSteps && idx < tokens.size(); s++)
                        for (int seg = 0; seg < pat.numSegments && idx < tokens.size(); seg++, idx++) {
                            auto hex = tokens[idx].trim();
                            if (hex.length() == 6) {
                                int r = hex.substring(0, 2).getHexValue32();
                                int g = hex.substring(2, 4).getHexValue32();
                                int b = hex.substring(4, 6).getHexValue32();
                                pat.setColor(s, seg, {(uint8_t)r, (uint8_t)g, (uint8_t)b});
                            }
                        }

                    fix.patternBank.patterns.push_back(std::move(pat));
                }
            }
            fixtures.push_back(std::move(fix));
        }
        if (fixtures.empty())
            fixtures.emplace_back("Fixture 1", 8, 0, 0);
    }
    if (activeFixture >= (int)fixtures.size()) activeFixture = 0;

    if (auto* songXml = xml->getChildByName("Song")) {
        song.loop = songXml->getBoolAttribute("loop", true);
        song.blocks.clear();
        for (auto* b : songXml->getChildIterator())
            song.addBlock(b->getIntAttribute("pat", 0), b->getIntAttribute("rep", 1));
    }

    if (auto* mapXml = xml->getChildByName("MidiMappings")) {
        midiMappings.clear();
        for (auto* mx : mapXml->getChildIterator()) {
            MidiMapping mm;
            mm.msgType = mx->getIntAttribute("type");
            mm.data1   = mx->getIntAttribute("d1");
            // Back-compat: legacy files stored "pat" → PatternSelect
            if (mx->hasAttribute("target")) {
                mm.target = (MidiTarget)mx->getIntAttribute("target");
                mm.param  = mx->getIntAttribute("param");
            } else {
                mm.target = MidiTarget::PatternSelect;
                mm.param  = mx->getIntAttribute("pat");
            }
            midiMappings.push_back(mm);
        }
    }

    for (int i = 0; i < 4; i++) { scenes[i].data.reset(); scenes[i].occupied = false; }
    if (auto* scnXml = xml->getChildByName("Scenes")) {
        for (auto* s : scnXml->getChildIterator()) {
            int i = s->getIntAttribute("i", -1);
            if (i >= 0 && i < 4) {
                scenes[i].data.fromBase64Encoding(s->getStringAttribute("data"));
                scenes[i].occupied = scenes[i].data.getSize() > 0;
            }
        }
    }

    if (savedOut.isNotEmpty()) setMidiOutputDevice(savedOut);
    if (savedIn.isNotEmpty())  setMidiInputDevice(savedIn);
}

// ============================================================================
// Undo / Redo
// ============================================================================
void DMXControllerProcessor::snapshot() {
    const juce::ScopedLock l(dataLock);
    if (activeFixture < 0 || activeFixture >= (int)fixtures.size()) return;
    auto& bank = fixtures[activeFixture].patternBank;
    if (auto* pat = bank.current()) {
        undoStack_.push_back({activeFixture, bank.currentIndex, *pat});
        if (undoStack_.size() > 100) undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }
}

bool DMXControllerProcessor::undo() {
    const juce::ScopedLock l(dataLock);
    if (undoStack_.empty()) return false;
    auto entry = undoStack_.back();
    undoStack_.pop_back();
    if (entry.fixtureIdx < 0 || entry.fixtureIdx >= (int)fixtures.size()) return false;
    auto& bank = fixtures[entry.fixtureIdx].patternBank;
    if (entry.patternIdx < 0 || entry.patternIdx >= (int)bank.patterns.size()) return false;
    // push current into redo
    redoStack_.push_back({entry.fixtureIdx, entry.patternIdx, bank.patterns[entry.patternIdx]});
    bank.patterns[entry.patternIdx] = entry.snap;
    return true;
}

bool DMXControllerProcessor::redo() {
    const juce::ScopedLock l(dataLock);
    if (redoStack_.empty()) return false;
    auto entry = redoStack_.back();
    redoStack_.pop_back();
    if (entry.fixtureIdx < 0 || entry.fixtureIdx >= (int)fixtures.size()) return false;
    auto& bank = fixtures[entry.fixtureIdx].patternBank;
    if (entry.patternIdx < 0 || entry.patternIdx >= (int)bank.patterns.size()) return false;
    undoStack_.push_back({entry.fixtureIdx, entry.patternIdx, bank.patterns[entry.patternIdx]});
    bank.patterns[entry.patternIdx] = entry.snap;
    return true;
}

// ============================================================================
// Scenes — capture/restore entire plugin state
// ============================================================================
void DMXControllerProcessor::storeScene(int idx) {
    if (idx < 0 || idx >= 4) return;
    juce::MemoryBlock mb;
    getStateInformation(mb);
    scenes[idx].data = mb;
    scenes[idx].occupied = true;
}

bool DMXControllerProcessor::loadScene(int idx) {
    if (idx < 0 || idx >= 4) return false;
    if (!scenes[idx].occupied) return false;
    // Preserve the scenes table across the load — otherwise loading scene A
    // would replace scenes[] with whatever was stored at the time of storing A
    // (which may have been empty), clobbering B/C/D.
    Scene savedScenes[4];
    for (int i = 0; i < 4; i++) savedScenes[i] = scenes[i];
    setStateInformation(scenes[idx].data.getData(), (int)scenes[idx].data.getSize());
    for (int i = 0; i < 4; i++) scenes[i] = savedScenes[i];
    return true;
}

// ============================================================================
// Tap tempo
// ============================================================================
void DMXControllerProcessor::tapTempo() {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (!tapTimes_.empty() && (now - tapTimes_.back()) > 2500.0)
        tapTimes_.clear();   // reset on big gap
    tapTimes_.push_back(now);
    if (tapTimes_.size() > 5) tapTimes_.erase(tapTimes_.begin());
    if (tapTimes_.size() >= 2) {
        double sumMs = 0.0;
        for (size_t i = 1; i < tapTimes_.size(); ++i)
            sumMs += tapTimes_[i] - tapTimes_[i - 1];
        double avgMs = sumMs / (tapTimes_.size() - 1);
        if (avgMs > 0.0) {
            double newBpm = 60000.0 / avgMs;
            newBpm = juce::jlimit(40.0, 300.0, newBpm);
            bpm = newBpm;
            if (isPlaying.load() && clockSource.load() == 0) updateClockTimer();
        }
    }
}

// ============================================================================
// Panic blackout — zero all 128 channels, clear blackout flag after
// ============================================================================
void DMXControllerProcessor::panicBlackout() {
    const juce::ScopedLock l(dataLock);
    std::memset(dmxState_, 0, sizeof(dmxState_));
    emitDmxDelta(nullptr, 0);
    blackoutActive.store(true);
}

// ============================================================================
// Duplicate fixture
// ============================================================================
void DMXControllerProcessor::duplicateFixture(int idx) {
    const juce::ScopedLock l(dataLock);
    if (idx < 0 || idx >= (int)fixtures.size()) return;
    FixtureConfig copy = fixtures[idx];
    // Bump DMX start to the next free block
    int nextDmx = 0;
    for (auto& f : fixtures) {
        int end = f.dmxStart + f.numSegments * f.profile().channelsPerSegment;
        if (end > nextDmx) nextDmx = end;
    }
    copy.dmxStart = nextDmx;
    copy.name = "Fixture " + std::to_string(fixtures.size() + 1);
    fixtures.push_back(std::move(copy));
    renumberDefaultFixtures();
}

// ============================================================================
// Fixture JSON serialize / deserialize
// ============================================================================
juce::String DMXControllerProcessor::serializeFixture(int idx) const {
    if (idx < 0 || idx >= (int)fixtures.size()) return {};
    const auto& fix = fixtures[idx];

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name",       juce::String(fix.name));
    obj->setProperty("segments",   fix.numSegments);
    obj->setProperty("dmxStart",   fix.dmxStart);
    obj->setProperty("profile",    fix.profileIndex);
    obj->setProperty("brightOff",  (double)fix.brightnessOffset);

    juce::Array<juce::var> patArr;
    for (auto& pat : fix.patternBank.patterns) {
        juce::DynamicObject::Ptr po = new juce::DynamicObject();
        po->setProperty("name",   juce::String(pat.name));
        po->setProperty("steps",  pat.numSteps);
        po->setProperty("segs",   pat.numSegments);
        po->setProperty("subdiv", pat.subdiv);

        juce::String grid;
        for (int s = 0; s < pat.numSteps; s++)
            for (int seg = 0; seg < pat.numSegments; seg++) {
                auto c = pat.getColor(s, seg);
                grid += juce::String::formatted("%02x%02x%02x,", c.r, c.g, c.b);
            }
        po->setProperty("grid", grid);
        patArr.add(juce::var(po.get()));
    }
    obj->setProperty("patterns", patArr);

    return juce::JSON::toString(juce::var(obj.get()), true);
}

bool DMXControllerProcessor::deserializeFixtureInto(int idx, const juce::String& json) {
    juce::var parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;
    const juce::ScopedLock l(dataLock);
    if (idx < 0 || idx >= (int)fixtures.size()) return false;

    auto& fix = fixtures[idx];
    fix.name         = parsed.getProperty("name", "Fixture").toString().toStdString();
    fix.numSegments  = (int)parsed.getProperty("segments", 8);
    fix.dmxStart     = (int)parsed.getProperty("dmxStart", 0);
    fix.profileIndex = (int)parsed.getProperty("profile",  0);
    fix.brightnessOffset = (float)(double)parsed.getProperty("brightOff", 1.0);

    fix.patternBank.patterns.clear();
    auto pats = parsed.getProperty("patterns", juce::var());
    if (auto* arr = pats.getArray()) {
        for (auto& pv : *arr) {
            Pattern pat(
                pv.getProperty("name",   "Pattern").toString().toStdString(),
                (int)pv.getProperty("steps",  16),
                (int)pv.getProperty("segs",   8),
                (int)pv.getProperty("subdiv", 4));
            auto gridStr = pv.getProperty("grid", "").toString();
            juce::StringArray tokens;
            tokens.addTokens(gridStr, ",", "");
            int ti = 0;
            for (int s = 0; s < pat.numSteps && ti < tokens.size(); s++)
                for (int seg = 0; seg < pat.numSegments && ti < tokens.size(); seg++, ti++) {
                    auto hex = tokens[ti].trim();
                    if (hex.length() == 6) {
                        int r = hex.substring(0, 2).getHexValue32();
                        int g = hex.substring(2, 4).getHexValue32();
                        int b = hex.substring(4, 6).getHexValue32();
                        pat.setColor(s, seg, {(uint8_t)r, (uint8_t)g, (uint8_t)b});
                    }
                }
            fix.patternBank.patterns.push_back(std::move(pat));
        }
    }
    if (fix.patternBank.patterns.empty())
        fix.patternBank.patterns.emplace_back("Pattern 1", 16, fix.numSegments, 4);
    fix.patternBank.currentIndex = 0;
    return true;
}

// ============================================================================
// Pattern JSON serialize / deserialize (single pattern within a fixture)
// ============================================================================
juce::String DMXControllerProcessor::serializePattern(int fixtureIdx, int patIdx) const {
    if (fixtureIdx < 0 || fixtureIdx >= (int)fixtures.size()) return {};
    const auto& fix = fixtures[fixtureIdx];
    if (patIdx < 0 || patIdx >= (int)fix.patternBank.patterns.size()) return {};
    const auto& pat = fix.patternBank.patterns[patIdx];

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name",   juce::String(pat.name));
    obj->setProperty("steps",  pat.numSteps);
    obj->setProperty("segs",   pat.numSegments);
    obj->setProperty("subdiv", pat.subdiv);

    juce::String grid;
    for (int s = 0; s < pat.numSteps; s++)
        for (int seg = 0; seg < pat.numSegments; seg++) {
            auto c = pat.getColor(s, seg);
            grid += juce::String::formatted("%02x%02x%02x,", c.r, c.g, c.b);
        }
    obj->setProperty("grid", grid);
    return juce::JSON::toString(juce::var(obj.get()), true);
}

bool DMXControllerProcessor::deserializePatternInto(int fixtureIdx, int patIdx, const juce::String& json) {
    juce::var parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;
    const juce::ScopedLock l(dataLock);
    if (fixtureIdx < 0 || fixtureIdx >= (int)fixtures.size()) return false;
    auto& fix = fixtures[fixtureIdx];
    if (patIdx < 0 || patIdx >= (int)fix.patternBank.patterns.size()) return false;

    int steps  = (int)parsed.getProperty("steps",  16);
    int segs   = (int)parsed.getProperty("segs",   fix.numSegments);
    int subdiv = (int)parsed.getProperty("subdiv", 4);
    // Clamp segments to the fixture's actual count — the imported pattern
    // might have been built for a different fixture.
    segs = std::min(segs, fix.numSegments);

    Pattern pat(
        parsed.getProperty("name", "Pattern").toString().toStdString(),
        steps, fix.numSegments, subdiv);

    auto gridStr = parsed.getProperty("grid", "").toString();
    juce::StringArray tokens;
    tokens.addTokens(gridStr, ",", "");
    int ti = 0;
    for (int s = 0; s < steps && ti < tokens.size(); s++)
        for (int seg = 0; seg < segs && ti < tokens.size(); seg++, ti++) {
            auto hex = tokens[ti].trim();
            if (hex.length() == 6) {
                int r = hex.substring(0, 2).getHexValue32();
                int g = hex.substring(2, 4).getHexValue32();
                int b = hex.substring(4, 6).getHexValue32();
                pat.setColor(s, seg, {(uint8_t)r, (uint8_t)g, (uint8_t)b});
            }
        }
    fix.patternBank.patterns[patIdx] = std::move(pat);
    return true;
}

// ============================================================================
// Plugin instantiation
// ============================================================================
juce::AudioProcessorEditor* DMXControllerProcessor::createEditor() {
    return new DMXControllerEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new DMXControllerProcessor();
}
