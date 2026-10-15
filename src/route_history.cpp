#include "binary.h"
#include "tmap/navigation.h"
#include "tmap/turn_context.h"
namespace tmap
{
using namespace detail;
Result GraphReader::routeTransition(uint32_t incoming, uint32_t outgoing, TravelMode mode, bool& allowed, uint32_t& next)
{
    allowed = false;
    next = 0;
    if (!incoming) return Result::Invalid;
    if (!initialized_ || !package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    const auto* states = package_.section(SectionType::RouteHistoryStates);
    const auto* transitions = package_.section(SectionType::RouteHistoryTransitions);
    if (!(info_.history_modes & uint8_t(mode)))
    {
        const auto r = turnAllowed(routeEdge(incoming), outgoing, mode, allowed);
        if (r == Result::Ok && allowed) next = outgoing + 1;
        return r;
    }
    if (!states || !transitions || !states->count || !info_.edges || transitions->count > UINT32_MAX || states->bytes != states->count * 16 ||
        transitions->bytes != transitions->count * 16 || states->count > (UINT32_MAX - 1) / info_.edges ||
        outgoing >= info_.edges || (mode != TravelMode::Foot && mode != TravelMode::Bicycle)) return Result::Invalid;
    const auto context = incoming == UINT32_MAX ? 0 : (incoming - 1) / info_.edges;
    if (!incoming || context >= states->count) return Result::Invalid;
    auto& c = turn_;
    if (c.stage < 128 || c.incoming != incoming || c.outgoing != outgoing || c.mode != mode || c.generation != generation_)
    {
        const auto r = turnAllowed(routeEdge(incoming), outgoing, mode, allowed);
        if (r != Result::Ok || !allowed) return r;
        reset(c);
        c.incoming = incoming;
        c.outgoing = outgoing;
        c.mode = mode;
        c.generation = generation_;
        c.stage = 128;
    }
    if (c.result != Result::More)
    {
        allowed = c.allowed;
        if (c.result == Result::Ok && allowed) next = c.gap_end * info_.edges + outgoing + 1;
        return c.result;
    }
    for (size_t work = 0; work < 4; ++work)
    {
        auto* b = c.bytes.data();
        Result r;
        if (c.stage == 130)
        {
            if (!turnRules() || !validRuntime()) return c.result = Result::Incompatible;
            bool dynamic_allowed = false;
            r = turnRules()->read(context, outgoing, uint8_t(mode), info_.edges, dynamic_allowed);
            if (r == Result::Busy || r == Result::More) return r;
            if (r != Result::Ok) return c.result = r;
            c.allowed = c.allowed && dynamic_allowed;
            c.result = Result::Ok;
            allowed = c.allowed;
            if (allowed) next = c.gap_end * info_.edges + outgoing + 1;
            return Result::Ok;
        }
        else if (c.stage == 128)
        {
            r = package_.record(SectionType::RouteHistoryStates, context, 16, b);
            if (r != Result::Ok) return r;
            c.lo = u64(b);
            c.hi = c.lo + u32(b + 8);
            c.gap_begin = u32(b + 12);
            if (c.hi < c.lo || c.hi > transitions->count || (c.gap_begin & ~3U)) return c.result = Result::Invalid;
            c.gap_end = static_cast<uint32_t>(c.hi);
            c.stage = 129;
        }
        else if (c.stage == 129 && c.lo < c.hi)
        {
            const auto mid = c.lo + (c.hi - c.lo) / 2;
            r = package_.record(SectionType::RouteHistoryTransitions, mid, 16, b);
            if (r != Result::Ok) return r;
            if (u32(b) >= info_.edges) return c.result = Result::Invalid;
            if (u32(b) < outgoing) c.lo = mid + 1;
            else c.hi = mid;
        }
        else
        {
            // Preserve the range end across Busy; table pages share one slot.
            uint32_t next_context = 0;
            if (c.lo < c.gap_end)
            {
                r = package_.record(SectionType::RouteHistoryTransitions, c.lo, 16, b);
                if (r != Result::Ok) return r;
                if (u32(b) == outgoing)
                {
                    if (u32(b + 4) >= states->count || (u32(b + 8) & ~3U) || u32(b + 12)) return c.result = Result::Invalid;
                    next_context = u32(b + 4);
                    c.gap_begin = u32(b + 8);
                }
            }
            c.gap_end = next_context;
            c.allowed = (c.gap_begin & uint8_t(mode)) != 0;
            if (info_.runtime_turns)
            {
                c.stage = 130;
                continue;
            }
            c.result = Result::Ok;
            allowed = c.allowed;
            if (allowed) next = c.gap_end * info_.edges + outgoing + 1;
            return Result::Ok;
        }
    }
    return Result::More;
}
} // namespace tmap
