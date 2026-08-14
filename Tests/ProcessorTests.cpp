// ============================================================================
// Processor tests — end-to-end behaviour of DMXControllerProcessor driven
// through the public interface: processBlock with MIDI clock, host-sync via
// a fake AudioPlayHead, parameters, state persistence, scenes, crossfade.
//
// The plugin's DMX output is observed through the MIDI events it emits into
// the host buffer (note number = DMX channel, velocity ≈ dmx/2).
// ============================================================================
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "AllocCounter.h"

namespace {

// ---- Fake playhead for Host Sync tests -------------------------------------
//
// The flags model what a host actually chooses to report. Not every host
// fills in every field: an AU host that doesn't answer CallHostBeatAndTempo
// leaves the PPQ position empty, and one that doesn't answer
// CallHostTransportState leaves isPlaying stuck at false. Host Sync has to
// cope with both, so the tests can switch each field off independently.
struct FakePlayHead : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        if (reportsBpm) p.setBpm(bpm);
        if (reportsPpq) p.setPpqPosition(ppq);

        if (reportsTransport) {
            // JUCE's AU wrapper populates isPlaying, the loop points and a
            // real timeline position together, off one host callback.
            p.setIsPlaying(playing);
            p.setIsLooping(false);
            p.setLoopPoints(juce::AudioPlayHead::LoopPoints { 0.0, 0.0 });
            p.setTimeInSamples((juce::int64)std::llround(ppq * 60.0 / bpm * sampleRate));
        } else {
            // ...and when that callback fails it falls back to the render
            // engine's free-running sample clock, with NO loop points. That
            // counter keeps climbing whether or not anything is playing, so
            // it must never be mistaken for a timeline.
            p.setTimeInSamples(engineSamples);
        }
        return p;
    }
    bool   playing = false;
    double bpm     = 120.0;
    double ppq     = 0.0;
    double sampleRate = 44100.0;
    juce::int64 engineSamples = 0;   // free-running, never rewinds

    bool reportsTransport = true;
    bool reportsBpm       = true;
    bool reportsPpq       = true;
};

struct Ev {
    int  note, vel, sample;
    bool isOn;
};

static std::vector<Ev> collect(const juce::MidiBuffer& buf, int baseSample = 0) {
    std::vector<Ev> out;
    for (const auto metadata : buf) {
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
            out.push_back({m.getNoteNumber(), (int)m.getVelocity(),
                           baseSample + metadata.samplePosition, true});
        else if (m.isNoteOff())
            out.push_back({m.getNoteNumber(), 0,
                           baseSample + metadata.samplePosition, false});
    }
    return out;
}

// Run one processBlock with the given MIDI input; returns emitted events.
//
// The host MIDI buffer is how these tests observe what the engine produced,
// so this opts into it. It is OFF in the shipping default (see
// sendMidiToHost) — the test below covers that behaviour specifically, and
// drives processBlock directly so it isn't affected by this.
static std::vector<Ev> runBlock(DMXControllerProcessor& p, juce::MidiBuffer midiIn,
                                int numSamples = 512, int baseSample = 0) {
    p.sendMidiToHost.store(true);
    juce::AudioBuffer<float> audio(2, numSamples);
    p.processBlock(audio, midiIn);
    return collect(midiIn, baseSample);
}

static juce::MidiBuffer clockTicks(int n, bool withStart = false) {
    juce::MidiBuffer b;
    int pos = 0;
    if (withStart) b.addEvent(juce::MidiMessage::midiStart(), pos++);
    for (int i = 0; i < n; ++i)
        b.addEvent(juce::MidiMessage::midiClock(), pos++);
    return b;
}

// Default pattern is 16 steps / 8 segs / subdiv 4  →  24/4 = 6 clocks per step.
constexpr int kClocksPerStep = 6;

static void setChasePattern(DMXControllerProcessor& p) {
    const juce::ScopedLock l(p.dataLock);
    p.fixtures[0].patternBank.patterns[0] = Pattern::chase(16, 8, {255, 0, 0});
}

} // namespace

// ----------------------------------------------------------------------------
// FILL: a per-fixture live colour override. The interesting behaviour is not
// "does it output a colour" but how it composes with the controls that
// outrank it, and that one fixture's fill never leaks into another's.
// ----------------------------------------------------------------------------
class FillTests : public juce::UnitTest {
public:
    FillTests() : UnitTest("Fill (per-fixture override)", "processor") {}

    // Read back the DMX value a fixture's first red channel is holding, by
    // running a preview and inspecting the note stream.
    static int redOf(DMXControllerProcessor& p, int fixtureIdx) {
        const juce::ScopedLock l(p.dataLock);
        if (fixtureIdx < 0 || fixtureIdx >= (int)p.fixtures.size()) return -1;
        return p.dmxValueAt(p.fixtures[(size_t)fixtureIdx].dmxStart);
    }

