#pragma once
#include "tmap/package.h"
namespace tmap
{
// One conjunction. Multiple periods/clauses are evaluated by the caller.
struct CalendarPredicate
{
    uint32_t first_date = 0, last_date = 0;
    uint16_t months = 0x0fff, first_minute = 0, last_minute = 1440;
    uint8_t weekdays = 0x7f, date_kind = 0; // 0 none, 1 annual, 2 absolute.
};
// Dates encode year*512 + month*32 + day; annual dates omit the year.
// Fixed UTC offset supplied by the application; no timezone database or heap.
// Output remains unchanged on error. Overnight intervals use the previous date.
Result evaluateCalendarPredicate(const CalendarPredicate& predicate, uint64_t utc_seconds,
                                 int32_t local_offset_seconds, bool& matches);
static_assert(sizeof(CalendarPredicate) == 16, "Compact calendar conjunction");
} // namespace tmap
