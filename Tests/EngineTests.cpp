// ============================================================================
// Engine tests — Pattern grid, PatternBank, fixture profiles, DMX mapping.
// These exercise the pure data structures with no processor involved.
// ============================================================================
#include <JuceHeader.h>
#include "PatternData.h"
#include "FixtureProfile.h"
#include "SongMode.h"

// ----------------------------------------------------------------------------
class PatternGridTests : public juce::UnitTest {
public:
    PatternGridTests() : UnitTest("Pattern grid", "engine") {}

    void runTest() override {
        beginTest("construction and bounds");
        {
            Pattern p("t", 16, 8, 4);
            expectEquals(p.numSteps, 16);
            expectEquals(p.numSegments, 8);
            expect(p.getColor(0, 0) == RGBColor{});
            expect(p.getColor(-1, 0) == RGBColor{});      // OOB reads are safe
            expect(p.getColor(16, 0) == RGBColor{});
            p.setColor(99, 99, {1, 2, 3});                // OOB writes are no-ops
            expect(p.getColor(15, 7) == RGBColor{});
        }

        beginTest("setSteps grows and shrinks, preserving content");
        {
            Pattern p("t", 4, 2, 4);
            p.setColor(3, 1, {10, 20, 30});
            p.setSteps(8);
            expectEquals(p.numSteps, 8);
            expect(p.getColor(3, 1) == RGBColor{10, 20, 30});
            expect(p.getColor(7, 1) == RGBColor{});       // new steps are black
            p.setSteps(2);
            expectEquals(p.numSteps, 2);
            p.setSteps(0);                                // clamps to 1
            expectEquals(p.numSteps, 1);
            p.setSteps(1000);                             // clamps to 64
            expectEquals(p.numSteps, 64);
        }

        beginTest("fill operations");
        {
            Pattern p("t", 4, 3, 4);
            p.fillAll({5, 6, 7});
            expect(p.getColor(3, 2) == RGBColor{5, 6, 7});
            p.fillStep(1, {9, 9, 9});
            expect(p.getColor(1, 0) == RGBColor{9, 9, 9});
            expect(p.getColor(2, 0) == RGBColor{5, 6, 7});
            p.fillSegment(2, {1, 1, 1});
            expect(p.getColor(0, 2) == RGBColor{1, 1, 1});
            expect(p.getColor(0, 1) == RGBColor{5, 6, 7});
        }

        beginTest("shift left/right round-trips; mirror is an involution");
        {
            Pattern p("t", 4, 1, 4);
            for (int s = 0; s < 4; ++s) p.setColor(s, 0, {(uint8_t)(s + 1), 0, 0});
            p.shiftLeft();
            expect(p.getColor(0, 0) == RGBColor{2, 0, 0});
            expect(p.getColor(3, 0) == RGBColor{1, 0, 0});   // wrapped
            p.shiftRight();
            expect(p.getColor(0, 0) == RGBColor{1, 0, 0});   // back to start
            p.mirror();
            expect(p.getColor(0, 0) == RGBColor{4, 0, 0});
            p.mirror();
            expect(p.getColor(0, 0) == RGBColor{1, 0, 0});
        }

        beginTest("copy/paste range clamps and preserves");
        {
            Pattern p("t", 8, 2, 4);
            p.setColor(2, 0, {50, 0, 0});
            p.setColor(3, 0, {60, 0, 0});
            auto range = p.copyRange(2, 3);
            expectEquals((int)range.size(), 2);
            p.pasteRange(6, range);
            expect(p.getColor(6, 0) == RGBColor{50, 0, 0});
            expect(p.getColor(7, 0) == RGBColor{60, 0, 0});
            p.pasteRange(7, range);                         // second row clips off the end
            expect(p.getColor(7, 0) == RGBColor{50, 0, 0});
        }

        beginTest("generators stay in bounds and hit expected cells");
        {
            auto chase = Pattern::chase(16, 8, {255, 0, 0});
            for (int s = 0; s < 16; ++s)
                expect(chase.getColor(s, s % 8) == RGBColor{255, 0, 0});

            auto strobe = Pattern::strobe(8, 4, {0, 255, 0});
            expect(strobe.getColor(0, 0) == RGBColor{0, 255, 0});
            expect(strobe.getColor(1, 0) == RGBColor{});

            auto alt = Pattern::alternating(4, 4, {1, 0, 0}, {0, 1, 0});
            expect(alt.getColor(0, 0) == RGBColor{1, 0, 0});
            expect(alt.getColor(0, 1) == RGBColor{0, 1, 0});
        }

        beginTest("PatternBank select/delete keep a valid current pattern");
        {
            PatternBank bank;
            expectEquals((int)bank.patterns.size(), 1);
            bank.addPattern(8);
            bank.addPattern(8);
            bank.select(2);
            expectEquals(bank.currentIndex, 2);
            bank.select(99);                                // invalid → unchanged
            expectEquals(bank.currentIndex, 2);
            bank.deletePattern(2);
            expectEquals(bank.currentIndex, 1);             // clamped
            expect(bank.current() != nullptr);
            bank.deletePattern(0);
            bank.deletePattern(0);                          // last one is protected
            expectEquals((int)bank.patterns.size(), 1);
        }
    }
};
static PatternGridTests patternGridTests;