    void runTest() override {
        beginTest("fill lights only the fixture it was set on");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.addFixture();                      // two fixtures

            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0xFF0000);

            expect(redOf(proc, 0) > 200, "fixture 0 is red");
            expectEquals(redOf(proc, 1), 0, "fixture 1 untouched");
        }

        beginTest("two fixtures hold two different colours at once");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.addFixture();

            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0xFF0000);   // red
            proc.activeFixture.store(1);
            proc.setFillForActiveFixture(true, 0x0000FF);   // blue

            const juce::ScopedLock l(proc.dataLock);
            const int f0 = proc.fixtures[0].dmxStart;
            const int f1 = proc.fixtures[1].dmxStart;
            expect(proc.dmxValueAt(f0)     > 200, "fixture 0 red channel high");
            expectEquals(proc.dmxValueAt(f0 + 2), 0, "fixture 0 blue channel off");
            expectEquals(proc.dmxValueAt(f1), 0,     "fixture 1 red channel off");
            expect(proc.dmxValueAt(f1 + 2) > 200, "fixture 1 blue channel high");
        }

        beginTest("flood outranks fill on every fixture");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.addFixture();

            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0x0000FF);   // blue fill
            proc.setFloodParams(true, 0);                   // flood (preset 0)
            // Parameter moves normally re-render via the message thread's
            // preview, which isn't pumping in a headless test.
            proc.forceRecompute();

            const juce::ScopedLock l(proc.dataLock);
            const uint32_t flood = proc.floodColor.load();
            const int f0 = proc.fixtures[0].dmxStart;
            expectEquals(proc.dmxValueAt(f0 + 0), (int)((flood >> 16) & 0xFF),
                         "red channel came from the flood");
            expectEquals(proc.dmxValueAt(f0 + 2), (int)(flood & 0xFF),
                         "blue channel came from the flood, not the blue fill");
        }

        beginTest("blackout outranks fill");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0xFF0000);
            expect(redOf(proc, 0) > 200);

            proc.apvts.getParameter("blackout")->setValueNotifyingHost(1.0f);
            proc.forceRecompute();
            expectEquals(redOf(proc, 0), 0, "blackout kills the fill");
        }

        beginTest("fill outputs with the transport stopped");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            expect(!proc.isPlaying.load(), "precondition: stopped");
            proc.setFillForActiveFixture(true, 0x00FF00);
            expect(redOf(proc, 0) == 0, "red channel off for a green fill");

            const juce::ScopedLock l(proc.dataLock);
            expect(proc.dmxValueAt(proc.fixtures[0].dmxStart + 1) > 200,
                   "green channel lit while stopped");
        }

        beginTest("a fill on one fixture does not wake the others up");
        {
            // The gate that lets a fill output while stopped must be per
            // fixture. If it were rig-wide, filling fixture 0 would lift the
            // "stopped, output nothing" rule for fixture 1 too, and fixture 1
            // would light up with whatever step its pattern is parked on —
            // the opposite of what FILL promises.
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.addFixture();
            {
                const juce::ScopedLock l(proc.dataLock);
                // Give fixture 1 a pattern that is lit at step 0.
                proc.fixtures[1].patternBank.patterns[0] =
                    Pattern::chase(16, 8, {0, 255, 0});
            }
            expect(!proc.isPlaying.load(), "precondition: stopped");

            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0xFF0000);

            const juce::ScopedLock l(proc.dataLock);
            expect(proc.dmxValueAt(proc.fixtures[0].dmxStart) > 200,
                   "fixture 0 holds its fill");
            const int f1 = proc.fixtures[1].dmxStart;
            int lit = 0;
            for (int ch = f1; ch < f1 + proc.fixtures[1].dmxFootprint(); ++ch)
                lit += proc.dmxValueAt(ch);
            expectEquals(lit, 0, "fixture 1 stayed dark");
        }

        beginTest("clearAllFills releases every fixture");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.addFixture();
            proc.activeFixture.store(0);
            proc.setFillForActiveFixture(true, 0xFF0000);
            proc.activeFixture.store(1);
            proc.setFillForActiveFixture(true, 0xFF0000);

            proc.clearAllFills();
            expectEquals(redOf(proc, 0), 0);
            expectEquals(redOf(proc, 1), 0);
        }

        beginTest("fills survive a state round-trip");
        {
            DMXControllerProcessor a, b;
            a.prepareToPlay(44100.0, 512);
            b.prepareToPlay(44100.0, 512);
            a.addFixture();
            a.activeFixture.store(1);
            a.setFillForActiveFixture(true, 0x123456);

            juce::MemoryBlock state;
            a.getStateInformation(state);
            b.setStateInformation(state.getData(), (int)state.getSize());

            const juce::ScopedLock l(b.dataLock);
            expectEquals((int)b.fixtures.size(), 2);
            expect(!b.fixtures[0].fillActive, "fixture 0 had no fill");
            expect(b.fixtures[1].fillActive,  "fixture 1's fill restored");
            expectEquals((int)b.fixtures[1].fillColor, 0x123456);
        }
    }
};
static FillTests fillTests;

