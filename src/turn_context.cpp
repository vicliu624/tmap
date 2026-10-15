#include "tmap/turn_context.h"
#include "binary.h"
#include <new>
namespace tmap
{
TurnContextReader::TurnContextReader(OutdoorPackage& package, uint64_t utc, int32_t offset)
    : package_(package), records_(package, SectionType::TurnRuleRecords), generation_(package.generation()), utc_(utc), offset_(offset) {}
bool TurnContextReader::valid() const { return package_.isOpen() && package_.generation() == generation_; }
bool TurnContextReader::validFor(const OutdoorPackage& package, uint64_t utc, int32_t offset) const
{
    return &package == &package_ && valid() && utc == utc_ && offset == offset_;
}
void TurnContextReader::cancel()
{
    if (rules_active_) tmap::cancel(scratch_.rules);
    else tmap::cancel(scratch_.descriptor);
    tmap::cancel(decision_);
    records_.discardPending();
    terminal_ = Result::Cancelled;
}
Result TurnContextReader::begin(uint32_t context, uint32_t outgoing, uint8_t mode, uint32_t edges, uint32_t maximum, uint32_t max_records)
{
    cancel();
    if (!valid()) return Result::Cancelled;
    CalendarPredicate clock;
    bool selected = false;
    const auto clock_result = evaluateCalendarPredicate(clock, utc_, offset_, selected);
    if (clock_result != Result::Ok) return terminal_ = clock_result;
    const auto* states = package_.section(SectionType::RouteHistoryStates);
    const auto* index = package_.section(SectionType::TurnRuleContextIndex);
    const auto* matches = package_.section(SectionType::TurnRuleContextMatches);
    const auto* relations = package_.section(SectionType::TurnRuleRelations);
    const auto* paths = package_.section(SectionType::TurnRuleEdges);
    const auto* programs = package_.section(SectionType::TurnRulePrograms);
    const auto* records = package_.section(SectionType::TurnRuleRecords);
    if (!states || !index || !matches || !relations || !paths || !programs || !records) return terminal_ = Result::Missing;
    if (!edges || edges >= UINT32_MAX - 1 || outgoing >= edges || !index->count || index->count > UINT32_MAX ||
        index->count != states->count || index->bytes != index->count * 16 || context >= index->count ||
        matches->count > UINT32_MAX || matches->bytes != matches->count * 16 ||
        !relations->count || relations->count > UINT32_MAX || relations->bytes != relations->count * 24 ||
        paths->count > UINT64_MAX / 4 || paths->bytes != paths->count * 4 ||
        !programs->count || programs->count > UINT32_MAX || programs->bytes != programs->count * 40 ||
        records->count > UINT64_MAX / 24 || records->bytes != records->count * 24) return terminal_ = Result::Invalid;
    const auto start = beginTurnDecision(mode, outgoing, decision_);
    if (start != Result::Ok) return terminal_ = start;
    context_ = context;
    edges_ = edges;
    maximum_ = maximum;
    max_records_ = max_records;
    stage_ = 0;
    ::new (static_cast<void*>(&scratch_.descriptor)) TurnProgramReadCursor{};
    rules_active_ = false;
    return terminal_ = Result::More;
}
Result TurnContextReader::step(size_t budget, bool& output)
{
    if (!budget) return Result::Invalid;
    if (!valid()) return terminal_ = Result::Cancelled;
    if (terminal_ != Result::More)
    {
        if (terminal_ == Result::Ok) return finishTurnDecision(decision_, output);
        return terminal_;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        Result result = Result::Invalid;
        if (stage_ == 0)
        {
            result = package_.record(SectionType::TurnRuleContextIndex, context_, 16, bytes_.data());
            if (result == Result::Ok)
            {
                first_ = detail::u64(bytes_.data());
                const auto count = detail::u32(bytes_.data() + 8);
                if (detail::u32(bytes_.data() + 12)) return terminal_ = Result::Invalid;
                if (count > maximum_) return terminal_ = Result::BudgetExceeded;
                const auto* matches = package_.section(SectionType::TurnRuleContextMatches);
                if (!detail::range(first_, count, matches->count)) return terminal_ = Result::Invalid;
                end_ = first_ + count;
                index_ = first_;
                stage_ = 1;
            }
        }
        else if (stage_ == 1)
        {
            if (index_ == end_)
            {
                if (!valid()) return terminal_ = Result::Cancelled;
                return terminal_ = finishTurnDecision(decision_, output);
            }
            result = package_.record(SectionType::TurnRuleContextMatches, index_, 16, bytes_.data());
            if (result == Result::Ok)
            {
                result = decodeTurnContextMatch(bytes_.data(), 16, package_.section(SectionType::TurnRuleRelations)->count, edges_, match_);
                if (result == Result::Ok) stage_ = 2;
            }
        }
        else if (stage_ == 2)
        {
            result = package_.record(SectionType::TurnRuleRelations, match_.relation, 24, bytes_.data());
            if (result == Result::Ok)
            {
                result = decodeTurnRelation(bytes_.data(), 24, package_.section(SectionType::TurnRuleEdges)->bytes,
                                            package_.section(SectionType::TurnRulePrograms)->count, relation_);
                if (result == Result::Ok) stage_ = 3;
            }
        }
        else if (stage_ == 3)
        {
            if (match_.prefix_length >= relation_.edges) return terminal_ = Result::Invalid;
            result = package_.read(SectionType::TurnRuleEdges, relation_.offset + uint64_t(match_.prefix_length) * 4, bytes_.data(), 4);
            if (result == Result::Ok)
            {
                result = validateTurnContextMatch(match_, relation_, detail::u32(bytes_.data()));
                if (result == Result::Ok)
                {
                    ::new (static_cast<void*>(&scratch_.descriptor)) TurnProgramReadCursor{};
                    rules_active_ = false;
                    result = beginTurnProgramRead(package_, relation_.program, scratch_.descriptor, max_records_);
                    if (result == Result::Ok) stage_ = 4;
                }
            }
        }
        else if (stage_ == 4)
        {
            result = turnProgramReadStep(package_, scratch_.descriptor, 1, programs_, program_mode_);
            if (result == Result::Ok)
            {
                ::new (static_cast<void*>(&scratch_.rules)) TurnRuleCursor{};
                rules_active_ = true;
                result = beginTurnRules(records_, programs_, utc_, offset_, scratch_.rules, max_records_, PackageSectionFile::epoch, &records_);
                if (result == Result::Ok) stage_ = 5;
            }
        }
        else if (stage_ == 5)
        {
            uint8_t action = 0;
            result = turnRulesStep(scratch_.rules, 1, action);
            if (result == Result::Ok)
            {
                result = turnDecisionMatch(decision_, relation_.group, program_mode_, match_.prefix_length, action, match_.next_edge, match_.complete);
                if (result == Result::More)
                {
                    ++index_;
                    stage_ = 1;
                    result = Result::Ok;
                }
            }
        }
        if (!valid()) return terminal_ = Result::Cancelled;
        if (result == Result::Busy) return result;
        if (result != Result::Ok && result != Result::More) return terminal_ = result;
    }
    return Result::More;
}
Result TurnContextReader::read(uint32_t context, uint32_t outgoing, uint8_t mode, uint32_t edges, bool& output,
                               uint32_t maximum, uint32_t max_records)
{
    if (context != context_ || outgoing != decision_.outgoing || mode != decision_.requested ||
        edges != edges_ || maximum != maximum_ || max_records != max_records_)
    {
        const auto result = begin(context, outgoing, mode, edges, maximum, max_records);
        if (result != Result::More) return result;
    }
    return step(1, output);
}
} // namespace tmap
