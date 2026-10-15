#pragma once
#include "tmap/road_rules.h"
#include "tmap/turn_permissions.h"
namespace tmap
{
struct TurnRelation
{
    uint64_t offset = 0, group = 0;
    uint32_t edges = 0, program = 0;
};
struct TurnContextMatch
{
    uint32_t relation = 0, prefix_length = 0, next_edge = 0;
    bool complete = false;
};
Result decodeTurnRelation(const uint8_t* bytes, size_t size, uint64_t edge_bytes, uint64_t programs, TurnRelation& output);
Result decodeTurnContextMatch(const uint8_t* bytes, size_t size, uint64_t relations, uint32_t edges, TurnContextMatch& output);
Result validateTurnContextMatch(const TurnContextMatch& match, const TurnRelation& relation, uint32_t expected_edge);
// Version1 header8 (mode1/2, zero reserved), followed by two range16 descriptors.
Result decodeTurnRuleProgram(const uint8_t* bytes, size_t size, uint64_t record_bytes,
                             std::array<RoadRuleRange, 2>& programs, uint8_t& mode, uint32_t max_records = 4096);
struct TurnProgramReadCursor
{
    ReadCursor read{};
    std::array<uint8_t, 40> bytes{};
    uint64_t record_bytes = 0;
    uint32_t max_records = 4096;
    Result terminal = Result::Invalid;
    TurnProgramReadCursor() = default;
    TurnProgramReadCursor(const TurnProgramReadCursor&) = delete;
    TurnProgramReadCursor& operator=(const TurnProgramReadCursor&) = delete;
};
Result beginTurnProgramRead(OutdoorPackage& package, uint32_t row, TurnProgramReadCursor& cursor, uint32_t max_records = 4096);
Result turnProgramReadStep(OutdoorPackage& package, TurnProgramReadCursor& cursor, size_t page_budget,
                           std::array<RoadRuleRange, 2>& programs, uint8_t& mode);
void cancel(TurnProgramReadCursor& cursor);
struct TurnRuleCursor
{
    CalendarRuleCursor calendar{};
    std::array<RoadRuleRange, 2> programs{};
    uint64_t initial_epoch = 0, bytes = 0;
    uint32_t general = 0;
    uint8_t property = 0, value = 0;
    Result terminal = Result::Invalid;
    TurnRuleCursor() = default;
    TurnRuleCursor(const TurnRuleCursor&) = delete;
    TurnRuleCursor& operator=(const TurnRuleCursor&) = delete;
};
Result beginTurnRules(RandomAccessFile& file, const std::array<RoadRuleRange, 2>& programs,
                      uint64_t utc, int32_t offset, TurnRuleCursor& cursor, uint32_t max_records = 4096,
                      FileEpoch epoch = nullptr, void* context = nullptr);
Result turnRulesStep(TurnRuleCursor& cursor, size_t budget, uint8_t& value);
void cancel(TurnRuleCursor& cursor);
} // namespace tmap
