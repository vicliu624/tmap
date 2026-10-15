#include "tmap/edge_rules.h"
#include "binary.h"
#include <algorithm>
#include <new>
namespace tmap
{
EdgeRuleReader::EdgeRuleReader(OutdoorPackage& package, uint64_t utc, int32_t offset)
    : package_(package), records_(package, SectionType::RoadRuleRecords), generation_(package.generation()), utc_(utc), offset_(offset) {}
Result EdgeRuleReader::loadEdgeBytes(uint32_t row)
{
    if (!validFor(package_)) return Result::Cancelled;
    bool loaded;
    edgeBytes(row, loaded);
    if (loaded) return Result::Ok;
    const auto* section = package_.section(SectionType::Edges);
    if (!section || row >= section->count || section->bytes % 48 || section->count != section->bytes / 48) return Result::Invalid;
    const auto at = uint64_t(row) * 48 + raw_completed_;
    const size_t bytes = std::min<size_t>(48 - raw_completed_, package_.payloadBytes() - at % package_.payloadBytes());
    const auto result = package_.read(SectionType::Edges, at, raw_.data() + raw_completed_, bytes);
    if (result != Result::Ok) return result;
    raw_completed_ += static_cast<uint8_t>(bytes);
    if (raw_completed_ != 48) return Result::More;
    raw_loaded_ = true;
    return Result::Ok;
}
void EdgeRuleReader::cancel()
{
    if (rules_active_) tmap::cancel(scratch_.rules);
    else tmap::cancel(scratch_.descriptor);
    records_.discardPending();
    terminal_ = Result::Cancelled;
}
Result EdgeRuleReader::begin(uint32_t edge, uint8_t base, uint32_t maximum)
{
    cancel();
    if (!package_.isOpen() || package_.generation() != generation_) return Result::Cancelled;
    if (!base || base > 3) return terminal_ = Result::Invalid;
    const auto* refs = package_.section(SectionType::RoadRuleReferences);
    const auto* programs = package_.section(SectionType::RoadRulePrograms);
    const auto* records = package_.section(SectionType::RoadRuleRecords);
    if (!refs || !programs || !records) return terminal_ = Result::Missing;
    if (refs->count > UINT32_MAX || refs->bytes != refs->count * 4 || edge >= refs->count ||
        !programs->count || programs->count > UINT32_MAX || programs->bytes != programs->count * 120 ||
        records->count > UINT64_MAX / 24 || records->bytes != records->count * 24) return terminal_ = Result::Invalid;
    edge_ = edge;
    base_ = base;
    maximum_ = maximum;
    ::new (static_cast<void*>(&scratch_.descriptor)) RoadProgramReadCursor{};
    rules_active_ = false;
    barrier_program_ = false;
    barrier_index_ = 0;
    stage_ = 0;
    return terminal_ = Result::More;
}
Result EdgeRuleReader::step(size_t budget, uint8_t& output)
{
    if (!budget) return Result::Invalid;
    if (!package_.isOpen() || package_.generation() != generation_) return terminal_ = Result::Cancelled;
    if (terminal_ != Result::More)
    {
        if (terminal_ == Result::Ok) output = access_;
        return terminal_;
    }
    for (size_t work = 0; work < budget; ++work)
    {
        if (stage_ == 0)
        {
            const auto result = package_.record(SectionType::RoadRuleReferences, edge_, 4, reference_.data());
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return terminal_ = result;
            const auto row = detail::u32(reference_.data());
            if (row == UINT32_MAX)
            {
                access_ = base_;
                stage_ = 3;
                continue;
            }
            const auto start = beginRoadProgramRead(package_, row, scratch_.descriptor, maximum_);
            if (start != Result::Ok) return terminal_ = start;
            stage_ = 1;
        }
        else if (stage_ == 1)
        {
            const auto result = roadProgramReadStep(package_, scratch_.descriptor, 1, programs_, highway_, reversed_);
            if (result == Result::Busy) return result;
            if (result == Result::More) continue;
            if (result != Result::Ok) return terminal_ = result;
            if (barrier_program_ && reversed_) return terminal_ = Result::Invalid;
            if (barrier_program_)
                for (size_t i = 4; i < 7; ++i)
                    if (programs_[i].count || programs_[i].fallback != 1) return terminal_ = Result::Invalid;
            ::new (static_cast<void*>(&scratch_.rules)) RoadRuleCursor{};
            rules_active_ = true;
            stage_ = 2;
            const auto start = beginRoadRules(records_, programs_, utc_, offset_, highway_, 3, scratch_.rules, maximum_, PackageSectionFile::epoch, &records_);
            if (start != Result::Ok) return terminal_ = start;
            stage_ = 2;
        }
        else if (stage_ == 2)
        {
            RoadMasks masks;
            const auto result = roadRulesStep(scratch_.rules, 1, masks);
            if (result == Result::Busy) return result;
            if (result == Result::More) continue;
            if (result != Result::Ok) return terminal_ = result;
            if (barrier_program_)
            {
                if (masks.forward != masks.backward) return terminal_ = Result::Invalid;
                access_ &= masks.forward;
                ++barrier_index_;
                stage_ = 4;
            }
            else
            {
                access_ = base_ & (reversed_ ? masks.backward : masks.forward);
                stage_ = 3;
            }
        }
        else if (stage_ == 3)
        {
            const auto* barriers = package_.section(SectionType::BarrierRuleReferences);
            if (!barriers)
            {
                output = access_;
                return terminal_ = Result::Ok;
            }
            const auto* refs = package_.section(SectionType::RoadRuleReferences);
            if (barriers->count != refs->count || barriers->bytes != barriers->count * 8) return terminal_ = Result::Invalid;
            const auto result = package_.record(SectionType::BarrierRuleReferences, edge_, 8, reference_.data());
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return terminal_ = result;
            stage_ = 4;
        }
        else if (stage_ == 4)
        {
            if (barrier_index_ == 2)
            {
                output = access_;
                return terminal_ = Result::Ok;
            }
            const auto row = detail::u32(reference_.data() + 4 * barrier_index_);
            if (row == UINT32_MAX)
            {
                ++barrier_index_;
                continue;
            }
            ::new (static_cast<void*>(&scratch_.descriptor)) RoadProgramReadCursor{};
            rules_active_ = false;
            const auto start = beginRoadProgramRead(package_, row, scratch_.descriptor, maximum_);
            if (start != Result::Ok) return terminal_ = start;
            barrier_program_ = true;
            stage_ = 1;
        }
        else return terminal_ = Result::Invalid;
    }
    return Result::More;
}
Result EdgeRuleReader::read(uint32_t edge, uint8_t base, uint8_t& output)
{
    if (edge != edge_ || base != base_)
    {
        const auto result = begin(edge, base);
        if (result != Result::More) return result;
    }
    return step(1, output);
}
} // namespace tmap
