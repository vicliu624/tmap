#pragma once
#include "tmap/integer_geometry.h"
#include "tmap/integer_prepared_route.h"
namespace tmap
{
// Exact, idempotent random writes: Busy consumes nothing. The application owns
// creation, exclusivity, media lifetime, durable sync, rename and deletion.
class WritableRouteFile : public RandomAccessFile
{
  public:
    virtual Status writeAt(uint64_t offset, const uint8_t* bytes, size_t count) = 0;
};
class RouteLegSpool final : public IntegerRouteLegSink
{
  public:
    RouteLegSpool(WritableRouteFile& file, uint32_t capacity) : file_(file), capacity_(capacity) {}
    Result writeLeg(uint32_t index, const IntegerRouteLeg& leg) override;
    Result readLeg(uint32_t index, IntegerRouteLeg& leg);
    uint32_t capacity() const { return capacity_; }

  private:
    WritableRouteFile& file_;
    uint32_t capacity_;
    std::array<uint8_t, 12> scratch_{};
};
// All storage is caller-owned. Place this and cursors in PSRAM, or allocate via
// MemoryObject<T> / PreferExternal. No allocator is called by the archive core.
struct RouteArchiveWorkspace
{
    static constexpr size_t kPageBytes = 1024, kHeaderBytes = 192, kNodeBytes = 64, kPointBytes = 24;
    static constexpr size_t kPoints = (kPageBytes - 64) / kPointBytes;
    std::array<uint8_t, kPageBytes> page{};
    std::array<uint8_t, kHeaderBytes> scratch{};
};
struct RouteArchiveInfo
{
    std::array<uint8_t, 16> map_id{}, map_build{}, map_series{};
    uint64_t map_revision = 0, cost_units = 0, length_cm = 0, points = 0;
    uint32_t legs = 0, chunks = 0;
    AscentRatio ascent{};
    TravelMode mode = TravelMode::Foot;
};
struct RouteArchivePoint
{
    GeoPoint point{};
    uint64_t distance_cm = 0;
    uint32_t leg = 0;
    bool break_before = false;
};
class RouteArchiveWriter
{
  public:
    explicit RouteArchiveWriter(RouteArchiveWorkspace& workspace) : workspace_(workspace) {}
    RouteArchiveWriter(const RouteArchiveWriter&) = delete;
    RouteArchiveWriter& operator=(const RouteArchiveWriter&) = delete;
    // Requires a newly created, empty file. max_bytes includes data and index.
    Result begin(WritableRouteFile& file, const OutdoorInfo& map, uint32_t legs, uint64_t cost_units, uint64_t max_bytes,
                 AscentRatio ascent = {}, TravelMode mode = TravelMode::Foot);
    Result writeLeg(uint32_t index, const IntegerRouteLeg& leg);
    Result append(const RouteArchivePoint& point);
    // One bounded IO unit per work item. Only Ok publishes a complete header.
    Result finishStep(size_t budget);
    void cancel();
    Result result() const { return terminal_; }
    const RouteArchiveInfo& info() const { return info_; }

