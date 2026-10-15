#include "tmap/calendar_condition.h"
#include <cstdio>
int main()
{
    unsigned long long utc;
    int offset;
    unsigned first, last, months, start, end, weekdays, kind;
    while (std::scanf("%llu %d %u %u %u %u %u %u %u", &utc, &offset, &first, &last, &months, &start, &end, &weekdays, &kind) == 9)
    {
        if (months > 65535 || start > 65535 || end > 65535 || weekdays > 255 || kind > 255) return 2;
        const tmap::CalendarPredicate predicate{first, last, static_cast<uint16_t>(months), static_cast<uint16_t>(start), static_cast<uint16_t>(end), static_cast<uint8_t>(weekdays), static_cast<uint8_t>(kind)};
        bool selected = true;
        const auto result = tmap::evaluateCalendarPredicate(predicate, utc, offset, selected);
        std::printf("%s %u\n", tmap::resultName(result), unsigned(selected));
    }
    return std::ferror(stdin) ? 1 : 0;
}
