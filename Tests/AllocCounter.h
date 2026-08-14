#pragma once
#include <atomic>
#include <cstddef>
#include <thread>

// Counter incremented by the global operator new replacement in TestMain.cpp.
// See RealtimeSafetyTests in ProcessorTests.cpp for how it's used.
//
// Only allocations made on `countedThread` are counted. operator new is
// global, so without that filter the count picks up whatever JUCE's message
// thread and the plugin's own MIDI sender thread happen to be doing at the
// same moment — which made the "exactly zero allocations" assertion flaky by
// an occasional one. The measurement we care about is what the *audio* path
// does, and in these tests that path runs on the thread calling processBlock.
namespace lc1x {
    extern std::atomic<int> allocCount;
    extern std::atomic<std::thread::id> countedThread;
}
