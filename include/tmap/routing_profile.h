#pragma once
#include "tmap/package.h"

namespace tmap
{
struct RoutingProfile
{
    uint64_t snapshot_utc_seconds = 0;
    int32_t local_offset_seconds = 0;
    uint32_t travel_modes = 0;
    bool conditional_snapshot = false;
};
// Output is published only on Ok. Missing optional metadata returns Missing.
Result readRoutingProfile(OutdoorPackage& package, RoutingProfile& output);
// Weekly/daytime snapshots represent one local minute. Call before each route
// and as time changes; this does not evaluate calendars or turn rules dynamically.
Result checkRoutingProfile(const RoutingProfile& profile, uint64_t utc_seconds, int32_t local_offset_seconds,
                           uint32_t travel_mode);
} // namespace tmap
