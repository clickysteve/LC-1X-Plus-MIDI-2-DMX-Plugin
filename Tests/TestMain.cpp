// ============================================================================
// LC-1X+ MIDI2DMX — headless unit test runner
//
// Runs every registered juce::UnitTest and exits non-zero on any failure,
// so CI can gate on it. Keep this target buildable on all three platforms:
// it must never require a display, audio device, or MIDI hardware.
// ============================================================================
#include <JuceHeader.h>
#include "AllocCounter.h"
#include <cstdlib>
#include <thread>

// ----------------------------------------------------------------------------
// Global allocation counter, used by the realtime-safety tests to prove that
// processBlock() never hits the allocator. Replacing global operator new is
// the only portable way to observe this; the counter is only *read* inside
// an explicitly scoped measurement, so the cost elsewhere is one relaxed
// atomic increment per allocation.
// ----------------------------------------------------------------------------
namespace lc1x {
    std::atomic<int> allocCount { 0 };
    std::atomic<std::thread::id> countedThread {};
}

void* operator new(std::size_t n) {
    // Count only the thread under measurement. Other threads allocate
    // concurrently and would otherwise show up as phantom allocations on the
    // audio path — see the note in AllocCounter.h.
    if (std::this_thread::get_id()
        == lc1x::countedThread.load(std::memory_order_relaxed))
        lc1x::allocCount.fetch_add(1, std::memory_order_relaxed);

    if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void  operator delete(void* p) noexcept       { std::free(p); }
void  operator delete[](void* p) noexcept     { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept   { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    // Every UnitTest runs on this thread, so this is the thread whose
    // allocations the realtime-safety tests are measuring.
    lc1x::countedThread.store(std::this_thread::get_id());

    // Message manager + platform init (no windows are ever created).
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;

    std::cout << "\n==========================================\n"
              << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
              << " (" << failures << " failure(s))\n"
              << "==========================================\n";

    return failures == 0 ? 0 : 1;
}