// ----------------------------------------------------------------------------
class FixtureMappingTests : public juce::UnitTest {
public:
    FixtureMappingTests() : UnitTest("Fixture DMX mapping", "engine") {}

    void runTest() override {
        beginTest("rgb layout maps segments to consecutive channel triples");
        {
            FixtureConfig fix("t", 2, 0, 0);                // profile 0 = Giga 8 Bar (rgb)
            auto pairs = fix.mapColorsToDmx({{10, 20, 30}, {40, 50, 60}});
            expectEquals((int)pairs.size(), 6);
            expect(pairs[0] == std::make_pair(0, 10));
            expect(pairs[1] == std::make_pair(1, 20));
            expect(pairs[2] == std::make_pair(2, 30));
            expect(pairs[3] == std::make_pair(3, 40));
            expect(pairs[5] == std::make_pair(5, 60));
        }

        beginTest("par can (drgb, dimAlwaysMax) locks dim channel to 255");
        {
            FixtureConfig fix("t", 1, 0, 1);                // profile 1 = 76W Par Can
            auto pairs = fix.mapColorsToDmx({{100, 0, 0}});
            expectEquals((int)pairs.size(), 4);
            expect(pairs[0] == std::make_pair(0, 255));     // dim hardcoded max
            expect(pairs[1] == std::make_pair(1, 100));
            // Even at black the dim channel stays up (colour does the dimming)
            auto black = fix.mapColorsToDmx({{0, 0, 0}});
            expect(black[0] == std::make_pair(0, 255));
        }

        beginTest("dmxFootprint honours totalDmxChannels for rig spacing");
        {
            FixtureConfig par("t", 1, 0, 1);
            expectEquals(par.dmxFootprint(), 7);            // 7ch mode, only 4 driven
            FixtureConfig bar("t", 8, 0, 0);
            expectEquals(bar.dmxFootprint(), 24);           // 8 segs × 3ch
        }

        beginTest("profile() clamps a dangling profileIndex");
        {
            FixtureConfig fix("t", 8, 0, 9999);
            expect(&fix.profile() == &getFixtureProfiles()[0]);
        }
    }
};
static FixtureMappingTests fixtureMappingTests;

// ----------------------------------------------------------------------------
class SongPlayerTests : public juce::UnitTest {
public:
    SongPlayerTests() : UnitTest("Song player", "engine") {}

    void runTest() override {
        beginTest("advance walks blocks with repeats and loops");
        {
            Song song;
            song.addBlock(0, 2);
            song.addBlock(3, 1);
            song.loop = true;

            SongPlayer sp;
            expectEquals(sp.getCurrentPatternIndex(song), 0);
            expect(sp.advanceBlock(song));                  // repeat 2 of block 0
            expectEquals(sp.getCurrentPatternIndex(song), 0);
            expect(sp.advanceBlock(song));                  // → block 1
            expectEquals(sp.getCurrentPatternIndex(song), 3);
            expect(sp.advanceBlock(song));                  // wraps (loop)
            expectEquals(sp.getCurrentPatternIndex(song), 0);
        }

        beginTest("non-looping song reports completion");
        {
            Song song;
            song.addBlock(1, 1);
            song.loop = false;
            SongPlayer sp;
            expect(!sp.advanceBlock(song));
        }

        beginTest("empty song never advances");
        {
            Song song;
            song.loop = true;
            SongPlayer sp;
            expect(!sp.advanceBlock(song));
        }
    }
};
static SongPlayerTests songPlayerTests;
