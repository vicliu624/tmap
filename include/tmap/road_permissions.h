#pragma once
#include "tmap/package.h"
namespace tmap
{
enum class RoadAccess : uint8_t
{
    Inherit,
    Denied,
    Allowed
};
enum class RoadDirection : uint8_t
{
    Inherit,
    Both,
    Forward,
    Backward
};
struct RoadPermissions
{
    RoadAccess access = RoadAccess::Inherit, vehicle = RoadAccess::Inherit;
    RoadAccess foot = RoadAccess::Inherit, bicycle = RoadAccess::Inherit;
    RoadDirection oneway = RoadDirection::Both, bicycle_oneway = RoadDirection::Inherit;
    RoadDirection foot_oneway = RoadDirection::Both;
};
struct RoadMasks
{
    uint8_t forward = 0, backward = 0;
};
// Apply resolved property values after their ordered conditional clauses.
// highway_modes describes default highway eligibility; explicit mode access can
// reopen an otherwise ineligible highway. General oneway applies to bicycles.
// No file reads, clock access, allocation or output publication on invalid input.
Result resolveRoadPermissions(const RoadPermissions& permissions, uint8_t highway_modes,
                              uint8_t requested_modes, RoadMasks& output);
} // namespace tmap