// ----------------------------------------------------------------------------
class MidiClockTests : public juce::UnitTest {
public:
    MidiClockTests() : UnitTest("MIDI clock stepping", "processor") {}

    void runTest() override {
        DMXControllerProcessor proc;
        proc.prepareToPlay(44100.0, 512);
        proc.clockSource.store(1);
        setChasePattern(proc);

        beginTest("start + one step of clocks emits step 0 into the host buffer");
        {
            auto evs = runBlock(proc, clockTicks(kClocksPerStep, true));
            // Initial flush covers all 128 channels; find the chase hit.
            bool sawStep0 = false;
            for (auto& e : evs)
                if (e.isOn && e.note == 0) { sawStep0 = true; expectEquals(e.vel, 127); }
            expect(sawStep0, "expected noteOn ch0 vel127 for step 0");
        }

        beginTest("next step emits only the delta (ch3 on, ch0 off)");
        {
            auto evs = runBlock(proc, clockTicks(kClocksPerStep));
            expectEquals((int)evs.size(), 2);
            bool on3 = false, off0 = false;
            for (auto& e : evs) {
                if (e.isOn && e.note == 3 && e.vel == 127) on3 = true;
                if (!e.isOn && e.note == 0)                off0 = true;
            }
            expect(on3 && off0);
        }

        beginTest("MIDI stop rewinds (auto-reset default = last step)");
        {
            juce::MidiBuffer b;
            b.addEvent(juce::MidiMessage::midiStop(), 0);
            runBlock(proc, b);
            expect(!proc.isPlaying.load());
            expectEquals(proc.currentStep.load(), 15);   // parked at last step
        }

        beginTest("MIDI to host is off by default and gates the host stream");
        {
            // Off by default because in Logic the plugin can only run on a
            // Software Instrument track with an instrument loaded, and that
            // instrument would receive every DMX note.
            DMXControllerProcessor p2;
            p2.prepareToPlay(44100.0, 512);
            p2.clockSource.store(1);
            {
                const juce::ScopedLock l(p2.dataLock);
                p2.fixtures[0].patternBank.patterns[0] = Pattern::chase(16, 8, {255, 0, 0});
            }
            expect(!p2.sendMidiToHost.load(), "off by default");

            // Deliberately not via runBlock(), which opts in.
            juce::AudioBuffer<float> audio(2, 512);
            juce::MidiBuffer m1 = clockTicks(kClocksPerStep, true);
            p2.processBlock(audio, m1);
            expectEquals((int)collect(m1).size(), 0,
                         "nothing reaches the host chain");

            p2.sendMidiToHost.store(true);
            juce::MidiBuffer m2 = clockTicks(kClocksPerStep);
            p2.processBlock(audio, m2);
            expect(collect(m2).size() > 0, "enabling it puts the stream back");
        }

        beginTest("clocks are ignored while stopped");
        {
            auto evs = runBlock(proc, clockTicks(kClocksPerStep));
            expectEquals((int)evs.size(), 0);
        }
    }
};
static MidiClockTests midiClockTests;

// ----------------------------------------------------------------------------
class HostSyncTests : public juce::UnitTest {
public:
    HostSyncTests() : UnitTest("Host sync (PPQ)", "processor") {}

    // Advance the fake playhead block by block, collecting events with
    // global sample positions.
    std::vector<Ev> play(DMXControllerProcessor& p, FakePlayHead& ph,
                         int blocks, int blockSize = 512) {
        std::vector<Ev> all;
        const double ppqPerSample = ph.bpm / 60.0 / 44100.0;
        for (int i = 0; i < blocks; ++i) {
            auto evs = runBlock(p, {}, blockSize, i * blockSize);
            all.insert(all.end(), evs.begin(), evs.end());
            ph.ppq += ppqPerSample * blockSize;
        }
        return all;
    }

