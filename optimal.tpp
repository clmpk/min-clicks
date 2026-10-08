#ifndef OPTIMAL_TPP
#define OPTIMAL_TPP

// Helper to precompute height * width fixed point multiplier for division
template <typename CellType>
inline auto computeMultiplier(unsigned int divisor) {
    // Outputs unsigned char, unsigned short, or unsigned int.
    // Depends on the size of height * width.
    using ReturnType = std::conditional_t<
        sizeof(CellType) == 1, unsigned char,
        std::conditional_t<
            sizeof(CellType) == 2, unsigned short, 
            unsigned int
        >
    >;

    if (divisor <= 1) {
        return static_cast<ReturnType>(0);
    }
    constexpr unsigned int shift_width = sizeof(CellType) * 8;
    constexpr auto shift_base =  sizeof(CellType) > 2 ? 1ULL : 1UL;
    return static_cast<ReturnType>((shift_base << shift_width) / divisor);
}

#endif