#pragma once
#include "PatternData.h"
#include <string>
#include <vector>
#include <map>
#include <utility>

// ============================================================================
// Fixture Profile — describes a DMX fixture's channel layout
// ============================================================================
struct FixtureProfile {
    std::string name;
    std::vector<char> channelLayout;      // 'r','g','b','w','d' (master dim)
    std::map<int, int> extraChannels;     // relative offset → default value
    bool hasMasterDim       = false;
    int  channelsPerSegment = 3;
    int  totalDmxChannels   = 0;          // 0 = auto (segments × channelsPerSegment)
    std::string description;
    int  fixedSegments      = 0;          // 0 = user-set; >0 = force this count
    bool dimAlwaysMax       = false;      // if true, 'd' channels are hardcoded
                                          // to 255 so colour dimming happens via
                                          // the RGB channels alone (par can mode)
    bool isBuiltin          = true;       // false for user-created profiles
                                          // (shown in the selector, persisted to
                                          // ~/Library/Application Support)
};

// The fixture profile list is intentionally a single mutable static
// std::vector — user-created profiles are appended at the end by
// UserProfileStore::loadOnce() and addUserProfile(). Capacity is
// reserved up front so that push_back never reallocates, which means
// references returned by FixtureConfig::profile() remain valid for
// the lifetime of the audio thread's access window. All mutations
// (add/remove/reload) are serialised via DMXControllerProcessor's
// dataLock so the audio thread never races against a shifting vector.
inline std::vector<FixtureProfile>& getFixtureProfiles() {
    static std::vector<FixtureProfile> profiles = [] {
        std::vector<FixtureProfile> p;
        p.reserve(128);
        p.push_back({
            "Giga 8 Bar (24ch)",
            {'r', 'g', 'b'}, {},
            false, 3, 0,
            "Giga 8 Bar: 8 RGB segments, 24 channels",
            0,
            false,
            true
        });
        p.push_back({
            "76W RGB Par Can (7ch)",
            {'d', 'r', 'g', 'b'},       // ch1=dim (hardcoded max), ch2-4=RGB
            {},                         // ch5-7 (strobe/mode/speed) left at 0 so
                                        // the par can sits in its default
                                        // "solid colour, no effects" state.
            true, 4, 7,                 // writes 4 channels, reserves 7 for rig spacing
            "76W Par Can (7-channel mode): Dim/R/G/B + unused Strobe/Mode/Speed. "
            "The plugin drives only the first 4 channels (dim held at 100%, "
            "brightness comes from the RGB channels), but the fixture still "
            "occupies a 7-channel slot so the next fixture should start "
            "7 addresses later.",
            1,                          // single-segment fixture
            true,                       // dimAlwaysMax: lock ch1 to 255
            true
        });
        return p;
    }();
    return profiles;
}

inline int builtinProfileCount() {
    // Keep in sync with the push_back calls above.
    return 2;
}

// ============================================================================
// Fixture Config — one physical fixture on the rig
// ============================================================================
struct FixtureConfig {
    std::string name;
    int numSegments;
    int dmxStart;          // 0‑based DMX offset
    int profileIndex;
    float brightnessOffset = 1.0f;     // 0..2 per-fixture trim
    PatternBank patternBank;

    FixtureConfig(const std::string& n = "Fixture 1",
                  int segs = 8, int start = 0, int profile = 0)
        : name(n), numSegments(segs), dmxStart(start), profileIndex(profile) {}

    const FixtureProfile& profile() const {
        // Clamp to a valid index so that state files saved against older
        // profile lists (which may have referenced profiles that have
        // since been removed, e.g. the old Dimmer+RGB and RGBW entries)
        // fall back safely to the first profile instead of crashing.
        const auto& all = getFixtureProfiles();
        int idx = profileIndex;
        if (idx < 0 || idx >= (int)all.size()) idx = 0;
        return all[idx];
    }

    /// Total DMX address footprint of this fixture — i.e. how many
    /// channels the physical hardware reserves, regardless of how many
    /// the plugin actually writes to. Used for auto-spacing when adding
    /// / duplicating fixtures so the next fixture doesn't land inside
    /// the unused-but-still-allocated tail of this one (e.g. par cans
    /// in 7-channel mode where we only drive the first 4 channels).
    int dmxFootprint() const {
        const auto& prof = profile();
        const int autoSpan = numSegments * prof.channelsPerSegment;
        if (prof.totalDmxChannels > 0)
            return std::max(prof.totalDmxChannels, autoSpan);
        return autoSpan;
    }

    /// Map a vector of segment RGB colours → list of (relative DMX offset, value) pairs
    std::vector<std::pair<int,int>> mapColorsToDmx(const std::vector<RGBColor>& colors) const {
        std::vector<std::pair<int,int>> result;
        const auto& prof = profile();

        for (int seg = 0; seg < (int)colors.size(); seg++) {
            auto c = colors[seg];
            int base = seg * prof.channelsPerSegment;

            // Dimmer channel value. Two modes:
            //  - dimAlwaysMax: lock to 255 always. Used for par cans where
            //    brightness is driven entirely by the RGB channels and the
            //    dim channel sitting at 0 would switch the whole fixture
            //    off (even for non-black colours).
            //  - default: track max(R,G,B). Setting the colour to black
            //    turns the fixture off via the dim channel, and the
            //    master/brightness trim — already baked into RGB by
            //    applyTrim — also reaches the dim channel.
            const int dimValue = prof.dimAlwaysMax
                                   ? 255
                                   : std::max({c.r, c.g, c.b});

            for (int ch = 0; ch < (int)prof.channelLayout.size(); ch++) {
                int offset = base + ch;
                switch (prof.channelLayout[ch]) {
                    case 'r': result.push_back({offset, c.r}); break;
                    case 'g': result.push_back({offset, c.g}); break;
                    case 'b': result.push_back({offset, c.b}); break;
                    case 'w': result.push_back({offset, std::min({c.r, c.g, c.b})}); break;
                    case 'd': result.push_back({offset, dimValue}); break;
                }
            }
        }

        // Extra channels (strobe / mode / speed / etc) are placed at absolute
        // offsets relative to dmxStart. Applied LAST so that if a user
        // configures a single-segment fixture whose layout spans more
        // channels than `channelsPerSegment`, the extras still take effect.
        for (auto& [off, val] : prof.extraChannels)
            result.push_back({off, val});

        return result;
    }
};
