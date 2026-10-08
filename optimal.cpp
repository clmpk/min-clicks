// Command-line and WebAssembly adapter. Build with python build.py.
#include "src/board.hpp"
#include "src/exact.hpp"
#include "src/zini.hpp"
#include "src/json.hpp"
#include "src/csv.hpp"
#include "optimal.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
// For JSON file output
#include <glaze/glaze.hpp> // v9.0.0 requires c++23
// For CSV file output
#include <vector>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off
EM_JS(void, browser_progress,
      (const char *phase, int completed, int total, double states, double peak,
       int best, double elapsed, double transitions),
      {
          const callback = Module['onSolverProgress'];
          if (typeof callback === 'function') {
              callback({
                  phase : UTF8ToString(phase),
                  completed,
                  total,
                  states,
                  peak,
                  best,
                  elapsed,
                  transitions,
                  heapBytes : HEAPU8.buffer.byteLength
              });
          }
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
        maxBV > 9801 || minBV > maxBV
    ) {
        return "{\"error\":\"Invalid generation parameters.\"}";
    }
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
    if (s.empty() || s.find_first_not_of("0123456789") != string::npos) {
        throw runtime_error("Expected nonnegative integer: " + s);
    }
    return stoull(s);
}

using Clock = chrono::steady_clock;

