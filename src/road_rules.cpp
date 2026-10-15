#include "tmap/road_rules.h"
#include "binary.h"
#include <new>
namespace tmap
{
namespace
{
bool valid(const RoadRuleCursor& c)
{
    return c.calendar.file && c.calendar.file->size() == c.bytes &&
           (!c.calendar.epoch || c.calendar.epoch(c.calendar.context) == c.initial_epoch);
}
Result startProperty(RoadRuleCursor& c)
{
    const auto& p = c.programs[c.property];
    return beginCalendarRules(*c.calendar.file, p.offset, p.count, c.calendar.utc,
                              c.calendar.local_offset, p.fallback, c.calendar, p.count,
                              c.calendar.epoch, c.calendar.context, c.property < 4 ? 2 : 3);
}
} // namespace
Result decodeRoadRuleProgram(const uint8_t* bytes, size_t size, uint64_t record_bytes,
                             std::array<RoadRuleRange, 7>& programs, uint8_t& highway,
                             bool& reversed, uint32_t maximum)
{
    if (!bytes || size != 120 || detail::u32(bytes) != 1 || bytes[4] || bytes[5] > 3 || bytes[6] > 1 || bytes[7]) return Result::Invalid;
    uint64_t count = 0;
    for (size_t i = 0; i < 7; ++i)
    {
        const auto* p = bytes + 8 + 16 * i;
        const auto rows = detail::u32(p + 8), fallback = detail::u32(p + 12);
        count += rows;
        if (count > maximum) return Result::BudgetExceeded;
        if (fallback > (i < 4 ? 2u : 3u) || !detail::range(detail::u64(p), uint64_t(rows) * 24, record_bytes)) return Result::Invalid;
    }
    for (size_t i = 0; i < 7; ++i)
    {
        const auto* p = bytes + 8 + 16 * i;
        programs[i].offset = detail::u64(p);
        programs[i].count = detail::u32(p + 8);
        programs[i].fallback = detail::u32(p + 12);
    }
    highway = bytes[5];
    reversed = bytes[6] != 0;
    return Result::Ok;
}
Result beginRoadRules(RandomAccessFile& file, const std::array<RoadRuleRange, 7>& programs,
                      uint64_t utc, int32_t offset, uint8_t highway, uint8_t requested,
                      RoadRuleCursor& c, uint32_t maximum, FileEpoch epoch, void* context)
{
    if (&programs == &c.programs) return Result::Invalid;
    ::new (static_cast<void*>(&c)) RoadRuleCursor{};
    if (highway > 3 || !requested || requested > 3) return Result::Invalid;
    uint64_t total = 0;
    c.bytes = file.size();
    for (size_t i = 0; i < programs.size(); ++i)
    {
        const auto& p = programs[i];
        total += p.count;
        if (total > maximum) return Result::BudgetExceeded;
        if (p.fallback > (i < 4 ? 2u : 3u) || !detail::range(p.offset, uint64_t(p.count) * 24, c.bytes)) return Result::Invalid;
        c.programs[i] = p;
    }
    c.initial_epoch = epoch ? epoch(context) : 0;
    c.highway = highway;
    c.requested = requested;
    const auto& p = c.programs[0];
    const auto result = beginCalendarRules(file, p.offset, p.count, utc, offset, p.fallback,
                                           c.calendar, p.count, epoch, context, 2);
    if (result != Result::Ok) return result;
    if (!valid(c)) return c.terminal = Result::Cancelled;
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(RoadRuleCursor& c)
{
    c.terminal = Result::Cancelled;
    cancel(c.calendar);
}
Result roadRulesStep(RoadRuleCursor& c, size_t budget, RoadMasks& output)
{
    if (!budget || !c.calendar.file) return Result::Invalid;
    if (!valid(c)) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.masks;
        return c.terminal;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        CalendarRuleResult value;
        const auto result = calendarRulesStep(c.calendar, 1, value);
        if (!valid(c)) return c.terminal = Result::Cancelled;
        if (result == Result::Busy) return result;
        if (result == Result::More) continue;
        if (result != Result::Ok) return c.terminal = result;
        switch (c.property)
        {
        case 0:
            c.permissions.access = static_cast<RoadAccess>(value.value);
            break;
        case 1:
            c.permissions.vehicle = static_cast<RoadAccess>(value.value);
            break;
        case 2:
            c.permissions.foot = static_cast<RoadAccess>(value.value);
            break;
        case 3:
            c.permissions.bicycle = static_cast<RoadAccess>(value.value);
            break;
        case 4:
            c.permissions.oneway = static_cast<RoadDirection>(value.value);
            break;
        case 5:
            c.permissions.bicycle_oneway = static_cast<RoadDirection>(value.value);
            break;
        case 6:
            c.permissions.foot_oneway = static_cast<RoadDirection>(value.value);
            break;
        default:
            return c.terminal = Result::Invalid;
        }
        if (++c.property == 7)
        {
            const auto resolved = resolveRoadPermissions(c.permissions, c.highway, c.requested, c.masks);
            if (!valid(c)) return c.terminal = Result::Cancelled;
            if (resolved == Result::Ok) output = c.masks;
            return c.terminal = resolved;
        }
        const auto start = startProperty(c);
        if (start != Result::Ok) return c.terminal = start;
        if (!valid(c)) return c.terminal = Result::Cancelled;
    }
    return Result::More;
}
Result beginRoadProgramRead(OutdoorPackage& package, uint32_t row, RoadProgramReadCursor& c, uint32_t maximum)
{
    ::new (static_cast<void*>(&c)) RoadProgramReadCursor{};
    const auto* programs = package.section(SectionType::RoadRulePrograms);
    const auto* records = package.section(SectionType::RoadRuleRecords);
    if (!programs || !records) return Result::Missing;
    if (programs->count > UINT32_MAX || programs->bytes != programs->count * 120 ||
        records->count > UINT64_MAX / 24 || records->bytes != records->count * 24 || row >= programs->count) return Result::Invalid;
    c.record_bytes = records->bytes;
    c.max_records = maximum;
    const auto result = beginRead(package, SectionType::RoadRulePrograms, uint64_t(row) * 120, c.bytes.data(), c.bytes.size(), c.read);
    if (result == Result::Ok) c.terminal = Result::More;
    return result;
}
Result roadProgramReadStep(OutdoorPackage& package, RoadProgramReadCursor& c, size_t budget,
                           std::array<RoadRuleRange, 7>& programs, uint8_t& highway, bool& reversed)
{
    if (!budget) return Result::Invalid;
    if (c.terminal != Result::More && c.terminal != Result::Ok) return c.terminal;
    if (!package.isOpen() || package.generation() != c.read.generation) return c.terminal = Result::Cancelled;
    if (c.terminal == Result::More)
    {
        const auto result = readStep(package, c.read, budget);
        if (result == Result::Busy || result == Result::More) return result;
        if (result != Result::Ok) return c.terminal = result;
    }
    return c.terminal = decodeRoadRuleProgram(c.bytes.data(), c.bytes.size(), c.record_bytes, programs, highway, reversed, c.max_records);
}
void cancel(RoadProgramReadCursor& c)
{
    cancelRead(c.read);
    c.terminal = Result::Cancelled;
}
} // namespace tmap