    void runTest() override {
        beginTest("steps land sample-accurately on the host grid");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            ph.playing = true;
            ph.ppq     = 0.0;
            auto evs = play(proc, ph, 25);   // ~12800 samples ≈ 2.3 steps

            // At 120 BPM, subdiv 4: one step = 60/120/4 s = 5512.5 samples.
            // Step 0 fires at 0, step 1 at ~5512, step 2 at ~11025.
            int s0 = -1, s1 = -1, s2 = -1;
            for (auto& e : evs) {
                if (e.isOn && e.note == 0 && s0 < 0) s0 = e.sample;
                if (e.isOn && e.note == 3 && s1 < 0) s1 = e.sample;
                if (e.isOn && e.note == 6 && s2 < 0) s2 = e.sample;
            }
            expect(s0 >= 0 && s1 >= 0 && s2 >= 0, "all three steps emitted");
            expect(std::abs(s0 - 0)     <= 2, "step 0 at sample ~0 (got "     + juce::String(s0) + ")");
            expect(std::abs(s1 - 5513)  <= 2, "step 1 at sample ~5513 (got "  + juce::String(s1) + ")");
            expect(std::abs(s2 - 11025) <= 2, "step 2 at sample ~11025 (got " + juce::String(s2) + ")");
            expect(proc.isPlaying.load());
        }

        beginTest("swing delays odd steps by an exact fraction of a step");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);
            // 62.5% swing → sw = 0.25 of a step
            proc.apvts.getParameter("swing")->setValueNotifyingHost(
                proc.apvts.getParameter("swing")->convertTo0to1(62.5f));

            ph.playing = true;
            auto evs = play(proc, ph, 16);

            // Step 1 boundary at (1 + 0.25)/4 ppq = 6890.6 samples.
            int s1 = -1;
            for (auto& e : evs)
                if (e.isOn && e.note == 3 && s1 < 0) s1 = e.sample;
            expect(s1 >= 0);
            expect(std::abs(s1 - 6891) <= 2, "swung step 1 at ~6891 (got " + juce::String(s1) + ")");
        }

        beginTest("loop jump re-locks instead of machine-gunning catch-up steps");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            ph.playing = true;
            play(proc, ph, 30);              // run forward ~14 ksamples
            ph.ppq = 0.0;                    // host loops back to bar 1
            auto evs = runBlock(proc, {});
            // Should fire exactly the relocated step (plus its delta),
            // not dozens of catch-up steps.
            int noteOns = 0;
            for (auto& e : evs) if (e.isOn) ++noteOns;
            expectEquals(noteOns, 1);
            expectEquals(proc.currentStep.load(), 0);
        }

        beginTest("transport stop mirrors into isPlaying and auto-resets");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            ph.playing = true;
            play(proc, ph, 12);
            ph.playing = false;
            runBlock(proc, {});
            expect(!proc.isPlaying.load());
            expectEquals(proc.currentStep.load(), 15);   // default auto-reset = last step
        }

        // --------------------------------------------------------------
        // Degraded hosts. Logic reports "nothing at all" through Host Sync
        // on some track types, which is what these cover: the plugin must
        // still step from whatever the host does provide.
        // --------------------------------------------------------------
        beginTest("a free-running engine clock is never mistaken for a timeline");
        {
            // The failure this guards against: JUCE's AU wrapper reports the
            // render engine's sample counter as the position when the host
            // doesn't answer the transport callback. That counter climbs on
            // every block forever. If the plugin treated it as a timeline it
            // would decide the transport is rolling and never stop — a rig
            // free-running with no way to halt it from the DAW, which is a
            // good deal worse than Host Sync doing nothing.
            DMXControllerProcessor proc;
            FakePlayHead ph;
            ph.reportsPpq       = false;
            ph.reportsTransport = false;    // → no loop points, engine clock
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            int noteOns = 0;
            for (int i = 0; i < 40; ++i) {
                ph.engineSamples += 512;    // the engine clock never stops
                for (const auto& e : runBlock(proc, {}, 512, i * 512))
                    if (e.isOn) ++noteOns;
            }
            expectEquals(noteOns, 0, "did not free-run off the engine clock");
            expect(!proc.isPlaying.load(), "transport still reads as stopped");
            expect(!proc.hostDiagHavePpq.load(),
                   "diagnostics report no usable position");
        }

        beginTest("host reports no PPQ: position is derived from the timeline");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            ph.reportsPpq = false;          // no CallHostBeatAndTempo ppq
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            ph.playing = true;
            auto evs = play(proc, ph, 25);

            int s0 = -1, s1 = -1, s2 = -1;
            for (auto& e : evs) {
                if (e.isOn && e.note == 0 && s0 < 0) s0 = e.sample;
                if (e.isOn && e.note == 3 && s1 < 0) s1 = e.sample;
                if (e.isOn && e.note == 6 && s2 < 0) s2 = e.sample;
            }
            expect(s0 >= 0 && s1 >= 0 && s2 >= 0,
                   "steps still fire with no PPQ from the host");
            expect(std::abs(s1 - 5513) <= 8, "step 1 near ~5513 (got " + juce::String(s1) + ")");
            expect(std::abs(s2 - 11025) <= 8, "step 2 near ~11025 (got " + juce::String(s2) + ")");
        }

        beginTest("host never reports isPlaying: motion of the playhead implies it");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            ph.reportsTransport = false;    // no CallHostTransportState
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            ph.playing = true;              // ignored by the playhead
            auto evs = play(proc, ph, 25);

            int noteOns = 0;
            for (auto& e : evs) if (e.isOn) ++noteOns;
            expect(noteOns > 0, "stepped even though the host never said 'playing'");
            expect(proc.isPlaying.load(), "inferred that the transport is rolling");
        }

        beginTest("a frozen playhead is not mistaken for playback");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            ph.reportsTransport = false;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);

            // PPQ never advances: the host is stopped, just silent about it.
            std::vector<Ev> all;
            for (int i = 0; i < 12; ++i) {
                auto evs = runBlock(proc, {}, 512, i * 512);
                all.insert(all.end(), evs.begin(), evs.end());
            }
            int noteOns = 0;
            for (auto& e : all) if (e.isOn) ++noteOns;
            expectEquals(noteOns, 0);
            expect(!proc.isPlaying.load());
        }

        beginTest("zero-length audio buffer still advances the sequence");
        {
            // A MIDI-effect plugin can legitimately be rendered with no
            // audio channels; nothing about stepping may depend on the
            // audio buffer's shape.
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);
            ph.playing = true;
            proc.sendMidiToHost.store(true);   // observe via the host buffer

            const double ppqPerSample = ph.bpm / 60.0 / 44100.0;
            int noteOns = 0;
            for (int i = 0; i < 25; ++i) {
                juce::AudioBuffer<float> audio(0, 0);   // no channels, no samples
                juce::MidiBuffer m;
                proc.processBlock(audio, m);
                for (const auto meta : m)
                    if (meta.getMessage().isNoteOn()) ++noteOns;
                ph.ppq += ppqPerSample * 512;
            }
            expect(noteOns > 0, "stepped with an empty audio buffer");
        }

        beginTest("diagnostics report what the host actually provided");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            setChasePattern(proc);
            ph.playing = true;
            play(proc, ph, 10);

            expect(proc.hostDiagBlocks.load() >= 10, "counted the blocks it saw");
            expect(proc.hostDiagHavePlayhead.load(), "saw a playhead");
            expect(proc.hostDiagHavePpq.load(), "saw a PPQ position");
            expect(proc.hostDiagSteps.load() > 0, "counted the steps it emitted");
        }

        beginTest("diagnostics show when the plugin is never processed");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            expectEquals(proc.hostDiagBlocks.load(), 0);
            expect(!proc.hostDiagHavePlayhead.load());
        }
    }
};
static HostSyncTests hostSyncTests;

