#pragma once

#include <string>

#include "src/board.hpp"

namespace minclicks {

namespace std = ::std;

// Precomputed height * width fixed point multiplier for divsion
//CoordType coord_cell_divisor = 1;
//CellType coord_cell_multiplier = 0;

} // namespace minclicks

static unsigned long long natural(const std::string &s);

int main(int argc, char **argv);

#include "optimal.tpp"