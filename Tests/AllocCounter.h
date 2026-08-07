#pragma once
#include <atomic>
#include <cstddef>

// Counter incremented by the global operator new replacement in TestMain.cpp.
// See RealtimeSafetyTests in ProcessorTests.cpp for how it's used.
namespace lc1x { extern std::atomic<int> allocCount; }
