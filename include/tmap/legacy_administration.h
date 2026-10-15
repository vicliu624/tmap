#pragma once
#include "tmap/administration.h"
namespace tmap
{
struct LegacyAdministrativeCandidate
{
    std::array<char, 513> path{};
    std::array<uint8_t, 16> id{};
    uint32_t distance_cm = 0;
    uint8_t levels = 0, flags = 0;
};
struct LegacyAdministrativeWorkspace
{
    LegacyAdministrativeCandidate candidate{};
    std::array<char, 513> path{};
};
struct LegacyAdministrativeCursor
{
    struct Frame
    {
        uint64_t offset = 0;
        uint32_t next = 0, count = 0;
        uint16_t level = 0;
        bool loaded = false;
    };
    std::array<Frame, 17> stack{};
    std::array<uint8_t, 16> build{};
    LegacyAdministrativeWorkspace* workspace = nullptr;
    LegacyAdministrativeCandidate* candidates = nullptr;
    AdministrativeResult result{};
    Bounds bounds{};
    GeoPoint point{}, candidate_point{};
    uint64_t revision = 0, row = 0, path_ref = 0;
    uint32_t operations = 0, max_operations = 100000, scanned = 0, max_candidates = 256;
    uint32_t remaining_rows = 0, radius_cm = 3000000;
    size_t capacity = 0, count = 0, depth = 0;
    uint8_t stage = 0;
    bool complete = true;
    Result terminal = Result::Invalid;
};
// Privileged adapter adds bounded spatial traversal without changing existing
// Reader methods, layout, caches or v1 on-disk format.
class LegacyAdministrativeQuery
{
  public:
    static Result begin(Reader& reader, GeoPoint point, LegacyAdministrativeWorkspace& workspace,
                        LegacyAdministrativeCandidate* candidates, size_t capacity, LegacyAdministrativeCursor& cursor,
                        uint32_t radius_m = 30000, uint32_t max_candidates = 256, uint32_t max_operations = 100000);
    static Result step(Reader& reader, LegacyAdministrativeCursor& cursor, size_t budget);
    static void cancel(LegacyAdministrativeCursor& cursor);
};
} // namespace tmap