inline double seconds(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

int main(int argc, char **argv) {
    try {
        enum class Formats { DEFAULT, JSON, CSV };
        Formats output_format = Formats::DEFAULT;
        ExactOptions options;
        string input, algorithm = "optimal";
        string output_filename = "output";
        string rng_load_filename = "rng_state.txt";
        string rng_save_filename = "rng_state.txt";
        bool random = false, all = false, file = false, witness = false, random64 = false,
             rng_save = false, rng_load = false, progress = true;
        CoordType w = 0, h = 0;
        CellType mines = 0;
        int runs = 1;
        uint64_t seed = 0;
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            auto arg = [&]() {
                if (++i >= argc) {
                    throw runtime_error("Missing value for " + a);
                }
                return string(argv[i]);
            };
            auto integer = [&]() {
                auto v = natural(arg());
                if (v > numeric_limits<int>::max()) {
                    throw runtime_error("Integer out of range");
                }
                return int(v);
            };
            if (a == "--help") {
                cout << "Usage: optimal URL [options]\n"
                     << "       optimal --random WIDTH HEIGHT MINES SEED [--64b] "
                        "[options]\n"
                     << "       optimal --randomruns WIDTH HEIGHT MINES RUNS SEED [--64b] "
                        "[options]\n"
                     << "  --64b       Use mt19937_64 and a uint64 seed for random "
                        "boards\n"
                     << "  --algorithm optimal|8way|lzini|hzini (default optimal)\n"
                     << "  --all       All four algorithms, one JSON object/CSV output, no progress\n"
                        "              Outputs to in terminal.\n"
                     << "  --json      Machine-readable output for one algorithm\n"
                     << "  --json-file FILENAME\n"
                        "              Saves machine-readable output for one algorithm to 'filename.json'\n"
                     << "  --csv       Table-readable output for one algorithm\n"
                     << "  --csv-file FILENAME\n"
                        "              Saves table-readable output for one algorithm to 'filename.csv'\n"
                     << "  --witness   Include actions (F flag, O open, C chord). Cannot be used with CSV.\n"
                     << "  --quiet     Suppress stderr and --randomruns progress\n"
                     << "  --time-limit SECONDS / --max-states N (default 0: "
                        "unlimited)\n"
                     << "--rng-load FILENAME\n"
                        "              Load the rng state from the filename.\n"
                     << "--rng-save FILENAME\n"
                        "              Save the rng state to the filename.\n"
                     << "Exit: 0 success, 2 exact search limited, 1 invalid "
                        "input/error.\n";
                return 0;
            } else if (a == "--random") {
                random = true;
                w = integer();
                h = integer();
                mines = integer();
                seed = natural(arg());
            } else if (a == "--randomruns") {
                random = true;
                w = integer();
                h = integer();
                mines = integer();
                runs = natural(arg());
                seed = natural(arg());
            } else if (a == "--64b") {
                random64 = true;
            } else if (a == "--algorithm") {
                algorithm = arg();
            } else if (a == "--all") {
                all = true;
            } else if (a == "--json") {
                if (output_format != Formats::DEFAULT && output_format != Formats::JSON) {
                    throw runtime_error("Please choose one output format.");
                }
                output_format = Formats::JSON;
            } else if (a == "--json-file") {
                if (output_format != Formats::DEFAULT && output_format != Formats::JSON) {
                    throw runtime_error("Please choose one output format.");
                }
                output_format = Formats::JSON;
                file = true;
                output_filename = arg();
            } else if (a == "--csv") {
                if (output_format != Formats::DEFAULT && output_format != Formats::CSV) {
                    throw runtime_error("Please choose one output format.");
                }
                output_format = Formats::CSV;
            } else if (a == "--csv-file") {
                if (output_format != Formats::DEFAULT && output_format != Formats::CSV) {
                    throw runtime_error("Please choose one output format.");
                }
                output_format = Formats::CSV;
                file = true;
                output_filename = arg();
            } else if (a == "--quiet") {
                options.progress = false;
                progress = false;
            } else if (a == "--witness") {
                witness = true;
            } else if (a == "--max-states") {
                auto n = natural(arg());
                if (n > numeric_limits<size_t>::max()) {
                    throw runtime_error("State limit out of range");
                }
                options.maxStates = size_t(n);
            } else if (a == "--time-limit") {
                string v = arg();
                size_t used;
                options.timeLimit = stod(v, &used);
                if (used != v.size() || !isfinite(options.timeLimit)
                    || options.timeLimit < 0
                ) {
                    throw runtime_error("Invalid time limit");
                }
            } else if (a == "--rng-load") {
                rng_load = true;
                rng_load_filename = arg();
            } else if (a == "--rng-save") {
                rng_save = true;
                rng_save_filename = arg();
            } else if (a.starts_with("--")) {
                throw runtime_error("Unknown option: " + a);
            } else if (input.empty()) {
                input = a;
            } else {
                throw runtime_error("Unexpected argument: " + a);
            }
        }
        if (algorithm != "optimal" && algorithm != "8way" && algorithm != "lzini" &&
            algorithm != "hzini"
        ) {
            throw runtime_error("Unknown algorithm: " + algorithm);
        }
        if (all && algorithm != "optimal") {
            throw runtime_error("Choose --all or --algorithm");
        }
        if (random == !input.empty()) {
            throw runtime_error("Supply either a URL or --random; see --help");
        }
        if (random64 && !random) {
            throw runtime_error("--64b requires --random or --randomruns");
        }
        if (random && !random64 && seed > UINT32_MAX) {
            throw runtime_error("Seed out of range");
        }
        if (witness && output_format == Formats::CSV) {
            throw runtime_error("--witness outputs are not supported by CSV output.");
        }
        if (!input.empty() && input.starts_with("b=")) {
            input = "?" + input;
        }
        minclicks::coord_cell_divisor = w;
        minclicks::coord_cell_multiplier = computeMultiplier<CellType>(coord_cell_divisor);
        Board b = random ? (random64 ? randomBoard64(w, h, mines, seed)
                                     : randomBoard(w, h, mines, uint32_t(seed)))
                         : decodeBoard(input);
        if (all && output_format == Formats::DEFAULT) {
            output_format = Formats::JSON;
        }
        if (output_format != Formats::DEFAULT) {
            options.progress = false;
        }
#ifdef __EMSCRIPTEN__
        if (!json) {
            options.onProgress = [](const Progress &p) {
                browser_progress(p.phase, p.completed, p.total, double(p.states),
                                 double(p.peak), p.best, p.elapsed,
                                 double(p.transitions));
            };
        }
#endif
        bool isExact = all || algorithm == "optimal";
        bool exact_exact;
        std::chrono::time_point<std::chrono::steady_clock> start_runs;
        double end_runs, end_writes;
        double write_time_elapsed = 0;
        {
            JSONResultWriter json_writer(output_filename);
            JSONResult json_result;
            JSONMetadata json_metadata{runs, b.w, b.h, b.mineCount()};
            if (!rng_load) {
                json_metadata.initial_seed = random64 ? seed : uint32_t(seed);
            }
            if (!isExact) {
                json_metadata.algorithm = algorithm;
            }
            if (runs > 1) {
                json_writer.update_metadata(json_metadata);
            }
            CSVResultWriter csv_writer = file ? CSVResultWriter(output_filename) : CSVResultWriter();
            CSVResult csv_result;
            if (rng_load) {
                if (b.load_rng_state(rng_load_filename)) {
                    cout << "RNG state loaded successfully from '" << rng_load_filename << "'" << endl;
                } else {
                    throw runtime_error("Failed to load RNG or file was empty.\n");
                }
            }
            //CSVResult csv_output;
            ExactResult exact;
            HeuristicResult heuristic;
            LegacyZiniResults legacy;
            HeuristicResult eight;
            start_runs = Clock::now();
            for (int run = 1; run <= runs; run++) {
                if (all || algorithm == "optimal") {
                    exact = solveExact(b, options);
                }
                if (all) {
                    legacy = legacyZini(b);
                    if (witness) {
                        eight = eightWayZini(b);
                    }
                    else eight.clicks = eightWayZiniClicks(b);
                } else if (algorithm == "hzini") {
                    heuristic = hzini(b);
                } else if (algorithm == "lzini") {
                    heuristic = lzini(b);
                } else if (algorithm == "8way") {
                    if (witness) {
                        heuristic = eightWayZini(b);
                    } else {
                        heuristic.clicks = eightWayZiniClicks(b);
                    }
                }
                exact_exact = !exact.exact;
                string status = (
                    isExact ? 
                    (exact.exact ? "optimal" : "resource limit") 
                    : "heuristic"
                );
                const auto &actions = isExact ? exact.actions : heuristic.actions;
                auto start_output = Clock::now();
                switch (output_format) {
                    case Formats::JSON:
                        if (!file) {
                            // TODO: Fully Implement JSONResultWriter for Terminal output and update here
                            cout << setprecision(9) << "{\"url\":\"" << b.url()
                                << "\",\"width\":" << static_cast<unsigned>(b.w) << ",\"height\":" << static_cast<unsigned>(b.h)
                                << ",\"mines\":" << b.mineCount() << ",\"three_bv\":" << b.bv()
                                << ",\"status\":\"" << status << "\"";
                            if (isExact) {
                                cout << ",\"minclicks\":"
                                    << (exact.exact ? to_string(exact.clicks) : "null")
                                    << ",\"upper_bound\":" << exact.clicks
                                    << ",\"peak_states\":" << exact.peak
                                    << ",\"solve_seconds\":" << exact.elapsed;
                            } else {
                                cout << ",\"algorithm\":\"" << algorithm
                                    << "\",\"clicks\":" << heuristic.clicks;
                            }
                            if (all) {
                                cout << ",\"8way\":" << eight.clicks
                                    << ",\"lzini\":" << legacy.lzini.clicks
                                    << ",\"hzini\":" << legacy.hzini.clicks;
                            }
                            if (witness) {
                                auto emit = [&](const vector<Action> &a) {
                                    cout << '[';
                                    bool comma = false;
                                    for (auto v : a) {
                                        if (comma) {
                                            cout << ',';
                                        }
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
                            cout << "}\n";
                        } else {
                            json_result.url = b.url();
                            if (runs == 1) {
                                json_result.width = b.w;
                                json_result.height = b.h;
                                json_result.mines = b.mineCount();
                            }
                            json_result.three_bv = b.bv();
                            json_result.status = status;
                            if (isExact) {
                                json_result.minclicks = (exact.exact ? exact.clicks : 0);
                                json_result.upper_bound = exact.clicks;
                                json_result.peak_states = exact.peak;
                                json_result.solve_seconds = exact.elapsed;
                            } else {
                                if (runs == 1) {
                                    json_result.algorithm = algorithm;
                                }
                                json_result.clicks = heuristic.clicks;
                            }
                            if (all) {
                                json_result.eight_way = eight.clicks;
                                json_result.lzini = legacy.lzini.clicks;
                                json_result.hzini = legacy.hzini.clicks;
                            }
                            if (witness) {
                                if (all) {
                                    AllActions expandedActions;
                                    ExpandedActionsConversion(actions, expandedActions.optimal);
                                    ExpandedActionsConversion(eight.actions, expandedActions.way8);
                                    ExpandedActionsConversion(legacy.lzini.actions, expandedActions.lzini);
                                    ExpandedActionsConversion(legacy.hzini.actions, expandedActions.hzini);
                                    json_result.actions = expandedActions;
                                } else {
                                    SingleActions expandedActions;
                                    ExpandedActionsConversion(actions, expandedActions);
                                    json_result.actions = expandedActions;
                                }
                            }
                            if (runs == 1) {
                                json_writer.output_single(json_result);
                            } else {
                                json_writer.add_result(json_result);
                            }
                        }
                        break;
                    case Formats::CSV:
                        csv_result.url = b.url();
                        csv_result.width = b.w;
                        csv_result.height = b.h;
                        csv_result.mines = b.mineCount();
                        csv_result.three_bv = b.bv();
                        csv_result.status = status;
                        if (isExact) {
                            csv_result.minclicks = (exact.exact ? exact.clicks : 0);
                            csv_result.upper_bound = exact.clicks;
                            csv_result.peak_states = exact.peak;
                            csv_result.solve_seconds = exact.elapsed;
                        } else {
                            csv_result.algorithm = algorithm;
                            csv_result.clicks = heuristic.clicks;
                        }
                        if (all) {
                            csv_result.eight_way = eight.clicks;
                            csv_result.lzini = legacy.lzini.clicks;
                            csv_result.hzini = legacy.hzini.clicks;
                        }
                        csv_writer.add_result(csv_result);
                        break;
                    default:
                        cout << "Board: " << static_cast<unsigned>(b.w) << 'x' << static_cast<unsigned>(b.h) << ", " << b.mineCount()
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
                                << "\nFinal order: " << r.order << "\nSolve time: " << r.elapsed
                                << " seconds\nDominated states: " << r.dominated << '\n';
                        } else {
                            cout << "Algorithm: " << algorithm
                                << "\nStatus: heuristic\nUpper bound clicks: " << heuristic.clicks
                                << '\n';
                        }
                        if (witness) {
                            for (auto a : actions) {
                                cout << "Action: " << a.kind << ' ' << a.cell % b.w << ' '
                                    << a.cell / b.w << '\n';
                            }
                        }
                        break;
                }
                write_time_elapsed += seconds(start_output);
                if (run < runs) {
                    if (!file) {
                        cout << "\n\n";
                    }
                    b.randomize();
                    if (progress) {
                        if (runs > 10) {
                            if (run % (runs / 10) == 0) {
                                cout << "Runs " << run << " / " << runs << " (" << ((run*100) / runs) << "%)" << endl;
                            }
                        } else {
                            cout << "Runs " << run << " / " << runs << " (" << ((run*100) / runs) << "%)" << endl;
                        }
                    }
                }
            }
            end_runs = seconds(start_runs) - write_time_elapsed;
            if (output_format == Formats::JSON) {
                json_metadata.elapsed_time = end_runs;
                json_writer.update_metadata(json_metadata);
            }
        }
        end_writes = seconds(start_runs);
        cout << "Run Time Elapsed: " << end_runs << "s" << endl;
        cout << "Overall Time Elapsed: " << end_writes << "s" << endl;
        cout << "Average Time Per Board: " << (end_runs / runs) << "s (" << (runs / end_runs) << "/s)" << endl;
        
        // Save RNG state
        if (rng_save) {
            stringstream ss;
            if (b.save_rng_state(rng_save_filename)) {
                cout << "RNG state saved successfully to '" << rng_save_filename << "'" << endl;
            }
        } 
        
        return isExact && !exact_exact ? 2 : 0;
    } catch (const exception &e) {
        cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}


