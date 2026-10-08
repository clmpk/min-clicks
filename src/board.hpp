#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <random>

namespace minclicks {

namespace std = ::std;

using CellType = short;
using CoordType = unsigned char;

// Precomputed height * width fixed point multiplier for division
inline CoordType coord_cell_divisor = 1;
inline CellType coord_cell_multiplier = 0;

using RNGStateType = std::variant<std::monostate, std::mt19937, std::mt19937_64>;
enum RNGType { None, RNG32, RNG64 };
constexpr RNGType deduceRNGType(const RNGStateType &rng);

// A tile has at most eight neighbors. Inline storage avoids hundreds of tiny
// allocations per generated board while retaining the original row-major order.
class Neighbors {
    std::array<CellType, 8> cells{};
    uint8_t count = 0;

 public:
    void push_back(CellType cell) {
        assert(count < cells.size());
        cells[count++] = cell;
    }
    const CellType *begin() const { return cells.data(); }
    const CellType *end() const { return cells.data() + count; }
    std::size_t size() const { return count; }
};
// Row-major cells: p = y * w + x. Mines have number -1; island -1 means
// nonzero.
struct Board {
    CoordType w, h;
    CellType cell_count;
    std::vector<CellType> mine, number, island;
    std::vector<Neighbors> adj;
    std::vector<std::vector<CellType>> zeros, borders;
    std::vector<CellType> targets;
    Board(CoordType width, CoordType height, std::vector<CellType> mines);
    Board(CoordType width, CoordType height, std::vector<CellType> mines, CellType cellCount);
    Board(CoordType width, CoordType height, std::vector<CellType> mines, RNGStateType rng_state);
    Board(CoordType width, CoordType height, std::vector<CellType> mines, CellType cellCount, RNGStateType rng_state);
    CellType bv() const;
    CellType mineCount() const;
    std::string url() const;
    void randomize();
    bool save_rng_state(const std::string& filename);
    bool load_rng_state(const std::string& filename);

    private:
        CellType minecount;
        CellType bbbv;
        RNGStateType rng_state = std::monostate{};
        RNGType rng_type = RNGType::None;
        void setupBoard();
};
Board decodeBoard(const std::string &url);
template <typename RNGType>
void randomMines(std::vector<CellType> &mine, CellType cell_count, RNGType &rng);
Board randomBoard(CoordType width, CoordType height, int mines, uint32_t seed);
Board randomBoard64(CoordType width, CoordType height, int mines, uint64_t seed);

struct Action {
    char kind;
    CellType cell;
};  // F flag, O open, C chord.
struct ExpandedAction {
    char kind;
    CoordType row;
    CoordType column;
};  // F flag, O open, C chord.

struct HeuristicResult {
    int clicks = 0;
    std::vector<Action> actions;
};

/**
 * @brief Converts a vector of Actions to a vector of ExpandedActions
 * 
 * @param actions Vector of Actions to convert from.
 * @param expanded Vector of ExpandedActions to convert to.
 */
void ExpandedActionsConversion(const std::vector<Action> &actions, std::vector<ExpandedAction> &expanded);

}  // namespace minclicks

#include "board.tpp"
