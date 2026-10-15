#include "tmap/calendar_rules.h"
#include "binary.h"
#include "tmap/road_permissions.h"
#include "tmap/turn_permissions.h"
#include <new>
namespace tmap
{
namespace
{
bool valid(const CalendarRuleCursor& c)
{
    return c.file->size() == c.bytes && (!c.epoch || c.epoch(c.context) == c.initial_epoch);
}
} // namespace
Result beginCalendarRules(RandomAccessFile& file, uint64_t offset, uint32_t count, uint64_t utc,
                          int32_t local_offset, uint32_t fallback, CalendarRuleCursor& c,
                          uint32_t maximum, FileEpoch epoch, void* context, uint32_t max_value)
{
    ::new (static_cast<void*>(&c)) CalendarRuleCursor{};
    if (count > maximum) return Result::BudgetExceeded;
    if (fallback > max_value) return Result::Invalid;
    if (!detail::range(offset, uint64_t(count) * 24, file.size())) return Result::Invalid;
    bool selected = false;
    const auto r = evaluateCalendarPredicate(c.predicate, utc, local_offset, selected);
    if (r != Result::Ok) return r;
    c.file = &file;
    c.bytes = file.size();
    c.offset = offset;
    c.count = count;
    c.utc = utc;
    c.local_offset = local_offset;
    c.value.value = fallback;
    c.max_value = max_value;
    c.epoch = epoch;
    c.context = context;
    c.initial_epoch = epoch ? epoch(context) : 0;
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(CalendarRuleCursor& c) { c.terminal = Result::Cancelled; }
Result resolveTurnProperty(uint32_t general, uint32_t specific, uint8_t& output)
{
    if (general > 10 || specific > 11) return Result::Invalid;
    output = static_cast<uint8_t>(specific == 11 ? general : specific);
    return Result::Ok;
}
Result resolveRoadPermissions(const RoadPermissions& p, uint8_t highway, uint8_t requested, RoadMasks& output)
{
    if (highway > 3 || !requested || requested > 3 || uint8_t(p.access) > 2 || uint8_t(p.vehicle) > 2 ||
        uint8_t(p.foot) > 2 || uint8_t(p.bicycle) > 2 || uint8_t(p.oneway) > 3 ||
        uint8_t(p.bicycle_oneway) > 3 || uint8_t(p.foot_oneway) > 3) return Result::Invalid;
    const bool general = p.access != RoadAccess::Denied;
    const bool foot = p.foot == RoadAccess::Allowed ||
                      (p.foot == RoadAccess::Inherit && (highway & 1) && general);
    const bool bicycle = p.bicycle == RoadAccess::Allowed ||
                         (p.bicycle == RoadAccess::Inherit && (highway & 2) && general && p.vehicle != RoadAccess::Denied);
    const uint8_t modes = uint8_t((foot ? 1 : 0) | (bicycle ? 2 : 0)) & requested;
    const auto bike_direction = p.bicycle_oneway == RoadDirection::Inherit ? p.oneway : p.bicycle_oneway;
    RoadMasks value{modes, modes};
    if (p.foot_oneway == RoadDirection::Forward) value.backward &= uint8_t(~1u);
    if (p.foot_oneway == RoadDirection::Backward) value.forward &= uint8_t(~1u);
    if (bike_direction == RoadDirection::Forward) value.backward &= uint8_t(~2u);
    if (bike_direction == RoadDirection::Backward) value.forward &= uint8_t(~2u);
    output = value;
    return Result::Ok;
}
Result calendarRulesStep(CalendarRuleCursor& c, size_t budget, CalendarRuleResult& output)
{
    if (!budget || !c.file) return Result::Invalid;
    if (!valid(c)) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.value;
        return c.terminal;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.index == c.count)
        {
            if (!valid(c)) return c.terminal = Result::Cancelled;
            output = c.value;
            return c.terminal = Result::Ok;
        }
        const auto read = convert(c.file->readAt(c.offset + uint64_t(c.index) * 24, c.scratch.data(), 24));
        if (!valid(c)) return c.terminal = Result::Cancelled;
        if (read == Result::Busy || read == Result::More) return read;
        if (read != Result::Ok) return c.terminal = read;
        const auto* b = c.scratch.data();
        const auto ordinal = detail::u32(b + 16), value = detail::u32(b + 20);
        if (value > c.max_value) return c.terminal = Result::Invalid;
        if (c.index && (ordinal < c.ordinal || (ordinal == c.ordinal && value != c.clause_value))) return c.terminal = Result::Invalid;
        c.ordinal = ordinal;
        c.clause_value = value;
        auto& p = c.predicate;
        p.first_date = detail::u32(b);
        p.last_date = detail::u32(b + 4);
        p.months = detail::u16(b + 8);
        p.first_minute = detail::u16(b + 10);
        p.last_minute = detail::u16(b + 12);
        p.weekdays = b[14];
        p.date_kind = b[15];
        bool selected = false;
        const auto r = evaluateCalendarPredicate(p, c.utc, c.local_offset, selected);
        if (r != Result::Ok) return c.terminal = r;
        if (selected)
        {
            c.value.value = value;
            c.value.matched = true;
        }
        ++c.index;
    }
    return Result::More;
}
} // namespace tmap
