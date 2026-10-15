#include "tmap/integer_cost.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace tmap;
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
int main()
{
    uint64_t a = 0, b = 0, sum = 0;
    // Fractional contributions survive accumulation. Rounding every edge to
    // centimeters would incorrectly tie this two-edge route with a zero ascent.
    CHECK(integerEdgeCost(1, 1, true, {1, 3}, a) == Result::Ok && a == 4);
    CHECK(integerAccumulateCost(a, a, sum) == Result::Ok && sum == 8);
    CHECK(integerEdgeCost(2, 0, true, {1, 3}, b) == Result::Ok && b == 6 && b < sum);
    CHECK(integerPriority(sum, 7, {1, 3}, a) == Result::Ok && a == 29);
    CHECK(integerEdgeCost(10, 999, false, {0, 1}, a) == Result::Ok && a == 10);
    CHECK(integerEdgeCost(10, 999, false, {1, 1}, a) == Result::Incomplete && a == 0);
    CHECK(integerEdgeCost(10, 1, true, {1, 0}, a) == Result::Invalid && a == 0);
    CHECK(integerEdgeCost(UINT32_MAX, UINT32_MAX, true, {UINT32_MAX, UINT32_MAX}, a) == Result::BudgetExceeded && a == 0);
    CHECK(integerAccumulateCost(UINT64_MAX, 1, a) == Result::BudgetExceeded && a == 0);
    CHECK(integerPriority(UINT64_MAX, 1, {1, 1}, a) == Result::BudgetExceeded && a == 0);
    CHECK(integerAccumulateCost(UINT64_MAX, 0, a) == Result::Ok && a == UINT64_MAX);
    // Independent exact representable floating reference checks arithmetic only,
    // not routing. All intermediate reference integers here are below 2^53.
    uint32_t seed = 17;
    for (size_t i = 0; i < 1000; ++i)
    {
        seed = seed * 1664525U + 1013904223U;
        const uint32_t length = seed % 100000, ascent = (seed >> 8) % 1000;
        const AscentRatio ratio{seed % 17, 1 + (seed >> 16) % 97};
        CHECK(integerEdgeCost(length, ascent, true, ratio, a) == Result::Ok);
        const double reference = double(length) * ratio.denominator + double(ascent) * ratio.numerator;
        CHECK(a == uint64_t(reference));
    }
    std::printf("PASS fractional accumulation/order, missing terrain, invalid ratio, overflow, 1000 reference arithmetic cases\n");
}
