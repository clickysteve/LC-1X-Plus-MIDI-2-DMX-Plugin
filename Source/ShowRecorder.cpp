#include "ShowRecorder.h"

ShowRecorder::ShowRecorder() {
    // Reserve up front so the first drain of a take doesn't allocate at a
    // moment when the user is mid-performance.
    events_.reserve(1 << 14);
}

ShowRecorder::~ShowRecorder() {
    drainer_.stopTimer();
}

// ----------------------------------------------------------------------------
// Message thread
// ----------------------------------------------------------------------------
void ShowRecorder::start(double startPpq) {
    // Discard anything stale left in the FIFO from a previous take, so the
    // new one can't inherit its tail.
    fifo_.reset();
    events_.clear();
    overflowed_.store(false, std::memory_order_relaxed);
    startPpq_ = startPpq;

    recording_.store(true, std::memory_order_relaxed);
    drainer_.startTimer(kDrainIntervalMs);
}

void ShowRecorder::stop() {
    recording_.store(false, std::memory_order_relaxed);
    drainer_.stopTimer();
    drain();                 // sweep up whatever the last tick missed
}

void ShowRecorder::clear() {
    fifo_.reset();
    events_.clear();
    overflowed_.store(false, std::memory_order_relaxed);
}

void ShowRecorder::drain() {
    int start1, size1, start2, size2;
    fifo_.prepareToRead(fifo_.getNumReady(), start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i) events_.push_back(slots_[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) events_.push_back(slots_[(size_t)(start2 + i)]);

    fifo_.finishedRead(size1 + size2);
}

double ShowRecorder::lengthInQuarterNotes() const {
    if (events_.empty()) return 0.0;
    // Events arrive in emission order, which is chronological, but a
    // max() costs nothing and survives any future reordering.
    double last = 0.0;
    for (const auto& e : events_)
        last = std::max(last, e.ppq - startPpq_);
    return std::max(0.0, last);
}

bool ShowRecorder::writeMidiFile(const juce::File& dest) const {
    if (events_.empty()) return false;

    juce::MidiMessageSequence seq;

    auto name = juce::MidiMessage::textMetaEvent(3, "LC-1X+ MIDI2DMX show");
    name.setTimeStamp(0.0);
    seq.addEvent(name);

    for (const auto& e : events_) {
        juce::MidiMessage m(e.status, e.d1, e.d2);
        // Timestamps in a MidiFile are ticks. Rebasing on startPpq_ means a
        // take captured at bar 40 still begins at the top of the file, so it
        // can be dropped anywhere on the timeline.
        m.setTimeStamp(std::max(0.0, e.ppq - startPpq_) * (double)kTicksPerQuarter);
        seq.addEvent(m);
    }

    // Deliberately NOT updateMatchedPairs(). It does more than link pairs:
    // when it finds a second note-on on the same note before any note-off, it
    // INSERTS a synthetic note-off just before it. Here the note number is
    // the DMX channel, so every value change on a channel is another note-on
    // on the same note — pairing would inject a note-off, i.e. a momentary
    // drop to zero, at every single change. A fade would record as a
    // strobing mess that looked nothing like the performance.
    //
    // The stream already carries explicit note-offs for channels going to
    // zero, so nothing needs pairing up.

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kTicksPerQuarter);
    mf.addTrack(seq);

    dest.deleteFile();
    juce::FileOutputStream os(dest);
    if (!os.openedOk()) return false;

    // Type 0: one track, which is what this is. JUCE defaults to type 1,
    // where track 0 is conventionally a tempo map.
    const bool ok = mf.writeTo(os, 0);
    os.flush();
    return ok && os.getStatus().wasOk();
}

// ----------------------------------------------------------------------------
// Realtime threads
// ----------------------------------------------------------------------------
void ShowRecorder::push(double ppq, juce::uint8 status,
                        juce::uint8 d1, juce::uint8 d2) noexcept {
    if (!recording_.load(std::memory_order_relaxed)) return;

    int start1, size1, start2, size2;
    fifo_.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 < 1) {
        // Full. Drop it and say so rather than block: this can be running on
        // the audio thread.
        overflowed_.store(true, std::memory_order_relaxed);
        return;
    }

    const int idx = size1 > 0 ? start1 : start2;
    slots_[(size_t)idx] = { ppq, status, d1, d2 };
    fifo_.finishedWrite(1);
}