// ----------------------------------------------------------------------------
// The audio thread must never allocate: malloc can take a lock held by
// another thread, which is exactly how a plugin produces a dropout under
// load. These tests measure real allocations around processBlock().
// ----------------------------------------------------------------------------
class RealtimeSafetyTests : public juce::UnitTest {
public:
    RealtimeSafetyTests() : UnitTest("Realtime safety", "processor") {}

    void runTest() override {
        beginTest("processBlock does not allocate (MIDI clock stepping)");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0] = Pattern::rainbow(16, 8);
                proc.addFixture();
                proc.addFixture();     // three fixtures, all rendering
            }

            // Warm up: first blocks legitimately grow the host MIDI buffer
            // and take the initial full-channel flush.
            for (int i = 0; i < 20; ++i)
                runBlock(proc, clockTicks(kClocksPerStep, i == 0));

            juce::AudioBuffer<float> audio(2, 512);
            const int before = lc1x::allocCount.load();
            for (int i = 0; i < 50; ++i) {
                juce::MidiBuffer m = clockTicks(kClocksPerStep);
                const int pre = lc1x::allocCount.load();
                proc.processBlock(audio, m);
                juce::ignoreUnused(pre);
            }
            const int grew = lc1x::allocCount.load() - before;
            // The clockTicks() helper itself allocates its MidiBuffer, so
            // allow a small budget per iteration for the test harness and
            // require that processBlock adds nothing on top of it.
            expect(grew <= 50 * 4,
                   "50 blocks caused " + juce::String(grew)
                   + " allocations (budget " + juce::String(50 * 4) + ")");
        }

        beginTest("processBlock does not allocate (host sync stepping)");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0] = Pattern::rainbow(16, 8);
                proc.addFixture();
            }
            ph.playing = true;

            juce::AudioBuffer<float> audio(2, 512);
            const double ppqPerSample = ph.bpm / 60.0 / 44100.0;
            juce::MidiBuffer empty;

            for (int i = 0; i < 20; ++i) {       // warm up
                empty.clear();
                proc.processBlock(audio, empty);
                ph.ppq += ppqPerSample * 512;
            }

            const int before = lc1x::allocCount.load();
            for (int i = 0; i < 200; ++i) {
                empty.clear();
                proc.processBlock(audio, empty);
                ph.ppq += ppqPerSample * 512;
            }
            const int grew = lc1x::allocCount.load() - before;
            expect(grew == 0,
                   "200 host-synced blocks caused " + juce::String(grew)
                   + " allocations (expected 0)");
        }

        beginTest("a crossfading pattern change stays allocation-free");
        {
            DMXControllerProcessor proc;
            FakePlayHead ph;
            proc.setPlayHead(&ph);
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(2);
            proc.crossfadeSteps = 8;
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0] = Pattern::rainbow(16, 8);
                int idx = proc.fixtures[0].patternBank.addPattern(8);
                proc.fixtures[0].patternBank.patterns[(size_t)idx].fillAll({9, 9, 9});
            }
            ph.playing = true;

            juce::AudioBuffer<float> audio(2, 512);
            const double ppqPerSample = ph.bpm / 60.0 / 44100.0;
            juce::MidiBuffer empty;
            for (int i = 0; i < 20; ++i) {
                empty.clear();
                proc.processBlock(audio, empty);
                ph.ppq += ppqPerSample * 512;
            }

            const int before = lc1x::allocCount.load();
            proc.selectPatternWithCrossfade(1);   // captures the fade source
            for (int i = 0; i < 100; ++i) {
                empty.clear();
                proc.processBlock(audio, empty);
                ph.ppq += ppqPerSample * 512;
            }
            const int grew = lc1x::allocCount.load() - before;
            expect(grew == 0,
                   "crossfade capture + playback caused " + juce::String(grew)
                   + " allocations (expected 0)");
        }
    }
};
static RealtimeSafetyTests realtimeSafetyTests;

