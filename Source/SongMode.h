#pragma once
#include <vector>
#include <string>

// ============================================================================
// Song Block — one entry in a song chain
// ============================================================================
struct SongBlock {
    int patternIndex = 0;
    int repeats      = 1;
    std::string label;

    SongBlock(int pat = 0, int rep = 1) : patternIndex(pat), repeats(rep) {}
};

// ============================================================================
// Song — ordered chain of blocks
// ============================================================================
class Song {
public:
    std::vector<SongBlock> blocks;
    bool loop = true;

    void addBlock(int patternIndex, int repeats = 1) {
        blocks.emplace_back(patternIndex, repeats);
    }

    void removeBlock(int idx) {
        if (idx >= 0 && idx < (int)blocks.size())
            blocks.erase(blocks.begin() + idx);
    }

    void duplicateBlock(int idx) {
        if (idx >= 0 && idx < (int)blocks.size())
            blocks.insert(blocks.begin() + idx + 1, blocks[idx]);
    }

    int totalRepeats() const {
        int t = 0;
        for (auto& b : blocks) t += b.repeats;
        return t;
    }
};

// ============================================================================
// Song Player — tracks playback position within a song
// ============================================================================
class SongPlayer {
public:
    int currentBlock  = 0;
    int currentRepeat = 0;

    void reset() { currentBlock = 0; currentRepeat = 0; }

    /// Advance to next repeat / block.  Returns false when song is finished (non‑looping).
    bool advanceBlock(const Song& song) {
        if (song.blocks.empty()) return false;

        currentRepeat++;
        if (currentRepeat >= song.blocks[currentBlock].repeats) {
            currentRepeat = 0;
            currentBlock++;
            if (currentBlock >= (int)song.blocks.size()) {
                if (song.loop) { currentBlock = 0; return true; }
                return false;
            }
        }
        return true;
    }

    int getCurrentPatternIndex(const Song& song) const {
        if (currentBlock >= 0 && currentBlock < (int)song.blocks.size())
            return song.blocks[currentBlock].patternIndex;
        return 0;
    }

    void jumpToBlock(int idx) {
        currentBlock = idx;
        currentRepeat = 0;
    }
};
