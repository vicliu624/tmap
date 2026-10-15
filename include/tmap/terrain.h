#pragma once
#include "tmap/package.h"
#include <cstddef>

namespace tmap
{
struct GeoPoint
{
    int32_t latitude = 0, longitude = 0;
};
double distanceMeters(GeoPoint a, GeoPoint b);
struct TerrainInfo
{
    int32_t west = 0, south = 0, step_lon = 0, step_lat = 0;
    uint32_t columns = 0, rows = 0, resolution_m = 0, vertical_datum = 0;
    uint16_t block_side = 0;
    std::array<uint8_t, 16> source{};
};
struct ElevationSample
{
    double meters = 0;
    uint32_t resolution_m = 0, vertical_datum = 0;
};
struct ElevationCursor
{
    GeoPoint point{};
    uint64_t generation = 0;
    double value = 0;
    uint64_t sample_offset = 0;
    uint32_t fraction_x = 0, fraction_y = 0; // Exact E7 remainders over the DEM steps.
    uint32_t x = 0, y = 0, next = 0;
    bool complete = true;
    bool sample_pending = false;
    Result terminal = Result::Invalid;
};
void cancelElevation(ElevationCursor& cursor);
class TerrainReader
{
  public:
    explicit TerrainReader(OutdoorPackage& package) : package_(package) {}
    TerrainReader(const TerrainReader&) = delete;
    TerrainReader& operator=(const TerrainReader&) = delete;
    Result initialize();
    const TerrainInfo& info() const { return info_; }
    Result elevation(GeoPoint point, ElevationSample& output);
    Result beginElevation(GeoPoint point, ElevationCursor& cursor);
    Result elevationStep(ElevationCursor& cursor, size_t sample_budget, ElevationSample& output);

  private:
    OutdoorPackage& package_;
    TerrainInfo info_{};
    uint64_t generation_ = 0;
    bool initialized_ = false;
    ElevationCursor elevation_cursor_{};
    Result sample(ElevationCursor& cursor, uint32_t x, uint32_t y, int16_t& output);
};
struct ProfileSample
{
    GeoPoint point{};
    double distance_m = 0, elevation_m = 0;
    bool valid = false, break_before = false;
};
struct ProfileOptions
{
    double spacing_m = 30, noise_threshold_m = 3;
};
struct ProfileMetrics
{
    double distance_m = 0, ascent_m = 0, descent_m = 0;
    double minimum_m = 0, maximum_m = 0, valid_distance_m = 0;
    size_t samples = 0, valid_samples = 0, gaps = 0;
};
struct ProfileCursor
{
    const GeoPoint* points = nullptr;
    size_t count = 0, segment = 0, subdivision = 0, subdivisions = 0;
    uint64_t generation = 0;
    double segment_start_m = 0, segment_length_m = 0, accepted_m = 0, last_distance_m = 0;
    bool started = false, complete = true, previous_valid = false, filter_initialized = false;
    bool cancelled = false;
    ProfileOptions options{};
    ProfileMetrics metrics{};
    ProfileSample scratch{};
    ElevationSample elevation_scratch{};
};
using ProfileVisitor = bool (*)(void*, const ProfileSample&);
Result beginProfile(TerrainReader& terrain, OutdoorPackage& package, const GeoPoint* points, size_t count,
                    const ProfileOptions& options, ProfileCursor& cursor);
Result profileStep(TerrainReader& terrain, OutdoorPackage& package, ProfileCursor& cursor, size_t budget,
                   ProfileVisitor visitor, void* context);
// Streaming contours: a callback receives one vertex, not a complete large line.
struct ContourVertex
{
    GeoPoint point{};
    uint64_t id = 0;
    int32_t elevation_m = 0;
    bool first = false, last = false;
};
struct ContourPolicy
{
    uint32_t interval_m = 0, major_interval_m = 0;
    bool labels = false;
};
ContourPolicy contourPolicy(uint8_t zoom);
struct ContourCursor
{
    struct Frame
    {
        uint32_t row = 0, next = 0;
        uint16_t count = 0; // Wire child counts are u16; row offsets remain u32.
        uint8_t level = 0;
        bool loaded = false;
    };
    std::array<Frame, 24> stack{};
    std::array<uint8_t, 48> read_bytes{};
    ReadCursor read{};
    Bounds bounds{};
    uint64_t generation = 0, offset = 0, end = 0, block_end = 0, id = 0;
    uint32_t remaining_lines = 0, points = 0, seen = 0, depth = 0;
    int32_t latitude = 0, longitude = 0, elevation_m = 0;
    ContourPolicy policy{};
    SectionType index_type = SectionType::ContourIndex, data_type = SectionType::ContourData;
    bool overview = false;
    uint8_t zoom = 0;
    bool lod = false;
    bool complete = true;
    bool cancelled = false;
};
using ContourVisitor = bool (*)(void*, const ContourVertex&);
Result beginContours(OutdoorPackage& package, const Bounds& bounds, uint8_t zoom, ContourCursor& cursor);
Result contourStep(OutdoorPackage& package, ContourCursor& cursor, size_t budget, ContourVisitor visitor, void* context);
struct ScreenPoint
{
    float x = 0, y = 0;
};
struct MapViewport
{
    GeoPoint center{};
    uint8_t zoom = 14;
    float width = 320, height = 240;
};
bool project(const MapViewport& viewport, GeoPoint point, ScreenPoint& output);
bool clipSegment(float width, float height, ScreenPoint& a, ScreenPoint& b);
static_assert(sizeof(ContourCursor) <= 1024, "Bounded continuation");
static_assert(sizeof(ProfileCursor) <= 512, "Bounded continuation");
} // namespace tmap
