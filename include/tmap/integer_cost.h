#pragma once
#include "tmap/package.h"
namespace tmap
{
// Exact nonnegative cost in units of 1/denominator centimeters. All edges in
// one search must use the same coefficient; no rounding at individual edges.
struct AscentRatio
{
    uint32_t numerator = 0, denominator = 1;
};
Result integerEdgeCost(uint32_t length_cm, uint32_t ascent_cm, bool has_elevation,
                       AscentRatio ratio, uint64_t& output);
Result integerAccumulateCost(uint64_t previous, uint64_t edge, uint64_t& output);
Result integerPriority(uint64_t cost, uint32_t lower_bound_cm, AscentRatio ratio, uint64_t& output);
// Conversion to display units belongs at the API boundary, after search. Core
// primitives below use no floating point, allocation or lookup tables.
} // namespace tmap
