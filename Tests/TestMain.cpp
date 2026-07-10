// ============================================================================
// LC-1X+ MIDI2DMX — headless unit test runner
//
// Runs every registered juce::UnitTest and exits non-zero on any failure,
// so CI can gate on it. Keep this target buildable on all three platforms:
// it must never require a display, audio device, or MIDI hardware.
// ============================================================================
#include <JuceHeader.h>

int main()
{
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
