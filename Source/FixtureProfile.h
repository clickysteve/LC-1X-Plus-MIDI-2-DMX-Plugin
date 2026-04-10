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
};

inline const std::vector<FixtureProfile>& getFixtureProfiles() {
    static const std::vector<FixtureProfile> profiles = {
        {
            "Giga 8 Bar (24ch)",
            {'r', 'g', 'b'}, {},
            false, 3, 0,
            "Giga 8 Bar: 8 RGB segments, 24 channels",
            0
        },
        {
            "Dimmer+RGB (4ch)",
            {'d', 'r', 'g', 'b'}, {},
            true, 4, 0,
            "Dimmer + RGB — 4 channels per segment",
            0
        },
        {
            "RGBW (4ch per segment)",
            {'r', 'g', 'b', 'w'}, {},
            false, 4, 0,
            "RGBW — 4 channels per segment",
            0
        },
        {
            "76W RGB Par Can (7ch)",
            {'d', 'r', 'g', 'b'},
            {{4, 0}, {5, 0}, {6, 0}},   // strobe off, manual mode, speed 0
            true, 4, 7,
            "76W Par Can: Dim/R/G/B/Strobe/Mode/Speed",
            1                           // single-segment fixture
        }
    };
    return profiles;
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

    const FixtureProfile& profile() const { return getFixtureProfiles()[profileIndex]; }

    /// Map a vector of segment RGB colours → list of (relative DMX offset, value) pairs
    std::vector<std::pair<int,int>> mapColorsToDmx(const std::vector<RGBColor>& colors) const {
        std::vector<std::pair<int,int>> result;
        const auto& prof = profile();

        for (int seg = 0; seg < (int)colors.size(); seg++) {
            auto c = colors[seg];
            int base = seg * prof.channelsPerSegment;

            for (int ch = 0; ch < (int)prof.channelLayout.size(); ch++) {
                int offset = base + ch;
                switch (prof.channelLayout[ch]) {
                    case 'r': result.push_back({offset, c.r}); break;
                    case 'g': result.push_back({offset, c.g}); break;
                    case 'b': result.push_back({offset, c.b}); break;
                    case 'w': result.push_back({offset, std::min({c.r, c.g, c.b})}); break;
                    case 'd': result.push_back({offset, 255}); break;
                }
            }
        }

        for (auto& [off, val] : prof.extraChannels)
            result.push_back({off, val});

        return result;
    }
};
