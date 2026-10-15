#pragma once
#include "tmap/calendar_condition.h"
#include "tmap/file_identity.h"
namespace tmap
{
struct CalendarRuleResult
{
    uint32_t value = 0;
    bool matched = false;
};
struct CalendarRuleCursor
{
    RandomAccessFile* file = nullptr;
    FileEpoch epoch = nullptr;
    void* context = nullptr;
    uint64_t bytes = 0, initial_epoch = 0, offset = 0, utc = 0;
    uint32_t count = 0, index = 0, ordinal = 0, clause_value = 0;
    uint32_t max_value = UINT32_MAX;
    int32_t local_offset = 0;
    std::array<uint8_t, 24> scratch{};
    CalendarPredicate predicate{};
    CalendarRuleResult value{};
    Result terminal = Result::Invalid;
    CalendarRuleCursor() = default;
    CalendarRuleCursor(const CalendarRuleCursor&) = delete;
    CalendarRuleCursor& operator=(const CalendarRuleCursor&) = delete;
};
// 24-byte little-endian records: predicate16, clause ordinal32, action32.
Result beginCalendarRules(RandomAccessFile& file, uint64_t offset, uint32_t count, uint64_t utc,
                          int32_t local_offset, uint32_t fallback, CalendarRuleCursor& cursor,
                          uint32_t max_records = 4096, FileEpoch epoch = nullptr, void* context = nullptr,
                          uint32_t max_value = UINT32_MAX);
Result calendarRulesStep(CalendarRuleCursor& cursor, size_t budget, CalendarRuleResult& output);
void cancel(CalendarRuleCursor& cursor);
} // namespace tmap
