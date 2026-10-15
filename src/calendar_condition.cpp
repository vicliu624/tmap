#include "tmap/calendar_condition.h"
namespace tmap
{
namespace
{
bool validDate(uint32_t value, bool annual)
{
    const auto year = annual ? 2000U : value / 512;
    const auto month = value % 512 / 32, day = value % 32;
    if ((annual && value >= 512) || year < 1 || year > 9999 || month < 1 || month > 12 || !day) return false;
    const auto last = month == 2 ? 28U + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) : ((month + (month > 7)) % 2 ? 31U : 30U);
    return day <= last;
}
// Era arithmetic adapted from Howard Hinnant's public-domain civil algorithms:
// https://howardhinnant.github.io/date_algorithms.html (no lookup tables).
uint32_t civil(int32_t days)
{
    const int32_t shifted = days + 719468;
    const auto era = shifted / 146097;
    const auto day = static_cast<uint32_t>(shifted - era * 146097);
    const auto year = (day - day / 1460 + day / 36524 - day / 146096) / 365;
    const auto ordinal = day - (365 * year + year / 4 - year / 100);
    const auto adjusted_month = (5 * ordinal + 2) / 153;
    const auto month = adjusted_month < 10 ? adjusted_month + 3 : adjusted_month - 9;
    const auto month_day = ordinal - (153 * adjusted_month + 2) / 5 + 1;
    return (year + era * 400 + (month <= 2)) * 512 + month * 32 + month_day;
}
bool selected(const CalendarPredicate& p, int32_t day)
{
    const auto date = civil(day);
    const auto month = date % 512 / 32;
    const auto weekday = static_cast<uint32_t>((day + 3) % 7 + 7) % 7;
    if (!(p.months & (1U << (month - 1))) || !(p.weekdays & (1U << weekday))) return false;
    if (!p.date_kind) return true;
    const auto value = p.date_kind == 1 ? date % 512 : date;
    return p.first_date <= p.last_date ? value >= p.first_date && value <= p.last_date : value >= p.first_date || value <= p.last_date;
}
} // namespace
Result evaluateCalendarPredicate(const CalendarPredicate& p, uint64_t utc, int32_t offset, bool& output)
{
    if (!p.months || (p.months & ~0x0fffU) || !p.weekdays || (p.weekdays & ~0x7fU) ||
        p.first_minute >= 1440 || p.last_minute > 1440 || p.first_minute == p.last_minute ||
        p.date_kind > 2 || utc > UINT64_C(253402300799) || offset <= -86400 || offset >= 86400)
        return Result::Invalid;
    if ((!p.date_kind && (p.first_date || p.last_date)) ||
        (p.date_kind && (!validDate(p.first_date, p.date_kind == 1) || !validDate(p.last_date, p.date_kind == 1))) ||
        (p.date_kind == 2 && p.first_date > p.last_date)) return Result::Invalid;
    const auto local = static_cast<int64_t>(utc) + offset;
    if (local > INT64_C(253402300799)) return Result::Invalid;
    const auto day = static_cast<int32_t>(local >= 0 ? local / 86400 : (local - 86399) / 86400);
    const auto minute = static_cast<uint16_t>((local - int64_t(day) * 86400) / 60);
    if (p.first_minute < p.last_minute)
        output = minute >= p.first_minute && minute < p.last_minute && selected(p, day);
    else
        output = (minute >= p.first_minute && selected(p, day)) || (minute < p.last_minute && selected(p, day - 1));
    return Result::Ok;
}
} // namespace tmap
