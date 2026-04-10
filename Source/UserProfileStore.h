#pragma once

#include <juce_core/juce_core.h>

// ============================================================================
// UserProfileStore
//
// Persists user-created FixtureProfile entries to a global XML file so
// they survive across plugin instances, DAW sessions and projects.
//
// File location (macOS):
//   ~/Library/Application Support/AMFAS/LC-1X+ MIDI2DMX/user_profiles.xml
//
// User profiles live in the same std::vector returned by
// getFixtureProfiles(), appended after the built-ins. This means the
// rest of the plugin (audio thread included) keeps working against a
// single profile list and doesn't need to know whether a profile came
// from code or from disk. Mutations must be called under the
// processor's dataLock.
// ============================================================================
namespace UserProfileStore
{
    /// Path to the on-disk XML file (creates parent dirs on demand).
    juce::File getProfilesFile();

    /// Load persisted user profiles from disk and append them to the
    /// global profile list. Safe to call multiple times — subsequent
    /// calls after the first are no-ops.
    void loadOnce();

    /// Write all user profiles currently in the global list back to
    /// disk. Built-ins are skipped.
    void save();
}
