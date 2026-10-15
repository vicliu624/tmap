#pragma once
#include "tmap/terrain.h"
namespace tmap
{
struct ContourSegment
{
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int32_t elevation_m = 0;
    bool major = false;
};
struct ContourRenderOptions
{
    uint32_t max_operations = 100000;
    uint8_t max_density_retries = 5;
};
struct ContourRenderCursor
{
    ContourCursor source{};
    MapViewport viewport{};
    ContourSegment* segments = nullptr;
    size_t capacity = 0, count = 0;
    ScreenPoint previous{};
    uint32_t operations = 0, interval_m = 0, major_interval_m = 0;
    uint8_t density_retries = 0;
    ContourRenderOptions options{};
    bool overflow = false, invalid = false, has_previous = false, complete = true;
    Result terminal = Result::Invalid;
};
// Buffered output is published only on Ok. Any other terminal result discards
// the incomplete snapshot. No LVGL objects/pixel buffers are allocated here.
Result beginContourRender(OutdoorPackage& package, const MapViewport& viewport, ContourSegment* segments,
                          size_t capacity, const ContourRenderOptions& options, ContourRenderCursor& cursor);
Result contourRenderStep(OutdoorPackage& package, ContourRenderCursor& cursor, size_t budget);
static_assert(sizeof(ContourSegment) == 24, "Compact caller-owned draw segments");
static_assert(sizeof(ContourRenderCursor) <= 768, "Bounded renderer continuation");
} // namespace tmap
