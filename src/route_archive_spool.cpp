#include "route_archive_wire.h"
namespace tmap
{
using namespace route_detail;
Result RouteLegSpool::writeLeg(uint32_t index, const IntegerRouteLeg& leg)
{
    if (index >= capacity_) return Result::BudgetExceeded;
    encodeLeg(scratch_.data(), leg);
    return archiveIo(file_.writeAt(uint64_t(index) * 12, scratch_.data(), 12));
}
Result RouteLegSpool::readLeg(uint32_t index, IntegerRouteLeg& leg)
{
    if (index >= capacity_) return Result::BudgetExceeded;
    const auto r = archiveIo(file_.readAt(uint64_t(index) * 12, scratch_.data(), 12));
    if (r == Result::Ok) decodeLeg(scratch_.data(), leg);
    return r;
}
} // namespace tmap
