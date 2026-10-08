#ifndef BOARD_TPP
#define BOARD_TPP

#include <stdexcept>
#include <iostream>
#include <charconv>
#include <string_view>
#include <system_error>

namespace minclicks {

template <typename QuotientType, typename RemainderType>
struct DivisionPair {
    QuotientType quotient;
    RemainderType remainder;
};

// Faster version of integer using string_view
template <typename IntType>
static IntType integer(std::string_view s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("Expected nonnegative integer: " + std::string(s));
    }

    IntType v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);

    if (ec != std::errc() || ptr != s.data() + s.size()) {
        throw std::runtime_error("Invalid integer: " + std::string(s));
    }
    return v;
}

template <typename T>
struct is_variant : std::false_type {};

template <typename... Args>
struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T>
inline constexpr bool is_variant_v = is_variant<std::decay_t<T>>::value;

template <typename SizeType, typename ConcreteRNG>
void randomMinesImpl(std::vector<CellType> &mine, CellType mines, CellType cell_count, ConcreteRNG &rng) {
    std::vector<CellType> order(cell_count);
    std::iota(order.begin(), order.end(), 0);

    for (int i = cell_count - 1; i > 0; --i) {
        SizeType bound = i + 1, threshold = SizeType(-bound) % bound, x;
        do {
            x = rng();
        } while (x < threshold);
        std::swap(order[i], order[x % bound]);
    }
    
    for (int i = 0; i < mines; ++i) {
        mine[order[i]] = 1;
    }
}

template <typename RNGType>
void randomMines(std::vector<CellType> &mine, CellType mines, CellType cell_count, RNGType &rng) {
    using DecayedRNG = std::decay_t<RNGType>;
    if constexpr (is_variant_v<DecayedRNG>) {
        std::visit([&](auto &active_rng) {
            using T = std::decay_t<decltype(active_rng)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                throw std::invalid_argument("Invalid RNG state. `rng` must be type `std::mt19937` or `std::mt19937_64`.");
            } else {
                using SizeType = typename T::result_type;
                randomMinesImpl<SizeType>(mine, mines, cell_count, active_rng);
            }
        }, rng);
    } else {
        using SizeType = typename DecayedRNG::result_type;
        randomMinesImpl<SizeType>(mine, mines, cell_count, rng);
    }
}

/*
template <typename nType, typename dType, typename QuotientType, typename RemainderType>
DivisionPair<QuotientType, RemainderType> FractionalDivision(nType n, dType divisor) {
    constexpr unsigned int shift_width = sizeof(nType) * 8;
    using ProductType = std::conditional_t<
        sizeof(nType) == 1, unsigned short,
        std::conditional_t<
            sizeof(nType) == 2, unsigned int, 
            unsigned long long
        >
    >;
    // Explicitly cast to prevent overflow before shifting from multiplication
    unsigned short quotient = static_cast<unsigned short>((static_cast<ProductType>(n) * coord_cell_multiplier) >> shift_width);
    return {
        quotient: static_cast<QuotientType>(quotient);
        remainder: static_cast<RemainderType>(static_cast<>(cell - (quotient * divisor);
    };
}
*/

} // namespace minclicks

#endif