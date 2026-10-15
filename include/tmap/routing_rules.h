#pragma once
#include "tmap/edge_rules.h"
#include "tmap/turn_context.h"
namespace tmap
{
// Immutable query binding; all borrowed objects remain at fixed addresses.
struct RuntimeRoutingRules
{
    EdgeRuleReader* const roads;
    TurnContextReader* const turns;
    const uint64_t utc;
    const int32_t offset;
    RuntimeRoutingRules(uint64_t time, int32_t local_offset, EdgeRuleReader* road_reader, TurnContextReader* turn_reader)
        : roads(road_reader), turns(turn_reader), utc(time), offset(local_offset) {}
    bool validFor(const OutdoorPackage& package) const
    {
        CalendarPredicate clock;
        bool selected = false;
        return evaluateCalendarPredicate(clock, utc, offset, selected) == Result::Ok &&
               (!roads || roads->validFor(package, utc, offset)) && (!turns || turns->validFor(package, utc, offset));
    }
};
} // namespace tmap
