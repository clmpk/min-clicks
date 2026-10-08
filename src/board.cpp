#include "board.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <iostream>
#include <string>
#include <string_view>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minclicks {
using namespace std;

template <class T> static void unique_sort(vector<T> &v) {
    sort(v.begin(), v.end());
    v.erase(unique(v.begin(), v.end()), v.end());
}

constexpr RNGType deduceRNGType(const RNGStateType &rng) {
    switch (rng.index()) {
        case 1: return RNGType::RNG32;
        case 2: return RNGType::RNG64;
        default: return RNGType::None;
    }
}

Board::Board(
    CoordType width,
    CoordType height,
    vector<CellType> mines,
    CellType cellCount,
    RNGStateType rng_state
) : w(width), h(height), mine(move(mines)), cell_count(cellCount), rng_state(rng_state), rng_type(deduceRNGType(rng_state)) {
    if (w < 1 || h < 1 || w > 99 || h > 99 ||
        mine.size() != size_t(w * h) || mine.size() != cell_count
        || any_of(
            mine.begin(),
            mine.end(),
            [](int v) { return v != 0 && v != 1; }
        )
    ) {
        throw invalid_argument("Invalid board dimensions or mine map");
    }
    number.resize(cell_count);
    island.assign(cell_count, -1);
    adj.resize(cell_count);
    setupBoard();
    minecount = static_cast<CellType>(count(mine.begin(), mine.end(), 1));
}

Board::Board(CoordType width, CoordType height, vector<CellType> mines, RNGStateType rng_state)
    : Board(width, height, move(mines), width * height, rng_state) {
}

Board::Board(CoordType width, CoordType height, vector<CellType> mines)
    : Board(width, height, move(mines), width * height) {
}

Board::Board(CoordType width, CoordType height, vector<CellType> mines, CellType cellCount)
    : Board(width, height, move(mines), cellCount, rng_state) {
}

void Board::setupBoard() {
    //cout << "Start of setupBoard()" << endl;
    for (CellType p = 0; p < cell_count; ++p) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                CellType x = p % w + dx, y = p / w + dy;
                if ((dx || dy) && x >= 0 && x < w && y >= 0 && y < h) {
                    adj[p].push_back(y * w + x);
                }
            }
        }
        for (int q : adj[p]) {
            number[p] += mine[q];
        }
        if (mine[p]) {
            number[p] = -1;
        }
    }

    for (CellType p = 0; p < cell_count; ++p) {
        if (number[p] == 0 && island[p] < 0) {
            CellType z = (CellType)zeros.size();
            zeros.push_back({p});
            island[p] = z;
            for (size_t i = 0; i < zeros[z].size(); ++i) {
                for (CellType q : adj[zeros[z][i]]) {
                    if (number[q] == 0 && island[q] < 0) {
                        island[q] = z;
                        zeros[z].push_back(q);
                    }
                }
            }
        }
    }

    borders.clear();
    borders.resize(zeros.size());
    for (CellType p = 0; p < cell_count; ++p) {
        if (number[p] > 0) {
            bool border = false;
            for (CellType q : adj[p]) {
                if (island[q] >= 0) {
                    borders[island[q]].push_back(p);
                    border = true;
                }
            }
            if (!border) {
                targets.push_back(p);
            }
        }
    }

    for (auto &v : borders) {
        unique_sort(v);
    }
    bbbv = static_cast<CellType>(zeros.size() + targets.size());
}

CellType Board::bv() const { return bbbv; }

CellType Board::mineCount() const { return minecount; }

string Board::url() const {
    string b;
    if (w == 9 && h == 9) {
        b = "1";
    } else if (w == 16 && h == 16) {
        b = "2";
    } else if (w == 30 && h == 16) {
        b = "3";
    } else if (w < 10 && h < 10) {
        b = to_string(w) + to_string(h);
    } else {
        b = (w < 10 ? "0" : "") + to_string(w) + (h < 10 ? "0" : "") +
            to_string(h);
    }
    string m, alphabet = "0123456789abcdefghijklmnopqrstuv";
    for (CellType i = 0; i < cell_count; i += 5) {
        int v = 0;
        for (unsigned char j = 0; j < 5; ++j) {
            v = 2 * v + (i + j < cell_count ? mine[i + j] : 0);
        }
        m += alphabet[v];
    }
    return "https://llamasweeper.com/#/game/zini-explorer?b=" + b + "&m=" + m;
}

void Board::randomize() {
    if (rng_type == RNGType::None) {
        throw logic_error("Non-random Board cannot call `randomize()`.");
    }
    ranges::fill(number, 0);
    ranges::fill(island, -1);
    adj.clear();
    adj.resize(cell_count);
    ranges::fill(mine, 0);
    zeros.clear();
    borders.clear();
    targets.clear();
    randomMines<RNGStateType>(mine, minecount, cell_count, rng_state);
    setupBoard();
}

bool Board::save_rng_state(const string& filename) {
    ofstream out(filename);
    if (!out.is_open()) {
        return false; // File could not be opened/created
    }

    // 1. Write the active index so the loader knows the type
    out << rng_state.index() << " ";

    // 2. Stream the engine's internal state
    visit([&out](const auto& active_rng) {
        using T = decay_t<decltype(active_rng)>;
        if constexpr (!is_same_v<T, monostate>) {
            out << active_rng;
        }
    }, rng_state);

    return true;
}