  private:
    RouteArchiveWorkspace& workspace_;
    WritableRouteFile* file_ = nullptr;
    RouteArchiveInfo info_{};
    RouteArchivePoint previous_{};
    uint64_t max_bytes_ = 0, data_ = 0, index_ = 0, cursor_ = 0, next_level_ = 0, root_ = 0, file_bytes_ = 0;
    uint32_t fill_ = 0, level_count_ = 0, row_ = 0, legs_written_ = 0;
    uint8_t stage_ = 0, substage_ = 0;
    bool have_previous_ = false;
    Result terminal_ = Result::Invalid;
    Result flush();
    Result finishUnit();
};
struct RouteArchiveQuery
{
    // Wrapped longitude (west > east) is supported; latitude must be ordered.
    Bounds viewport{-1800000000, -900000000, 1800000000, 900000000};
    uint64_t first_cm = 0, last_cm = UINT64_MAX;
    uint64_t max_nodes = UINT64_MAX, max_segments = UINT64_MAX;
};
struct RouteArchiveSegment
{
    RouteArchivePoint first{}, last{};
};
using RouteArchiveVisitor = bool (*)(void*, const RouteArchiveSegment&);
// Conservative geographic bounds for the SDK's unrotated Web Mercator view.
Result routeArchiveViewport(const MapViewport& viewport, Bounds& bounds);
struct RouteArchiveQueryCursor
{
    std::array<uint64_t, 32> stack{};
    RouteArchiveQuery query{};
    RouteArchiveSegment segment{};
    uint64_t generation = 0, nodes = 0, segments = 0, chunk = 0;
    uint32_t vertex = 0;
    uint8_t depth = 0;
    Result terminal = Result::Invalid;
};
struct RouteArchiveValidationCursor
{
    RouteArchivePoint previous{}, point{};
    uint64_t generation = 0, row = 0, points = 0;
    uint8_t stage = 0;
    Result terminal = Result::Invalid;
};
class RouteArchiveReader
{
  public:
    explicit RouteArchiveReader(RouteArchiveWorkspace& workspace) : workspace_(workspace) {}
    RouteArchiveReader(const RouteArchiveReader&) = delete;
    RouteArchiveReader& operator=(const RouteArchiveReader&) = delete;
    // Header-only open. Visited nodes and pages are verified lazily by CRC.
    Result open(RandomAccessFile& file);
    void close();
    const RouteArchiveInfo& info() const { return info_; }
    bool matches(const OutdoorInfo& map) const;
    Result readLeg(uint32_t index, IntegerRouteLeg& leg, const OutdoorInfo& map);
    Result beginQuery(const RouteArchiveQuery& query, RouteArchiveQueryCursor& cursor);
    // Returns candidate segments with intersecting bounds. Project and clip in
    // the renderer's coordinate system; this conservative filter cannot discard
    // a projected crossing because of a geographic straight-line approximation.
    // No complete polyline or in-memory spatial index is retained.
    Result queryStep(RouteArchiveQueryCursor& cursor, size_t budget, RouteArchiveVisitor visitor, void* context);
    void cancel(RouteArchiveQueryCursor& cursor) const { cursor.terminal = Result::Cancelled; }
    Result beginValidation(RouteArchiveValidationCursor& cursor);
    Result validationStep(RouteArchiveValidationCursor& cursor, size_t budget);
    void cancel(RouteArchiveValidationCursor& cursor) const { cursor.terminal = Result::Cancelled; }

  private:
    RouteArchiveWorkspace& workspace_;
    RandomAccessFile* file_ = nullptr;
    RouteArchiveInfo info_{};
    uint64_t generation_ = 0, data_ = 0, index_ = 0, root_ = 0, file_bytes_ = 0, cached_ = UINT64_MAX;
    Result queryUnit(RouteArchiveQueryCursor& cursor, RouteArchiveVisitor visitor, void* context);
    Result validationUnit(RouteArchiveValidationCursor& cursor);
};
// Constant-space conversion of a completed spool into a self-contained archive.
// Search labels/queue may be released before beginRouteArchiveExport.
struct RouteArchiveExportCursor
{
    IntegerGeometryCursor geometry{};
    IntegerRouteLeg leg{};
    RouteArchivePoint point{};
    GeoPoint previous{};
    uint64_t generation = 0;
    uint32_t count = 0, index = 0;
    uint8_t stage = 0;
    bool pending = false, have_previous = false;
    Result terminal = Result::Invalid;
};
Result beginRouteArchiveExport(GraphReader& graph, RouteLegSpool& spool, uint32_t count,
                               RouteArchiveWriter& writer, RouteArchiveExportCursor& cursor);
Result routeArchiveExportStep(GraphReader& graph, RouteLegSpool& spool, RouteArchiveWriter& writer,
                              RouteArchiveExportCursor& cursor, size_t budget);
void cancelRouteArchiveExport(RouteArchiveWriter& writer, RouteArchiveExportCursor& cursor);
} // namespace tmap
