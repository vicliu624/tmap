#pragma once
#include "tmap/terrain.h"
namespace tmap
{
struct AdministrativeWorkspace
{
    std::array<char, 513> candidate{}, path{};
};
struct AdministrativeResult
{
    const char* path = nullptr; // Borrowed from workspace, consume only on Ok.
    uint32_t matches = 0;
    uint8_t depth = 0;
    bool boundary = false, approximate = false, limited = false;
};
struct AdministrativeCursor
{
    std::array<ContourCursor::Frame, 24> stack{};
    std::array<uint8_t, 48> scratch{};
    ReadCursor read{};
    AdministrativeWorkspace* workspace = nullptr;
    AdministrativeResult result{};
    GeoPoint point{}, first{}, previous{};
    uint64_t generation = 0, row = 0, geometry = 0, path_ref = 0;
    uint32_t depth = 0, remaining_rows = 0, rings = 0, points = 0, ring_points = 0;
    uint32_t operations = 0, max_operations = 100000;
    uint16_t path_bytes = 0;
    uint8_t stage = 0, candidate_depth = 0, best_depth = 0;
    bool inside = false, boundary = false, has_previous = false, complete = true;
    Result terminal = Result::Invalid;
};
Result beginAdministrativeQuery(OutdoorPackage& package, GeoPoint point, AdministrativeWorkspace& workspace,
                                AdministrativeCursor& cursor, uint32_t max_operations = 100000);
Result administrativeStep(OutdoorPackage& package, AdministrativeCursor& cursor, size_t budget);
void cancelAdministrativeQuery(AdministrativeCursor& cursor);
} // namespace tmap
