#include "tmap/integer_cost.h"
#include <limits>
namespace tmap
{
Result integerAccumulateCost(uint64_t previous, uint64_t edge, uint64_t& output)
{
    if (edge > std::numeric_limits<uint64_t>::max() - previous)
    {
        output = 0;
        return Result::BudgetExceeded;
    }
    output = previous + edge;
    return Result::Ok;
}
Result integerEdgeCost(uint32_t length, uint32_t ascent, bool has_elevation, AscentRatio ratio, uint64_t& output)
{
    output = 0;
    if (!ratio.denominator) return Result::Invalid;
    if (ratio.numerator && !has_elevation) return Result::Incomplete;
    return integerAccumulateCost(uint64_t(length) * ratio.denominator, uint64_t(ascent) * ratio.numerator, output);
}
Result integerPriority(uint64_t cost, uint32_t lower_bound, AscentRatio ratio, uint64_t& output)
{
    output = 0;
    if (!ratio.denominator) return Result::Invalid;
    return integerAccumulateCost(cost, uint64_t(lower_bound) * ratio.denominator, output);
}
} // namespace tmap
