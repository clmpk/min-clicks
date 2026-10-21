#pragma once
#include "board.hpp"
#include <array>
#include <chrono>
#include <functional>

namespace minclicks {
// A legal, incomplete play for one representative frontier state. Chord
// choices can change between layers; this is not the incumbent upper bound.
struct PartialSolution {
    std::vector<int> chords; // Physical board cells, including finished pieces.
    std::vector<Action> actions; // Flags and seeded chord chains; no cleanup opens.
};
struct Progress {
    const char *phase;
    int completed, total;
    size_t states, peak;
    int best;
    double elapsed;
    size_t transitions;
    const PartialSolution *preview = nullptr; // Valid only during the callback.
};
struct ExactOptions {
    double timeLimit = 0; // Zero means unlimited.
    size_t maxStates = 0;
    bool progress = true; // Human-readable stderr progress.
    bool includeActions = true; // Disable witness construction for count-only callers.
    bool visualize = false; // Opt-in per-candidate progress and partial witnesses.
    std::function<void(const Progress &)> onProgress;
    void report(const char *phase, int completed, int total, size_t states,
                size_t peak, int best, double elapsed, size_t transitions,
                bool force = false, const PartialSolution *preview = nullptr) const {
        if (!onProgress)
            return;
        auto now = std::chrono::steady_clock::now();
        if (!force && now - lastProgress < std::chrono::milliseconds(200))
            return;
        lastProgress = now;
        onProgress({phase, completed, total, states, peak, best, elapsed,
                    transitions, preview});
    }

  private:
    mutable std::chrono::steady_clock::time_point lastProgress{};
};
struct ExactResult {
    bool exact = false;
    int clicks = 0;
    std::array<int, 4>
        breakdown{}; // Flags, chords, component seeds, direct targets.
    std::vector<Action> actions;
    size_t peak = 0, transitions = 0, dominated = 0;
    int completed = 0, candidates = 0, removed = 0, maxConn = 0, maxFactors = 0,
        attempts = 1;
    double elapsed = 0; // Complete solveExact wall time, including preparation and witnesses.
    std::string order, engine, reason;
};
ExactResult solveExact(const Board &board, const ExactOptions &options = {});

enum class OptimalTile : uint8_t { Unknown = 0, Optional = 1, Forced = 2, Forbidden = 3 };
struct OptimalAnalysis {
    ExactResult solution;
    // Physical-cell arrays: chords classify nonzero numbers; flags classify mines.
    // Other cells stay Unknown and are excluded from total/settled.
    std::vector<OptimalTile> chords, flags;
    int settled = 0, total = 0, checks = 0, checkingCell = -1;
    bool complete = false;
    double elapsed = 0;
    std::string reason;
};
struct AnalysisOptions {
    ExactOptions solver;
    // Snapshot is borrowed for the duration of the callback. Unknown is never
    // treated as forbidden, including when a resource limit interrupts a check.
    std::function<void(const OptimalAnalysis &)> onProgress;
};
OptimalAnalysis analyzeOptimal(const Board &board, const AnalysisOptions &options = {},
                               // When supplied, an exact solveExact result for this board.
                               const ExactResult *knownSolution = nullptr);
} // namespace minclicks