// ----------------------------------------------------------------------------
class ParameterTests : public juce::UnitTest {
public:
    ParameterTests() : UnitTest("Host parameters", "processor") {}

    void runTest() override {
        beginTest("parameters mirror into the realtime atomics");
        {
            DMXControllerProcessor proc;
            auto set = [&](const char* id, float denorm) {
                auto* p = proc.apvts.getParameter(id);
                p->setValueNotifyingHost(p->convertTo0to1(denorm));
            };
            set("masterDim", 0.25f);
            expectWithinAbsoluteError(proc.masterDimmer.load(), 0.25f, 1e-4f);
            set("hueShift", 90.0f);
            expectWithinAbsoluteError(proc.hueShiftDeg.load(), 90.0f, 1e-3f);
            set("swing", 75.0f);
            expectWithinAbsoluteError(proc.swing.load(), 0.5f, 1e-4f);
            set("blackout", 1.0f);
            expect(proc.blackoutActive.load());
        }

        beginTest("master dimmer scales the emitted DMX");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            setChasePattern(proc);
            auto* dim = proc.apvts.getParameter("masterDim");
            dim->setValueNotifyingHost(dim->convertTo0to1(0.5f));

            auto evs = runBlock(proc, clockTicks(kClocksPerStep, true));
            bool found = false;
            for (auto& e : evs)
                if (e.isOn && e.note == 0) {
                    found = true;
                    // dmx ≈ 128 → velocity ≈ 64
                    expect(std::abs(e.vel - 64) <= 1,
                           "half dimmer → vel ~64 (got " + juce::String(e.vel) + ")");
                }
            expect(found);
        }

        beginTest("blackout parameter kills the output");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            setChasePattern(proc);
            auto* bo = proc.apvts.getParameter("blackout");
            bo->setValueNotifyingHost(1.0f);

            auto evs = runBlock(proc, clockTicks(kClocksPerStep, true));
            for (auto& e : evs)
                expect(!e.isOn, "no noteOns while blacked out");
        }

        beginTest("flood parameters override every segment");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            setChasePattern(proc);
            proc.setFloodParams(true, 0);    // flood Red

            auto evs = runBlock(proc, clockTicks(kClocksPerStep, true));
            // All 8 segments' red channels (0,3,...,21) should be at 127.
            int redOn = 0;
            for (auto& e : evs)
                if (e.isOn && e.vel == 127 && e.note % 3 == 0 && e.note <= 21) ++redOn;
            expectEquals(redOn, 8);
        }

        beginTest("pattern parameter selects patterns (1-based)");
        {
            DMXControllerProcessor proc;
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.addPattern(8);
            }
            auto* pp = proc.apvts.getParameter("pattern");
            pp->setValueNotifyingHost(pp->convertTo0to1(2.0f));
            expectEquals(proc.fixtures[0].patternBank.currentIndex, 1);
            // Out-of-range values clamp inside selectPatternWithCrossfade
            pp->setValueNotifyingHost(pp->convertTo0to1(16.0f));
            expectEquals(proc.fixtures[0].patternBank.currentIndex, 1);
        }
    }
};
static ParameterTests parameterTests;

// ----------------------------------------------------------------------------
class PersistenceTests : public juce::UnitTest {
public:
    PersistenceTests() : UnitTest("State persistence", "processor") {}

