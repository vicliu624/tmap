#pragma once
#include "tmap/calendar_rules.h"
#include "tmap/road_permissions.h"
namespace tmap
{
struct RoadRuleRange
{
    uint64_t offset = 0;
    uint32_t count = 0, fallback = 0;
};
// Decode descriptor version 1: header8, seven range16 records. Publication is
// atomic on Ok; record_bytes is the logical condition section size.
Result decodeRoadRuleProgram(const uint8_t* bytes, size_t size, uint64_t record_bytes,
                             std::array<RoadRuleRange, 7>& programs, uint8_t& highway,
                             bool& reversed, uint32_t max_records = 4096);
struct RoadProgramReadCursor
{
    ReadCursor read{};
    std::array<uint8_t, 120> bytes{};
    uint64_t record_bytes = 0;
    uint32_t max_records = 4096;
    Result terminal = Result::Invalid;
    RoadProgramReadCursor() = default;
    RoadProgramReadCursor(const RoadProgramReadCursor&) = delete;
    RoadProgramReadCursor& operator=(const RoadProgramReadCursor&) = delete;
};
Result beginRoadProgramRead(OutdoorPackage& package, uint32_t row, RoadProgramReadCursor& cursor,
                            uint32_t max_records = 4096);
Result roadProgramReadStep(OutdoorPackage& package, RoadProgramReadCursor& cursor, size_t page_budget,
                           std::array<RoadRuleRange, 7>& programs, uint8_t& highway, bool& reversed);
void cancel(RoadProgramReadCursor& cursor);
struct RoadRuleCursor
{
    CalendarRuleCursor calendar{};
    std::array<RoadRuleRange, 7> programs{};
    RoadPermissions permissions{};
    RoadMasks masks{};
    uint64_t initial_epoch = 0, bytes = 0;
    uint8_t property = 0, highway = 0, requested = 0;
    Result terminal = Result::Invalid;
    RoadRuleCursor() = default;
    RoadRuleCursor(const RoadRuleCursor&) = delete;
    RoadRuleCursor& operator=(const RoadRuleCursor&) = delete;
};
// Range order: access, vehicle, foot, bicycle, oneway, bicycle oneway, foot oneway.
// Copies descriptors, borrows file; max_records bounds their combined count.
Result beginRoadRules(RandomAccessFile& file, const std::array<RoadRuleRange, 7>& programs,
                      uint64_t utc, int32_t local_offset, uint8_t highway, uint8_t requested,
                      RoadRuleCursor& cursor, uint32_t max_records = 4096,
                      FileEpoch epoch = nullptr, void* context = nullptr);
Result roadRulesStep(RoadRuleCursor& cursor, size_t budget, RoadMasks& output);
void cancel(RoadRuleCursor& cursor);
} // namespace tmap
