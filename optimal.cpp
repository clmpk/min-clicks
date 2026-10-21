// Command-line and WebAssembly adapter. Build with python build.py.
#include "src/board.hpp"
#include "src/exact.hpp"
#include "src/zini.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#ifndef __EMSCRIPTEN__
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(void, browser_progress,
      (const char *phase, int completed, int total, double states, double peak,
       int best, double elapsed, double transitions, const int *actions, int actionCount),
      {
          const callback = Module['onSolverProgress'];
          if (typeof callback === 'function')
              callback({
                  phase : UTF8ToString(phase),
                  completed,
                  total,
                  states,
                  peak,
                  best,
                  elapsed,
                  transitions,
                  heapBytes : HEAPU8.buffer.byteLength,
                  // Copy before C++ releases this callback-local buffer. Pairs
                  // are [ASCII action kind, physical cell], independent of ABI.
                  preview : actions ? HEAP32.slice(actions >>> 2, (actions >>> 2) + actionCount * 2) : null
              });
      });
EM_JS(void, browser_analysis,
      (const uint8_t *chords, const uint8_t *flags, int count, int settled,
       int total, int checks, int checkingCell, int clicks, double elapsed, int complete),
      {
          const callback = Module['onAnalysisProgress'];
          chords >>>= 0; flags >>>= 0;
          if (typeof callback === 'function') callback({
              chords: HEAPU8.slice(chords, chords + count),
              flags: HEAPU8.slice(flags, flags + count),
              settled, total, checks, checkingCell, clicks, elapsed,
              complete: Boolean(complete)
          });
      });