    void runTest() override {
        beginTest("full state round-trips through get/setStateInformation");
        {
            DMXControllerProcessor a;
            {
                const juce::ScopedLock l(a.dataLock);
                a.fixtures[0].name = "Left Bar";
                a.fixtures[0].patternBank.patterns[0] = Pattern::chase(16, 8, {255, 0, 0});
                a.fixtures[0].patternBank.addPattern(8);
                a.addFixture();
                a.song.addBlock(1, 3);
                a.midiMappings.push_back({0xB0, 74, DMXControllerProcessor::MidiTarget::MasterDimCC, 0});
            }
            a.crossfadeSteps = 8;
            auto* dim = a.apvts.getParameter("masterDim");
            dim->setValueNotifyingHost(dim->convertTo0to1(0.5f));

            juce::MemoryBlock state;
            a.getStateInformation(state);

            DMXControllerProcessor b;
            b.setStateInformation(state.getData(), (int)state.getSize());

            expectEquals((int)b.fixtures.size(), 2);
            expectEquals(juce::String(b.fixtures[0].name), juce::String("Left Bar"));
            expectEquals(b.serializeFixture(0), a.serializeFixture(0));
            expectEquals(b.serializeFixture(1), a.serializeFixture(1));
            expectEquals((int)b.midiMappings.size(), 1);
            expectEquals(b.midiMappings[0].data1, 74);
            expectEquals((int)b.song.blocks.size(), 1);
            expectEquals(b.song.blocks[0].repeats, 3);
            expectEquals((int)b.crossfadeSteps, 8);
            expectWithinAbsoluteError(b.masterDimmer.load(), 0.5f, 1e-4f);
        }

        beginTest("scene snapshots don't nest (state size stays flat)");
        {
            DMXControllerProcessor p;
            {
                const juce::ScopedLock l(p.dataLock);
                p.fixtures[0].patternBank.patterns[0] = Pattern::rainbow(16, 8);
            }
            p.storeScene(0);
            const auto size0 = p.scenes[0].data.getSize();
            p.storeScene(1);
            p.storeScene(2);
            p.storeScene(3);
            p.storeScene(0);   // re-store with three other scenes occupied
            const auto size0again = p.scenes[0].data.getSize();
            // With the old nesting bug this would balloon ~4x per cycle.
            expect(std::abs((long)size0again - (long)size0) < 512,
                   "re-stored scene grew by " + juce::String((int)(size0again - size0)) + " bytes");
        }

        beginTest("scene load restores patterns and preserves the scene table");
        {
            DMXControllerProcessor p;
            {
                const juce::ScopedLock l(p.dataLock);
                p.fixtures[0].patternBank.patterns[0].fillAll({255, 0, 0});
            }
            p.storeScene(0);
            {
                const juce::ScopedLock l(p.dataLock);
                p.fixtures[0].patternBank.patterns[0].fillAll({0, 0, 255});
            }
            p.storeScene(1);

            expect(p.loadScene(0));
            expect(p.fixtures[0].patternBank.patterns[0].getColor(0, 0) == RGBColor{255, 0, 0});
            expect(p.scenes[1].occupied, "loading a scene must not clobber the others");
            expect(p.loadScene(1));
            expect(p.fixtures[0].patternBank.patterns[0].getColor(0, 0) == RGBColor{0, 0, 255});
        }

        beginTest("pattern import from a wider fixture keeps columns aligned");
        {
            DMXControllerProcessor p;
            {
                const juce::ScopedLock l(p.dataLock);
                p.fixtures[0].numSegments = 4;
                p.fixtures[0].patternBank.patterns[0] = Pattern("p", 16, 4, 4);
            }
            // Source grid from an 8-segment fixture, 2 steps. r encodes
            // (step*16 + seg) so misalignment is instantly visible.
            juce::String grid;
            for (int s = 0; s < 2; ++s)
                for (int seg = 0; seg < 8; ++seg)
                    grid += juce::String::formatted("%02x0000,", s * 16 + seg);
            juce::String json =
                "{\"name\":\"import\",\"steps\":2,\"segs\":8,\"subdiv\":4,"
                "\"grid\":\"" + grid + "\"}";

            expect(p.deserializePatternInto(0, 0, json));
            auto& pat = p.fixtures[0].patternBank.patterns[0];
            expectEquals((int)pat.getColor(0, 0).r, 0x00);
            expectEquals((int)pat.getColor(0, 3).r, 0x03);
            expectEquals((int)pat.getColor(1, 0).r, 0x10);   // was sheared pre-fix
            expectEquals((int)pat.getColor(1, 3).r, 0x13);
        }
    }
};
static PersistenceTests persistenceTests;

