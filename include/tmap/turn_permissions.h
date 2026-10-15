#pragma once
#include "tmap/package.h"
namespace tmap
{
// 0=none, 1..6=no_left/right/straight/u-turn/entry/exit,
// 7..10=only_left/right/straight/u-turn. 11 is specific fallback inheritance.
// General restrictions apply to bicycles; foot uses general=0.
Result resolveTurnProperty(uint32_t general, uint32_t specific, uint8_t& output);
struct TurnDecisionCursor
{
    uint64_t group = 0;
    uint32_t prefix_length = 0, outgoing = UINT32_MAX;
    uint8_t mode = 0, action = 0, requested = 0;
    bool seen = false, only = false, matched = false, allowed = true;
    Result terminal = Result::Invalid;
};
// Feed sorted (group,mode,prefix_length) matches after their programs finish.
// Same group/mode must use one action. Only choices union within one prefix;
// separate groups/prefixes intersect. No rules reject only completed paths.
Result beginTurnDecision(uint8_t mode, uint32_t outgoing, TurnDecisionCursor& cursor);
Result turnDecisionMatch(TurnDecisionCursor& cursor, uint64_t group, uint8_t mode,
                         uint32_t prefix_length, uint8_t action, uint32_t next_edge, bool complete);
Result finishTurnDecision(TurnDecisionCursor& cursor, bool& allowed);
void cancel(TurnDecisionCursor& cursor);
} // namespace tmap