// clang-format on
#endif
using namespace std;
using namespace minclicks;
#ifdef __EMSCRIPTEN__
// Short batches let generation yield to worker cancellation messages.
extern "C" EMSCRIPTEN_KEEPALIVE const char *web_generate(
    int width, int height, int mines, int minBV, int maxBV,
    uint32_t seedLow, uint32_t seedHigh, double minEff) {
    static string response;
    if (width < 1 || width > 99 || height < 1 || height > 99 ||
        mines < 0 || mines > width * height || minBV < 0 ||
        maxBV > 9801 || minBV > maxBV)
        return "{\"error\":\"Invalid generation parameters.\"}";
    try {
        uint64_t seed = (uint64_t(seedHigh) << 32) | seedLow;
        auto began = chrono::steady_clock::now();
        int attempts = 0;
        double maxEff = -1; // percent; -1 when no board reached the 8-way stage
        auto stats = [&]() {
            string s = "{\"attempts\":" + to_string(attempts);
            if (maxEff >= 0) s += ",\"maxEff\":" + to_string(maxEff);
            return s;
        };
        do {
            auto board = randomBoard64(width, height, mines, seed++);
            ++attempts;
            int bv = board.bv();
            if (bv < minBV || bv > maxBV) continue;
            if (minEff > 0) {
                int clicks = eightWayZiniClicks(board);
                double eff = clicks > 0 ? 100.0 * bv / clicks : 0;
                maxEff = max(maxEff, eff);
                if (eff < minEff - 1e-9) continue;
            }
            response = stats() + ",\"url\":\"" + board.url() + "\"}";
            return response.c_str();
        } while (chrono::steady_clock::now() - began < chrono::milliseconds(25));
        response = stats() + "}";
        return response.c_str();
    } catch (const exception &) {
        return "{\"error\":\"Board generation failed.\"}";
    }
}
#endif
static unsigned long long natural(const string &s) {
    if (s.empty() || s.find_first_not_of("0123456789") != string::npos)
        throw runtime_error("Expected nonnegative integer: " + s);
    return stoull(s);
}
// Bulk generation uses a single RNG and a partial forward shuffle.
// Keep the existing --random generators and their seeded sequences unchanged.
static Board bulkBoard(int w, int h, int mines, mt19937_64 &rng) {
    vector<int> cells(w * h), mine(w * h);
    iota(cells.begin(), cells.end(), 0);
    for (int i = 0; i < mines; ++i) {
        uint64_t bound = uint64_t(cells.size() - i);
        uint64_t threshold = (uint64_t(0) - bound) % bound, value;
        do { value = rng(); } while (value < threshold);
        swap(cells[i], cells[i + value % bound]);
        mine[cells[i]] = 1;
    }
    return Board(w, h, std::move(mine));
}
static string bulkRow(const Board &board, int clicks) {
    string url = board.url();
    return '"' + url.substr(url.find('?') + 1) + "\"," +
           to_string(board.bv()) + ',' + to_string(clicks) + '\n';
}
#ifndef __EMSCRIPTEN__
static int bulkSolveParallel(uint64_t count, int w, int h, int mines,
                             mt19937_64 &rng, const ExactOptions &options,
                             unsigned threadCount) {
    struct Slot {
        bool ready = false, exact = false;
        string row, reason;
        exception_ptr error;
    };
    // Bound both queued work and completed results when an early board is slow.
    vector<Slot> slots(size_t(min(count, uint64_t(threadCount) * 2)));
    mutex guard;
    condition_variable changed;
    uint64_t next = 0, written = 0;
    bool stopping = false;
    vector<jthread> workers;
    auto finish = [&] {
        {
            lock_guard lock(guard);
            stopping = true;
        }
        changed.notify_all();
        for (auto &worker : workers) if (worker.joinable()) worker.join();
    };
    try {
        for (unsigned worker = 0; worker < threadCount; ++worker)
            workers.emplace_back([&, localOptions = options] {
                for (;;) {
                    uint64_t index;
                    Slot completed;
                    {
                        unique_lock lock(guard);
                        changed.wait(lock, [&] {
                            return stopping || next == count || next - written < slots.size();
                        });
                        if (stopping || next == count) return;
                        index = next++;
                        try {
                            // Generate in index order from the one shared RNG.
                            // Each worker owns its board, options and solver state.
                            auto board = bulkBoard(w, h, mines, rng);
                            lock.unlock();
                            auto result = solveExact(board, localOptions);
                            completed.exact = result.exact;
                            if (result.exact) completed.row = bulkRow(board, result.clicks);
                            else completed.reason = std::move(result.reason);
                        } catch (...) {
                            completed.error = current_exception();
                        }
                    }
                    {
                        lock_guard lock(guard);
                        slots[size_t(index % slots.size())] = std::move(completed);
                        slots[size_t(index % slots.size())].ready = true;
                    }
                    changed.notify_all();
                }
            });
        for (uint64_t index = 0; index < count; ++index) {
            Slot completed;
            {
                unique_lock lock(guard);
                auto &slot = slots[size_t(index % slots.size())];
                changed.wait(lock, [&] { return slot.ready; });
                completed = std::move(slot);
                slot.ready = false;
                written = index + 1;
            }
            changed.notify_all();
            if (completed.error) rethrow_exception(completed.error);
            if (!completed.exact) {
                finish();
                cerr << "Bulk board " << index + 1 << '/' << count
                     << ": resource limit: " << completed.reason << '\n';
                return 2;
            }
            cout << completed.row;
        }
    } catch (...) {
        // Also handles partial thread creation: wake and join every started worker.
        finish();
        throw;
    }
    finish();
    return 0;
}
#endif
static int bulkSolve(uint64_t count, int w, int h, int mines, uint64_t seed,
                     ExactOptions options, unsigned requestedThreads) {
    bool log = options.progress;
    options.progress = false;
    options.includeActions = false;
    unsigned threadCount = requestedThreads;
#ifdef __EMSCRIPTEN__
    if (threadCount > 1) throw runtime_error("This build does not support bulk worker threads");
    threadCount = 1;
#else
    if (!threadCount) threadCount = max(1u, thread::hardware_concurrency());
#endif
    threadCount = unsigned(min(uint64_t(threadCount), count));
    mt19937_64 rng(seed);
    if (log)
        cerr << "Bulk RNG: std::mt19937_64; seed: " << seed
             << "\nBulk workers: " << threadCount << '\n';
    cout << "board,3bv,optimal_clicks\n";
    auto started = chrono::steady_clock::now();
#ifndef __EMSCRIPTEN__
    if (threadCount > 1) {
        int status = bulkSolveParallel(count, w, h, mines, rng, options, threadCount);
        if (status) return status;
    } else
#endif
    for (uint64_t i = 0; i < count; ++i) {
        auto board = bulkBoard(w, h, mines, rng);
        auto result = solveExact(board, options);
        if (!result.exact) {
            cerr << "Bulk board " << i + 1 << '/' << count
                 << ": resource limit: " << result.reason << '\n';
            return 2;
        }
        cout << bulkRow(board, result.clicks);
    }
    if (log)
        cerr << "Bulk solve time: " << fixed << setprecision(3)
             << chrono::duration<double>(chrono::steady_clock::now() - started).count()
             << " seconds\n";
    return 0;
}
static void printAnalysis(const OptimalAnalysis &a) {
    cout << "{\"complete\":" << (a.complete ? "true" : "false")
         << ",\"clicks\":" << (a.solution.exact ? to_string(a.solution.clicks) : "null")
         << ",\"settled\":" << a.settled << ",\"total\":" << a.total
         << ",\"checks\":" << a.checks << ",\"seconds\":" << a.elapsed
         << ",\"reason\":" << quoted(a.reason);
    auto emit = [](const vector<OptimalTile> &tiles) {
        cout << '[';
        for (size_t i = 0; i < tiles.size(); ++i) {
            if (i) cout << ',';
            cout << int(tiles[i]);
        }
        cout << ']';
    };
    cout << ",\"chords\":"; emit(a.chords);
    cout << ",\"flags\":"; emit(a.flags);
    cout << '}';
}
int main(int argc, char **argv) {
    try {
        ExactOptions options;
        string input, algorithm = "optimal";
        bool random = false, all = false, json = false, witness = false,
             random64 = false, analyze = false, bulk = false, seedSpecified = false;
        int w = 0, h = 0, mines = 0;
        uint64_t seed = 0;
        uint64_t bulkCount = 0;
        unsigned bulkThreads = 0;
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            auto arg = [&]() {
                if (++i >= argc) throw runtime_error("Missing value for " + a);
                return string(argv[i]);
            };
            auto integer = [&]() {
                auto v = natural(arg());
                if (v > numeric_limits<int>::max())
                    throw runtime_error("Integer out of range");
                return int(v);
            };
            if (a == "--help") {
                cout << "Usage: optimal URL [options]\n"
                     << "       optimal --random WIDTH HEIGHT MINES SEED [--64b] "
                        "[options]\n"
                     << "       optimal --bulk COUNT WIDTH HEIGHT MINES [--seed UINT64] [--threads N] [--quiet]\n"
                     << "  --bulk      Parallel exact solves; board,3bv,optimal_clicks CSV\n"
                     << "  --seed      Bulk RNG seed (random if omitted)\n"
                     << "  --threads   Bulk workers (default hardware concurrency; 1 for serial)\n"
                     << "  --64b       Use mt19937_64 and a uint64 seed for random "
                        "boards\n"
                     << "  --algorithm optimal|8way|lzini|hzini (default optimal)\n"
                     << "  --all       All four algorithms, one JSON object, no "
                        "progress\n"
                     << "  --json      Machine-readable output for one algorithm\n"
                     << "  --witness   Include actions (F flag, O open, C chord)\n"
                     << "  --quiet     Suppress stderr progress\n"
                     << "  --visualize Per-candidate partial solutions in progress callbacks\n"
                     << "  --analyze   Classify forced/forbidden optimal chords and flags\n"
                     << "  --time-limit SECONDS / --max-states N (default 0: "
                        "unlimited)\n"
                     << "Exit: 0 success, 2 exact search limited, 1 invalid "
                        "input/error.\n";
                return 0;
            } else if (a == "--bulk") {
                bulk = true;
                bulkCount = natural(arg());
                w = integer();
                h = integer();
                mines = integer();
            } else if (a == "--seed") {
                seed = natural(arg());
                seedSpecified = true;
            } else if (a == "--threads") {
                bulkThreads = unsigned(integer());
                if (!bulkThreads) throw runtime_error("--threads must be positive");
            } else if (a == "--random") {
                random = true;
                w = integer();
                h = integer();
                mines = integer();
                seed = natural(arg());
            } else if (a == "--64b") random64 = true;
            else if (a == "--algorithm") algorithm = arg();
            else if (a == "--all") all = true;
            else if (a == "--json") json = true;
            else if (a == "--quiet") options.progress = false;
            else if (a == "--witness") witness = true;
            else if (a == "--visualize") options.visualize = true;
            else if (a == "--analyze") analyze = true;
            else if (a == "--max-states") {
                auto n = natural(arg());
                if (n > numeric_limits<size_t>::max())
                    throw runtime_error("State limit out of range");
                options.maxStates = size_t(n);
            } else if (a == "--time-limit") {
                string v = arg();
                size_t used;
                options.timeLimit = stod(v, &used);
                if (used != v.size() || !isfinite(options.timeLimit) ||
                    options.timeLimit < 0)
                    throw runtime_error("Invalid time limit");
            } else if (a.starts_with("--")) throw runtime_error("Unknown option: " + a);
            else if (input.empty()) input = a;
            else throw runtime_error("Unexpected argument: " + a);
        }
        if (algorithm != "optimal" && algorithm != "8way" && algorithm != "lzini" &&
            algorithm != "hzini")
            throw runtime_error("Unknown algorithm: " + algorithm);
        if (all && algorithm != "optimal")
            throw runtime_error("Choose --all or --algorithm");
        if (analyze && (all || algorithm != "optimal"))
            throw runtime_error("--analyze requires the optimal algorithm without --all");
        if (seedSpecified && !bulk)
            throw runtime_error("--seed requires --bulk; --random takes its seed positionally");
        if (bulkThreads && !bulk) throw runtime_error("--threads requires --bulk");
        if (bulk) {
            if (random || !input.empty() || all || json || witness || analyze ||
                options.visualize || random64 || algorithm != "optimal")
                throw runtime_error("--bulk requires exact CSV output without other input/output modes");
            if (!bulkCount || w < 1 || h < 1 || w > 99 || h > 99 || mines > w * h)
                throw runtime_error("Invalid bulk count/dimensions/mine count");
            if (!seedSpecified) {
                random_device rd;
                seed = (uint64_t(rd()) << 32) ^ uint64_t(rd());
            }
            return bulkSolve(bulkCount, w, h, mines, seed, options, bulkThreads);
        }
        if (random == !input.empty())
            throw runtime_error("Supply either a URL or --random; see --help");
        if (random64 && !random) throw runtime_error("--64b requires --random");
        if (random && !random64 && seed > UINT32_MAX)
            throw runtime_error("Seed out of range");
        if (!input.empty() && input.starts_with("b=")) input = "?" + input;
        Board b = random ? (random64 ? randomBoard64(w, h, mines, seed)
                                     : randomBoard(w, h, mines, uint32_t(seed)))
                         : decodeBoard(input);
        json = json || all;
        if (json) options.progress = false;
#ifdef __EMSCRIPTEN__
        if (!json || analyze)
            options.onProgress = [](const Progress &p) {
                vector<int> actions;
                if (p.preview) {
                    actions.reserve(p.preview->actions.size() * 2 + 1);
                    for (const auto &action : p.preview->actions) {
                        actions.push_back(int(action.kind));
                        actions.push_back(action.cell);
                    }
                    // Keep a non-null pointer for an intentionally empty preview.
                    if (actions.empty()) actions.push_back(0);
                }
                browser_progress(p.phase, p.completed, p.total, double(p.states),
                                 double(p.peak), p.best, p.elapsed,
                                 double(p.transitions), p.preview ? actions.data() : nullptr,
                                 p.preview ? int(p.preview->actions.size()) : 0);
            };
#endif
        ExactResult exact;
        OptimalAnalysis analysis;
        HeuristicResult heuristic;
        LegacyZiniResults legacy;
        HeuristicResult eight;
        const ExactResult *known = nullptr;
#ifdef __EMSCRIPTEN__
        // callMain is reused within one worker. A previous normal solve can
        // supply the analysis baseline without proving the optimum twice.
        static string cachedBoard;
        static ExactResult cachedExact;
        if (cachedBoard == b.url() && cachedExact.exact) known = &cachedExact;
#endif
        if (analyze) {
            AnalysisOptions ao; ao.solver = options; ao.solver.visualize = false;
#ifdef __EMSCRIPTEN__
            ao.onProgress = [](const OptimalAnalysis &a) {
                browser_analysis(reinterpret_cast<const uint8_t *>(a.chords.data()),
                                 reinterpret_cast<const uint8_t *>(a.flags.data()),
                                 int(a.chords.size()), a.settled, a.total, a.checks,
                                 a.checkingCell, a.solution.exact ? a.solution.clicks : -1,
                                 a.elapsed, a.complete);
            };
#endif
            analysis = analyzeOptimal(b, ao, known);
            exact = analysis.solution;
        } else if (all || algorithm == "optimal") exact = solveExact(b, options);
#ifdef __EMSCRIPTEN__
        if ((all || algorithm == "optimal") && exact.exact) {
            cachedBoard = b.url(); cachedExact = exact;
        }
#endif
        if (all) {
            legacy = legacyZini(b);
            if (witness) eight = eightWayZini(b);
            else eight.clicks = eightWayZiniClicks(b);
        } else if (algorithm == "hzini") heuristic = hzini(b);
        else if (algorithm == "lzini") heuristic = lzini(b);
        else if (algorithm == "8way") {
            if (witness) heuristic = eightWayZini(b);
            else heuristic.clicks = eightWayZiniClicks(b);
        }
        bool isExact = all || algorithm == "optimal";
        const auto &actions = isExact ? exact.actions : heuristic.actions;
        if (json) {
            cout << setprecision(9) << "{\"url\":\"" << b.url()
                 << "\",\"width\":" << b.w << ",\"height\":" << b.h
                 << ",\"mines\":" << b.mineCount() << ",\"three_bv\":" << b.bv()
                 << ",\"status\":\""
                 << (isExact ? (exact.exact ? "optimal" : "resource limit")
                             : "heuristic")
                 << "\"";
            if (isExact)
                cout << ",\"minclicks\":"
                     << (exact.exact ? to_string(exact.clicks) : "null")
                     << ",\"upper_bound\":" << exact.clicks
                     << ",\"peak_states\":" << exact.peak
                     << ",\"solve_seconds\":" << exact.elapsed;
            else
                cout << ",\"algorithm\":\"" << algorithm
                     << "\",\"clicks\":" << heuristic.clicks;
            if (all)
                cout << ",\"8way\":" << eight.clicks
                     << ",\"lzini\":" << legacy.lzini.clicks
                     << ",\"hzini\":" << legacy.hzini.clicks;
            if (witness) {
                auto emit = [&](const vector<Action> &a) {
                    cout << '[';
                    bool comma = false;
                    for (auto v : a) {
                        if (comma) cout << ',';
                        comma = true;
                        cout << "[\"" << v.kind << "\"," << v.cell % b.w << ','
                             << v.cell / b.w << ']';
                    }
                    cout << ']';
                };
                cout << ",\"actions\":";
                if (all) {
                    cout << "{\"optimal\":";
                    emit(actions);
                    cout << ",\"8way\":";
                    emit(eight.actions);
                    cout << ",\"lzini\":";
                    emit(legacy.lzini.actions);
                    cout << ",\"hzini\":";
                    emit(legacy.hzini.actions);
                    cout << '}';
                } else emit(actions);
            }
            if (analyze) { cout << ",\"analysis\":"; printAnalysis(analysis); }
            cout << "}\n";
        } else {
            cout << "Board: " << b.w << 'x' << b.h << ", " << b.mineCount()
                 << " mines\nURL: " << b.url() << "\n3BV: " << b.bv() << '\n';
            if (isExact) {
                const auto &r = exact;
                const auto &c = r.breakdown;
                cout << "Candidates: " << r.candidates << " (removed " << r.removed
                     << ")\nDP: " << r.order << "\nEngine: " << r.engine << '\n'
                     << "Status: "
                     << (r.exact ? "optimal" : "resource limit: " + r.reason) << '\n'
                     << (r.exact ? "Optimal clicks: " : "Upper bound clicks: ")
                     << r.clicks << '\n'
                     << "Breakdown: " << c[0] << " flags + " << c[1] << " chords + "
                     << c[2] << " chains + " << c[3] << " remaining 3BV\n"
                     << "Peak states: " << r.peak << "\nTransitions: " << r.transitions
                     << "\nCompleted candidates: " << r.completed << '/' << r.candidates
                     << "\nMax connectivity: " << r.maxConn << ", max factors: " << r.maxFactors
                     << "\nFinal order: " << r.order << "\nSolve time: " << r.elapsed
                     << " seconds\nDominated states: " << r.dominated << '\n';
            } else
                cout << "Algorithm: " << algorithm
                     << "\nStatus: heuristic\nUpper bound clicks: " << heuristic.clicks
                     << '\n';
            if (witness)
                for (auto a : actions)
                    cout << "Action: " << a.kind << ' ' << a.cell % b.w << ' '
                         << a.cell / b.w << '\n';
            if (analyze) { cout << "Analysis: "; printAnalysis(analysis); cout << '\n'; }
        }
        return (isExact && !exact.exact) || (analyze && !analysis.complete) ? 2 : 0;
    } catch (const exception &e) {
        cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
