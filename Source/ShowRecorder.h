#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

// ============================================================================
// ShowRecorder — capture a live lighting performance as MIDI
//
// Records the DMX note stream the plugin emits, stamped with musical position,
// and writes it out as a Standard MIDI File. Drop that file onto an External
// MIDI track pointed at the LC-1X+ and the show plays back from the timeline
// with the plugin doing nothing — and, being ordinary MIDI, every cue can then
// be nudged, trimmed or redrawn in the DAW.
//
// It captures the performance, not the pattern: flood hits, scene recalls,
// fader moves and pattern changes all land in the recording exactly as they
// happened, which is the part an offline render of the sequence could never
// reproduce.
//
// Threading. push() is called from whichever thread emitted the DMX — the
// audio thread under Host Sync, the MIDI input thread under MIDI clock, the
// timer thread on the internal clock. It must therefore be realtime-safe: it
// writes into a fixed lock-free FIFO and does nothing else. A timer on the
// message thread drains that FIFO into the event list, and only the message
// thread ever touches the list or the file.
// ============================================================================
class ShowRecorder {
public:
    struct Event {
        double      ppq;                 // absolute musical position
        juce::uint8 status, d1, d2;
    };

    ShowRecorder();
    ~ShowRecorder();

    // ---- Message thread ----

    /// Begin recording. Timestamps passed to push() are already relative to
    /// the start of the take (see DMXControllerProcessor::captureTimestamp),
    /// so `startPpq` is normally 0.
    ///
    /// Resets the FIFO, so the caller MUST ensure no realtime thread can be
    /// inside push() concurrently — AbstractFifo::reset() writes the
    /// producer's index. DMXControllerProcessor::startRecording() guarantees
    /// this by holding dataLock, which every emitDmxDelta() call site holds.
    void start(double startPpq);
    void stop();

    bool isRecording() const noexcept { return recording_.load(std::memory_order_relaxed); }

    /// Number of events captured so far (drained ones only).
    int  eventCount() const { return (int)events_.size(); }

    /// True if the FIFO overflowed at any point during this take, i.e. the
    /// recording is missing events and should not be trusted.
    bool overflowed() const noexcept { return overflowed_.load(std::memory_order_relaxed); }

    /// Musical length of the take in quarter notes, 0 if nothing was captured.
    double lengthInQuarterNotes() const;

    /// Write the captured take as a single-track type-0 Standard MIDI File.
    /// Returns false if nothing was captured or the file couldn't be written.
    bool writeMidiFile(const juce::File& dest) const;

    /// Throw away the take. Same reset caveat as start(): no producer may be
    /// running. Go through DMXControllerProcessor::discardRecording().
    void clear();

    /// Move everything waiting in the FIFO into the event list. Called by the
    /// internal timer; exposed so tests can drain deterministically.
    void drain();

    // ---- Realtime threads ----

    /// Record one emitted DMX message. Realtime-safe: no allocation, no locks,
    /// no syscalls. Silently does nothing when not recording. On overflow it
    /// drops the event and latches the overflow flag rather than blocking —
    /// a missed light cue is far better than a missed audio deadline.
    void push(double ppq, juce::uint8 status, juce::uint8 d1, juce::uint8 d2) noexcept;

private:
    // ~65k events. At a busy 30 changed channels per step and 8 steps a
    // second that's over four minutes of buffer, against a drain that runs
    // ten times a second — the headroom is there so a stalled message thread
    // can't cost you a take.
    //
    // At 16 bytes an event that's a megabyte, which must NOT be an inline
    // array: DMXControllerProcessor embeds a ShowRecorder by value, and a
    // megabyte-wide processor overflows the stack the moment anything
    // constructs one as a local — which the tests do, and so do some host
    // scanning tools. See the static_assert in PluginProcessor.cpp.
    static constexpr int kCapacity        = 1 << 16;
    static constexpr int kTicksPerQuarter = 960;
    static constexpr int kDrainIntervalMs = 100;

    // Drains on the message thread. A nested Timer rather than making the
    // processor inherit juce::Timer, which would collide with the
    // HighResolutionTimer it already uses for the internal clock.
    struct Drainer : juce::Timer {
        explicit Drainer(ShowRecorder& r) : owner(r) {}
        void timerCallback() override { owner.drain(); }
        ShowRecorder& owner;
    };

    juce::AbstractFifo    fifo_ { kCapacity };
    // Sized once in the constructor and never resized, so indexing it from
    // push() on the audio thread allocates nothing.
    std::vector<Event>    slots_;
    std::vector<Event>    events_;
    Drainer               drainer_ { *this };

    std::atomic<bool>   recording_  { false };
    std::atomic<bool>   overflowed_ { false };
    double              startPpq_   { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShowRecorder)
};