bool Board::load_rng_state(const string& filename) {
    ifstream in(filename);
    if (!in.is_open()) {
        rng_state = monostate{}; // Return empty if file doesn't exist
        rng_type = RNGType::None;\
        return false;
    }

    size_t type_index = 0;
    if (!(in >> type_index)) {
        rng_state = monostate{};
        rng_type = RNGType::None;
        return false;
    }

    RNGStateType rng;

    // Emplace the correct engine type based on the index, then stream into it
    if (type_index == 1) {
        rng.emplace<mt19937>();
        in >> get<mt19937>(rng);
    } else if (type_index == 2) {
        rng.emplace<mt19937_64>();
        in >> get<mt19937_64>(rng);
    } else {
        rng.emplace<monostate>();
        return false;
    }
    if (holds_alternative<monostate>(rng)) {
        return false;
    }
    rng_state = rng;
    rng_type = deduceRNGType(rng_state);

    randomize();
    return true;
}

static string parameter(const string &s, const string &name) {
    for (char delimiter : {'?', '&'}) {
        string prefix = string(1, delimiter) + name + "=";
        size_t i = s.find(prefix);
        if (i != string::npos) {
            i += prefix.size();
            return s.substr(i, s.find_first_of("&#", i) - i);
        }
    }
    throw runtime_error("URL missing parameter '" + name + "'");
}
static int integer(const string &s) {
    if (s.empty() || s.find_first_not_of("0123456789") != string::npos) {
        throw runtime_error("Expected nonnegative integer: " + s);
    }
    size_t used = 0;
    int v = stoi(s, &used);
    if (used != s.size()) {
        throw runtime_error("Invalid integer: " + s);
    }
    return v;
}

Board decodeBoard(const string &url) {
    string b = parameter(url, "b"), m = parameter(url, "m");
    string_view b_view(b);
    CoordType w, h;
    if (b_view == "1") {
        w = h = 9;
    } else if (b_view == "2") {
        w = h = 16;
    } else if (b_view == "3") {
        w = 30;
        h = 16;
    } else if (b_view.size() == 2 || b_view.size() == 4) {
        size_t mid = b_view.size() / 2;
        w = integer<CoordType>(b_view.substr(0, mid));
        h = integer<CoordType>(b_view.substr(mid));
    } else { 
        throw runtime_error("Invalid board size");
    }
    if (w < 1 || h < 1 || w > 99 || h > 99) {
        throw runtime_error("Board dimensions must be 1..99");
    }
    CellType cell_count = static_cast<CellType>(size_t(w * h));
    if (m.size() * 5 < cell_count) {
        throw runtime_error("Mine encoding too short");
    }

    vector<CellType> mines;
    mines.reserve(cell_count);
    bool done = false;
    for (char c : m) {
        size_t v;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'v') {
            v = c - 'a' + 10;
        } else {
            throw runtime_error("Invalid base-32 mine encoding");
        }
        
        for (int bit = 4; bit >= 0; --bit) {
            if (mines.size() < cell_count) {
                mines.push_back((v >> bit) & 1);
            } else {
                done = true;
                break;
            }
        }
        if (done) {
            break;
        }
    }
    // Only need to shrink when the loops finish naturally (mines not full)
    if (!done) {
        mines.shrink_to_fit();
    }
    
    return Board(w, h, move(mines), cell_count);
}

Board randomBoard(CoordType w, CoordType h, int mines, uint32_t seed) {
    CellType cell_count = w * h;
    if (w < 1 || h < 1 || w > 99 || h > 99 || mines < 0 || mines > cell_count) {
        throw invalid_argument("Invalid random board dimensions/mine count");
    }
    vector<CellType> mine(cell_count);
    mt19937 rng(seed);
    randomMines<mt19937>(mine, mines, cell_count, rng);

    return Board(w, h, move(mine), cell_count, rng);
}

Board randomBoard64(CoordType w, CoordType h, int mines, uint64_t seed) {
    CellType cell_count = w * h;
    if (w < 1 || h < 1 || w > 99 || h > 99 || mines < 0 || mines > cell_count) {
        throw invalid_argument("Invalid random board dimensions/mine count");
    }
    vector<CellType> mine(cell_count);
    mt19937_64 rng(seed);
    randomMines<mt19937_64>(mine, mines, cell_count, rng);

    return Board(w, h, move(mine), cell_count, rng);
}

void ExpandedActionsConversion(
  const std::vector<Action> &actions, 
  std::vector<ExpandedAction> &expanded
) {
  expanded.clear();
  expanded.reserve(actions.size());

  // Determined at compile-time
  constexpr unsigned int shift_width = sizeof(CellType) * 8;
  using ProductType = std::conditional_t<
      sizeof(CellType) == 1, unsigned short,
      std::conditional_t<
        sizeof(CellType) == 2, unsigned int, 
        unsigned long long
      >
  >;

  for (const auto &action : actions) {
    // Explicitly cast to prevent overflow before shifting from multiplication
    CoordType column = static_cast<CoordType>((static_cast<ProductType>(action.cell) * coord_cell_multiplier) >> shift_width);
    CoordType row = static_cast<CoordType>(action.cell - (column * coord_cell_divisor));
    expanded.push_back({
      action.kind, 
      row,
      column
    });
  }
}

} // namespace minclicks