// ----------------------------------------------------------------------------
class PlaybackBehaviourTests : public juce::UnitTest {
public:
    PlaybackBehaviourTests() : UnitTest("Playback behaviour", "processor") {}

    void runTest() override {
        beginTest("fixtures with shorter patterns wrap instead of going dark");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0] = Pattern::chase(16, 8, {255, 0, 0});
                proc.addFixture();   // fixture 2 at DMX 24
                auto& fix2 = proc.fixtures[1];
                Pattern shortPat("short", 4, 8, 4);
                shortPat.fillAll({0, 255, 0});               // green everywhere
                fix2.patternBank.patterns[0] = shortPat;
            }
            // Advance 6 steps: the active pattern reaches step 5; the 4-step
            // pattern would previously render out-of-range (black) from step
            // 4 onward. Now it wraps (5 % 4 = 1) and stays lit, so fixture
            // 2's channels (DMX ≥ 24) must never receive a noteOff after the
            // initial flush.
            runBlock(proc, clockTicks(kClocksPerStep, true));
            for (int i = 0; i < 5; ++i)
                for (auto& e : runBlock(proc, clockTicks(kClocksPerStep)))
                    if (e.note >= 24 && !e.isOn)
                        expect(false, "fixture 2 went dark at wrapped step (ch "
                                      + juce::String(e.note) + ")");
        }

        beginTest("crossfade blends between patterns over the fade length");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0].fillAll({255, 0, 0});
                int idx = proc.fixtures[0].patternBank.addPattern(8);
                proc.fixtures[0].patternBank.patterns[idx].fillAll({51, 0, 0});
            }
            proc.crossfadeSteps = 2;

            // Establish steady output on pattern 0 (red 255 → vel 127)
            runBlock(proc, clockTicks(kClocksPerStep, true));
            runBlock(proc, clockTicks(kClocksPerStep));

            proc.selectPatternWithCrossfade(1);

            // Fade step 1: t=0 → still 255 → no delta emitted
            auto evs = runBlock(proc, clockTicks(kClocksPerStep));
            expectEquals((int)evs.size(), 0);
            // Fade step 2: t=0.5 → lerp(255,51) = 153 → vel 77
            evs = runBlock(proc, clockTicks(kClocksPerStep));
            expect(!evs.empty());
            for (auto& e : evs) if (e.isOn) expectEquals(e.vel, 77);
            // Fade done: target 51 → vel 26
            evs = runBlock(proc, clockTicks(kClocksPerStep));
            for (auto& e : evs) if (e.isOn) expectEquals(e.vel, 26);
        }

        beginTest("live previews do not consume crossfade progress");
        {
            // Regression: computeDmxState() used to advance the fade on
            // every call, so a parameter move (which pushes a preview)
            // burned through an 8-step fade in milliseconds.
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            {
                const juce::ScopedLock l(proc.dataLock);
                proc.fixtures[0].patternBank.patterns[0].fillAll({255, 0, 0});
                int idx = proc.fixtures[0].patternBank.addPattern(8);
                proc.fixtures[0].patternBank.patterns[idx].fillAll({51, 0, 0});
            }
            proc.crossfadeSteps = 2;

            runBlock(proc, clockTicks(kClocksPerStep, true));
            runBlock(proc, clockTicks(kClocksPerStep));
            proc.selectPatternWithCrossfade(1);

            // Hammer previews the way an automation ramp would.
            for (int i = 0; i < 20; ++i) proc.pushPreview();

            // The fade must still be at t=0 → first step emits no change.
            auto evs = runBlock(proc, clockTicks(kClocksPerStep));
            expectEquals((int)evs.size(), 0, "previews should not advance the fade");
            // ...and the next step is still the midpoint.
            evs = runBlock(proc, clockTicks(kClocksPerStep));
            expect(!evs.empty());
            for (auto& e : evs) if (e.isOn) expectEquals(e.vel, 77);
        }

        beginTest("song mode advances the chain when the pattern wraps");
        {
            DMXControllerProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            proc.clockSource.store(1);
            {
                const juce::ScopedLock l(proc.dataLock);
                auto& bank = proc.fixtures[0].patternBank;
                bank.patterns[0] = Pattern("a", 4, 8, 4);    // short for fast wrap
                int idx = bank.addPattern(8);
                bank.patterns[idx].name = "b";
                proc.song.addBlock(0, 1);
                proc.song.addBlock(1, 1);
                proc.song.loop = true;
            }
            proc.songModeActive = true;

            runBlock(proc, clockTicks(kClocksPerStep, true));   // step 0 (block advance #1)
            for (int i = 0; i < 4; ++i)
                runBlock(proc, clockTicks(kClocksPerStep));     // wraps → block advance #2
            expectEquals(proc.fixtures[0].patternBank.currentIndex, 1);
        }
    }
};
static PlaybackBehaviourTests playbackBehaviourTests;
