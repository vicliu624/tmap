#include "tmap/turn_rules.h"
#include "binary.h"
#include <new>
namespace tmap
{
namespace
{
bool valid(const TurnRuleCursor& c)
{
    return c.calendar.file && c.calendar.file->size() == c.bytes &&
           (!c.calendar.epoch || c.calendar.epoch(c.calendar.context) == c.initial_epoch);
}
Result start(TurnRuleCursor& c, RandomAccessFile& file, uint64_t utc, int32_t offset, FileEpoch epoch, void* context)
{
    const auto& p = c.programs[c.property];
    const auto result = beginCalendarRules(file, p.offset, p.count, utc, offset, p.fallback == 11 ? 0 : p.fallback,
                                           c.calendar, p.count, epoch, context, 10);
    // Inheritance is a validated fallback only, never a legal condition action.
    if (result == Result::Ok) c.calendar.value.value = p.fallback;
    return result;
}
} // namespace
Result decodeTurnRelation(const uint8_t* bytes, size_t size, uint64_t edge_bytes, uint64_t programs, TurnRelation& output)
{
    if (!bytes || size != 24 || !programs || programs > UINT32_MAX) return Result::Invalid;
    const auto offset = detail::u64(bytes), group = detail::u64(bytes + 16);
    const auto count = detail::u32(bytes + 8), program = detail::u32(bytes + 12);
    if (!group || count < 2 || count > 64 || program >= programs || offset % 4 ||
        !detail::range(offset, uint64_t(count) * 4, edge_bytes)) return Result::Invalid;
    output.offset = offset;
    output.group = group;
    output.edges = count;
    output.program = program;
    return Result::Ok;
}
Result decodeTurnContextMatch(const uint8_t* bytes, size_t size, uint64_t relations, uint32_t edges, TurnContextMatch& output)
{
    if (!bytes || size != 16 || !relations || relations > UINT32_MAX || !edges || edges >= UINT32_MAX - 1) return Result::Invalid;
    const auto relation = detail::u32(bytes), length = detail::u32(bytes + 4), next = detail::u32(bytes + 8), flags = detail::u32(bytes + 12);
    if (relation >= relations || !length || length > 63 || next >= edges || flags > 1) return Result::Invalid;
    output.relation = relation;
    output.prefix_length = length;
    output.next_edge = next;
    output.complete = flags != 0;
    return Result::Ok;
}
Result validateTurnContextMatch(const TurnContextMatch& match, const TurnRelation& relation, uint32_t expected)
{
    if (relation.edges < 2 || relation.edges > 64 || !match.prefix_length || match.prefix_length >= relation.edges ||
        match.complete != (match.prefix_length + 1 == relation.edges) || match.next_edge != expected || expected == UINT32_MAX)
        return Result::Invalid;
    return Result::Ok;
}
Result decodeTurnRuleProgram(const uint8_t* bytes, size_t size, uint64_t record_bytes,
                             std::array<RoadRuleRange, 2>& programs, uint8_t& mode, uint32_t maximum)
{
    if (!bytes || size != 40 || detail::u32(bytes) != 1 || (bytes[4] != 1 && bytes[4] != 2) ||
        bytes[5] || bytes[6] || bytes[7]) return Result::Invalid;
    uint64_t count = 0;
    for (size_t i = 0; i < 2; ++i)
    {
        const auto* p = bytes + 8 + 16 * i;
        const auto rows = detail::u32(p + 8), fallback = detail::u32(p + 12);
        count += rows;
        if (count > maximum) return Result::BudgetExceeded;
        if (fallback > (i ? 11u : 10u) || !detail::range(detail::u64(p), uint64_t(rows) * 24, record_bytes)) return Result::Invalid;
        if (!i && bytes[4] == 1 && (rows || fallback)) return Result::Invalid;
    }
    for (size_t i = 0; i < 2; ++i)
    {
        const auto* p = bytes + 8 + 16 * i;
        programs[i].offset = detail::u64(p);
        programs[i].count = detail::u32(p + 8);
        programs[i].fallback = detail::u32(p + 12);
    }
    mode = bytes[4];
    return Result::Ok;
}
Result beginTurnRules(RandomAccessFile& file, const std::array<RoadRuleRange, 2>& programs,
                      uint64_t utc, int32_t offset, TurnRuleCursor& c, uint32_t maximum, FileEpoch epoch, void* context)
{
    if (&programs == &c.programs) return Result::Invalid;
    ::new (static_cast<void*>(&c)) TurnRuleCursor{};
    c.bytes = file.size();
    uint64_t total = 0;
    for (size_t i = 0; i < 2; ++i)
    {
        const auto& p = programs[i];
        total += p.count;
        if (total > maximum) return Result::BudgetExceeded;
        if (p.fallback > (i ? 11u : 10u) || !detail::range(p.offset, uint64_t(p.count) * 24, c.bytes)) return Result::Invalid;
        c.programs[i] = p;
    }
    c.initial_epoch = epoch ? epoch(context) : 0;
    const auto result = start(c, file, utc, offset, epoch, context);
    if (result != Result::Ok) return result;
    if (!valid(c)) return c.terminal = Result::Cancelled;
    c.terminal = Result::More;
    return Result::Ok;
}
void cancel(TurnRuleCursor& c)
{
    c.terminal = Result::Cancelled;
    cancel(c.calendar);
}
Result turnRulesStep(TurnRuleCursor& c, size_t budget, uint8_t& output)
{
    if (!budget || !c.calendar.file) return Result::Invalid;
    if (!valid(c)) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More)
    {
        if (c.terminal == Result::Ok) output = c.value;
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
        if (!c.property)
        {
            c.general = value.value;
            c.property = 1;
            const auto next = start(c, *c.calendar.file, c.calendar.utc, c.calendar.local_offset, c.calendar.epoch, c.calendar.context);
            if (next != Result::Ok) return c.terminal = next;
            if (!valid(c)) return c.terminal = Result::Cancelled;
        }
        else
        {
            const auto resolved = resolveTurnProperty(c.general, value.value, c.value);
            if (!valid(c)) return c.terminal = Result::Cancelled;
            if (resolved == Result::Ok) output = c.value;
            return c.terminal = resolved;
        }
    }
    return Result::More;
}
Result beginTurnProgramRead(OutdoorPackage& package, uint32_t row, TurnProgramReadCursor& c, uint32_t maximum)
{
    ::new (static_cast<void*>(&c)) TurnProgramReadCursor{};
    const auto* programs = package.section(SectionType::TurnRulePrograms);
    const auto* records = package.section(SectionType::TurnRuleRecords);
    if (!programs || !records) return Result::Missing;
    if (programs->count > UINT32_MAX || programs->bytes != programs->count * 40 || row >= programs->count ||
        records->count > UINT64_MAX / 24 || records->bytes != records->count * 24) return Result::Invalid;
    c.record_bytes = records->bytes;
    c.max_records = maximum;
    const auto result = beginRead(package, SectionType::TurnRulePrograms, uint64_t(row) * 40, c.bytes.data(), c.bytes.size(), c.read);
    if (result == Result::Ok) c.terminal = Result::More;
    return result;
}
Result turnProgramReadStep(OutdoorPackage& package, TurnProgramReadCursor& c, size_t budget,
                           std::array<RoadRuleRange, 2>& programs, uint8_t& mode)
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
    return c.terminal = decodeTurnRuleProgram(c.bytes.data(), c.bytes.size(), c.record_bytes, programs, mode, c.max_records);
}
void cancel(TurnProgramReadCursor& c)
{
    cancelRead(c.read);
    c.terminal = Result::Cancelled;
}
Result beginTurnDecision(uint8_t mode, uint32_t outgoing, TurnDecisionCursor& c)
{
    ::new (static_cast<void*>(&c)) TurnDecisionCursor{};
    if ((mode != 1 && mode != 2) || outgoing == UINT32_MAX) return Result::Invalid;
    c.requested = mode;
    c.outgoing = outgoing;
    c.terminal = Result::More;
    return Result::Ok;
}
Result turnDecisionMatch(TurnDecisionCursor& c, uint64_t group, uint8_t mode, uint32_t length,
                         uint8_t action, uint32_t next_edge, bool complete)
{
    if (c.terminal != Result::More) return c.terminal == Result::Ok ? Result::Invalid : c.terminal;
    if (!group || (mode != 1 && mode != 2) || !length || length > 63 || action > 10 || next_edge == UINT32_MAX)
        return c.terminal = Result::Invalid;
    if (c.seen && (group < c.group || (group == c.group &&
                                       (mode < c.mode || (mode == c.mode && (length < c.prefix_length || action != c.action))))))
        return c.terminal = Result::Invalid;
    if (!c.seen || group != c.group || mode != c.mode || length != c.prefix_length)
    {
        if (c.seen && c.only && !c.matched) c.allowed = false;
        c.group = group;
        c.mode = mode;
        c.prefix_length = length;
        c.action = action;
        c.only = mode == c.requested && action >= 7;
        c.matched = false;
        c.seen = true;
    }
    if (mode == c.requested && action && action < 7 && complete && c.outgoing == next_edge) c.allowed = false;
    if (c.only && c.outgoing == next_edge) c.matched = true;
    return Result::More;
}
Result finishTurnDecision(TurnDecisionCursor& c, bool& output)
{
    if (c.terminal == Result::More)
    {
        if (c.seen && c.only && !c.matched) c.allowed = false;
        c.terminal = Result::Ok;
    }
    if (c.terminal == Result::Ok) output = c.allowed;
    return c.terminal;
}
void cancel(TurnDecisionCursor& c) { c.terminal = Result::Cancelled; }
} // namespace tmap
