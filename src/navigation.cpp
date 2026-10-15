#include "tmap/navigation.h"
#include "binary.h"
#include "tmap/coordinate_route.h"
#include "tmap/edge_rules.h"
#include "tmap/routing_profile.h"
#include "tmap/routing_rules.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace tmap
{
using namespace detail;
namespace
{
bool validEdge(const GraphInfo& info, const OutdoorSection* shapes, const uint8_t* b)
{
    const auto flags = u16(b + 10);
    const auto points = u32(b + 32);
    return u32(b) < info.nodes && u32(b + 4) < info.nodes && u32(b + 12) && b[8] && !(b[8] & ~3U) && !(flags & ~3U) &&
           ((flags & 2U) || (!u32(b + 16) && !u32(b + 20))) && points >= 2 && points <= 1000000 && shapes &&
           range(u64(b + 24), uint64_t(points) * 8, shapes->bytes) && (u32(b + 36) == UINT32_MAX || u32(b + 36) < info.edges);
}
} // namespace
EdgeRuleReader* GraphReader::edgeRules() const { return rules_binding_ ? runtime_.binding->roads : runtime_.edge; }
TurnContextReader* GraphReader::turnRules() const { return rules_binding_ ? runtime_.binding->turns : nullptr; }
bool GraphReader::validRuntime() const
{
    return rules_binding_ ? runtime_.binding->validFor(package_) : runtime_.edge && runtime_.edge->validFor(package_);
}
Result GraphReader::validateRouting(TravelMode mode)
{
    if (!package_.isOpen()) return Result::Invalid;
    if ((info_.runtime_roads || info_.runtime_turns) && !validRuntime()) return Result::Incompatible;
    if (routing_profile_generation_ != package_.generation())
    {
        auto result = readRoutingProfile(package_, routing_profile_);
        if (result == Result::Missing)
        {
            routing_profile_.travel_modes = 0;
        }
        else if (result != Result::Ok) return result;
        routing_profile_generation_ = package_.generation();
    }
    if (!routing_profile_.travel_modes) return Result::Ok;
    uint64_t seconds = 0;
    int32_t offset = 0;
    if (routing_profile_.conditional_snapshot && (!routing_clock_ || !routing_clock_(routing_clock_context_, seconds, offset)))
        return Result::Incompatible;
    return checkRoutingProfile(routing_profile_, seconds, offset, static_cast<uint32_t>(mode));
}
Result GraphReader::initialize()
{
    if (initialized_ && package_.isOpen() && generation_ == package_.generation()) return Result::Ok;
    initialized_ = false;
    uint8_t meta[32]{};
    auto r = package_.read(SectionType::Graph, 0, meta, sizeof(meta));
    if (r != Result::Ok) return r;
    GraphInfo pending{u32(meta), u32(meta + 4), {}, (u32(meta + 12) & 1U) != 0};
    pending.history_modes = static_cast<uint8_t>((u32(meta + 8) >> 8) & 3U);
    pending.runtime_roads = (u32(meta + 8) & 65536U) != 0;
    pending.runtime_barriers = (u32(meta + 8) & 131072U) != 0;
    pending.runtime_turns = (u32(meta + 8) & 262144U) != 0;
    std::copy(meta + 16, meta + 32, pending.topology.begin());
    const auto* nodes = package_.section(SectionType::Nodes);
    const auto* edges = package_.section(SectionType::Edges);
    const auto* restrictions = package_.section(SectionType::Restrictions);
    const auto* terrain_index = package_.section(SectionType::EdgeTerrainIndex);
    const auto* terrain_data = package_.section(SectionType::EdgeTerrainData);
    const auto* terrain_policy = package_.section(SectionType::EdgeTerrainPolicy);
    const auto* history = package_.section(SectionType::RouteHistoryStates);
    const auto* transitions = package_.section(SectionType::RouteHistoryTransitions);
    const auto* rule_refs = package_.section(SectionType::RoadRuleReferences);
    const auto* rule_programs = package_.section(SectionType::RoadRulePrograms);
    const auto* rule_records = package_.section(SectionType::RoadRuleRecords);
    const auto* barrier_refs = package_.section(SectionType::BarrierRuleReferences);
    if (barrier_refs || pending.runtime_barriers)
    {
        if (!pending.runtime_barriers || !pending.runtime_roads || !barrier_refs ||
            barrier_refs->count != pending.edges || barrier_refs->bytes != uint64_t(pending.edges) * 8) return Result::Invalid;
    }
    if (rule_refs || rule_programs || rule_records || pending.runtime_roads)
    {
        if (!pending.runtime_roads || !rule_refs || !rule_programs || !rule_records ||
            rule_refs->count != pending.edges || rule_refs->bytes != uint64_t(pending.edges) * 4 ||
            !rule_programs->count || rule_programs->count > UINT32_MAX || rule_programs->bytes != rule_programs->count * 120 ||
            rule_records->count > UINT64_MAX / 24 || rule_records->bytes != rule_records->count * 24) return Result::Invalid;
        if (!edgeRules()) return Result::Unsupported;
        if (!validRuntime()) return Result::Incompatible;
    }
    if ((history || transitions || pending.history_modes) && (!history || !transitions || !pending.history_modes || !history->count || !pending.edges ||
                                                              history->count > (UINT32_MAX - 1) / pending.edges || transitions->count > UINT32_MAX ||
                                                              history->bytes != history->count * 16 || transitions->bytes != transitions->count * 16)) return Result::Invalid;
    if ((terrain_index || terrain_data || terrain_policy) &&
        (!terrain_index || !terrain_data || !terrain_policy || terrain_index->count != pending.edges ||
         terrain_index->bytes != uint64_t(pending.edges) * 16 || terrain_data->bytes % 12 || terrain_policy->bytes != 32)) return Result::Invalid;
    const auto* turn_programs = package_.section(SectionType::TurnRulePrograms);
    const auto* turn_records = package_.section(SectionType::TurnRuleRecords);
    const auto* turn_relations = package_.section(SectionType::TurnRuleRelations);
    const auto* turn_edges = package_.section(SectionType::TurnRuleEdges);
    const auto* turn_index = package_.section(SectionType::TurnRuleContextIndex);
    const auto* turn_matches = package_.section(SectionType::TurnRuleContextMatches);
    if (pending.runtime_turns || turn_programs || turn_records || turn_relations || turn_edges || turn_index || turn_matches)
    {
        if (!pending.runtime_turns || !pending.history_modes || !history || !turn_programs || !turn_records || !turn_relations || !turn_edges || !turn_index || !turn_matches ||
            !turn_programs->count || turn_programs->count > UINT32_MAX || turn_programs->bytes != turn_programs->count * 40 ||
            turn_records->count > UINT64_MAX / 24 || turn_records->bytes != turn_records->count * 24 ||
            !turn_relations->count || turn_relations->count > UINT32_MAX || turn_relations->bytes != turn_relations->count * 24 ||
            turn_edges->count > UINT64_MAX / 4 || turn_edges->bytes != turn_edges->count * 4 ||
            turn_index->count != history->count || turn_index->bytes != turn_index->count * 16 ||
            turn_matches->count > UINT32_MAX || turn_matches->bytes != turn_matches->count * 16) return Result::Invalid;
        if (!turnRules()) return Result::Unsupported;
        if (!validRuntime()) return Result::Incompatible;
    }
    if (!pending.nodes || pending.edges >= UINT32_MAX - 1 || (u32(meta + 8) & 3U) != 3 || (u32(meta + 8) & ~UINT32_C(0x70303)) || (u32(meta + 12) & ~1U) || !nodes || !edges || !restrictions ||
        nodes->count != pending.nodes || nodes->bytes != uint64_t(pending.nodes) * 32 ||
        edges->count != pending.edges || edges->bytes != uint64_t(pending.edges) * 48 ||
        restrictions->bytes != restrictions->count * 16) return Result::Invalid;
    info_ = pending;
    generation_ = package_.generation();
    initialized_ = true;
    ascent_.raw_edge = UINT32_MAX;
    ascent_.read_completed = 0;
    restriction_gap_begin_ = restriction_gap_end_ = 0;
    return Result::Ok;
}
Result graphCompatibility(GraphReader& a, GraphReader& b)
{
    auto r = a.initialize();
    if (r != Result::Ok) return r;
    r = b.initialize();
    if (r != Result::Ok) return r;
    return a.info().topology_declared && b.info().topology_declared && a.info().topology == b.info().topology ? Result::Ok : Result::Incompatible;
}
Result GraphReader::node(uint32_t row, GraphNode& output)
{
    if (!initialized_)
    {
        auto r = initialize();
        if (r != Result::Ok) return r;
    }
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    uint8_t b[32]{};
    auto r = package_.record(SectionType::Nodes, row, 32, b);
    if (r != Result::Ok) return r;
    GraphNode n{u64(b), {i32(b + 8), i32(b + 12)}, u32(b + 16), u32(b + 20), i32(b + 24), (u32(b + 28) & 1U) != 0, (u32(b + 28) & 2U) != 0};
    if (!n.id || !coordinate(n.point.latitude, n.point.longitude) || !range(n.first_edge, n.edge_count, info_.edges) || (u32(b + 28) & ~3U)) return Result::Invalid;
    output = n;
    return Result::Ok;
}
Result GraphReader::loadRawEdge(uint32_t row)
{
    if (row >= info_.edges) return Result::Invalid;
    if (ascent_.raw_edge != row)
    {
        ascent_.raw_edge = row;
        ascent_.read_completed = 0;
    }
    return fixedFragment(package_, SectionType::Edges, uint64_t(row) * 48, ascent_.bytes.data(), 48, ascent_.read_completed);
}
Result GraphReader::edge(uint32_t row, GraphEdge& output)
{
    if (!initialized_)
    {
        auto r = initialize();
        if (r != Result::Ok) return r;
    }
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    uint8_t* b = ascent_.bytes.data();
    bool loaded = false;
    if (info_.runtime_roads)
    {
        if (!edgeRules() || !validRuntime()) return Result::Incompatible;
        b = edgeRules()->edgeBytes(row, loaded);
    }
    auto r = Result::Ok;
    if (!loaded)
    {
        r = info_.runtime_roads ? edgeRules()->loadEdgeBytes(row) : loadRawEdge(row);
        if (r != Result::Ok) return r;
        if (info_.runtime_roads) edgeRules()->markEdgeLoaded();
    }
    const auto* shapes = package_.section(SectionType::Shapes);
    if (!validEdge(info_, shapes, b)) return Result::Invalid;
    uint8_t access = b[8];
    if (info_.runtime_roads)
    {
        if (!edgeRules() || !validRuntime()) return Result::Incompatible;
        r = edgeRules()->read(row, access, access);
        if (r != Result::Ok) return r;
    }
    output.from = u32(b);
    output.to = u32(b + 4);
    output.length_cm = u32(b + 12);
    output.ascent_cm = u32(b + 16);
    output.descent_cm = u32(b + 20);
    output.shape_offset = u64(b + 24);
    output.name_id = u64(b + 40);
    output.points = u32(b + 32);
    output.opposite = u32(b + 36);
    output.access = access;
    output.road_class = b[9];
    output.reversed = (u16(b + 10) & 1U) != 0;
    output.has_elevation = (u16(b + 10) & 2U) != 0;
    return Result::Ok;
}
Result GraphReader::point(const GraphEdge& e, uint32_t index, GeoPoint& output)
{
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    if (index >= e.points) return Result::Invalid;
    uint8_t b[8]{};
    const auto r = package_.read(SectionType::Shapes, e.shape_offset + uint64_t(e.reversed ? e.points - 1 - index : index) * 8, b, 8);
    if (r != Result::Ok) return r;
    GeoPoint p{i32(b), i32(b + 4)};
    if (!coordinate(p.latitude, p.longitude)) return Result::Invalid;
    output = p;
    return Result::Ok;
}
Result GraphReader::turnAllowed(uint32_t incoming, uint32_t outgoing, TravelMode mode, bool& allowed)
{
    allowed = false;
    if (!initialized_)
    {
        const auto r = initialize();
        if (r != Result::Ok) return r;
    }
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    if ((mode != TravelMode::Foot && mode != TravelMode::Bicycle) || outgoing >= info_.edges) return Result::Invalid;
    if (incoming == UINT32_MAX)
    {
        allowed = true;
        return Result::Ok;
    }
    const auto* section = package_.section(SectionType::Restrictions);
    if (!section || incoming >= info_.edges || outgoing >= info_.edges) return Result::Invalid;
    // Cache a proven empty interval of incoming-edge keys, not whole table pages.
    // This avoids rereading a sparse restriction table for every road node.
    if (incoming >= restriction_gap_begin_ && incoming < restriction_gap_end_)
    {
        allowed = true;
        return Result::Ok;
    }
    auto& c = turn_;
    if (c.stage >= 128 || c.incoming != incoming || c.outgoing != outgoing || c.mode != mode || c.generation != package_.generation())
    {
        reset(c);
        c.incoming = incoming;
        c.outgoing = outgoing;
        c.mode = mode;
        c.generation = package_.generation();
        c.hi = section->count;
    }
    if (c.result != Result::More)
    {
        allowed = c.allowed;
        return c.result;
    }
    for (size_t work = 0; work < 64; ++work)
    {
        auto* b = c.bytes.data();
        if (!c.stage)
        {
            if (c.lo == c.hi)
            {
                if (incoming >= c.gap_begin && incoming < c.gap_end)
                {
                    restriction_gap_begin_ = c.gap_begin;
                    restriction_gap_end_ = c.gap_end;
                    c.allowed = true;
                    c.result = Result::Ok;
                    allowed = true;
                    return Result::Ok;
                }
                c.stage = 1;
                continue;
            }
            const auto mid = c.lo + (c.hi - c.lo) / 2;
            const auto r = package_.record(SectionType::Restrictions, mid, 16, b);
            if (r != Result::Ok) return r;
            if (u32(b) < incoming)
            {
                c.lo = mid + 1;
                c.gap_begin = u32(b) + 1;
            }
            else
            {
                c.hi = mid;
                c.gap_end = u32(b);
            }
        }
        else
        {
            if (c.lo == section->count)
            {
                c.allowed = true;
                c.result = Result::Ok;
                allowed = true;
                return Result::Ok;
            }
            const auto r = package_.record(SectionType::Restrictions, c.lo, 16, b);
            if (r != Result::Ok) return r;
            if (u32(b) != incoming)
            {
                c.allowed = true;
                c.result = Result::Ok;
                allowed = true;
                return Result::Ok;
            }
            if (c.scanned >= 64 || u32(b + 4) >= info_.edges || !b[8] || (b[8] & ~3U) || b[9] > 1 || u16(b + 10) || u32(b + 12)) return Result::Invalid;
            if ((b[8] & static_cast<uint8_t>(mode)) && ((!b[9] && u32(b + 4) == outgoing) || (b[9] && u32(b + 4) != outgoing)))
            {
                c.result = Result::Ok;
                c.allowed = false;
                return Result::Ok;
            }
            ++c.scanned;
            ++c.lo;
        }
    }
    return Result::More;
}
Result GraphReader::roadName(uint64_t reference, char* output, size_t capacity)
{
    if (!output || !capacity) return Result::Invalid;
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    if (!reference)
    {
        output[0] = 0;
        return Result::Missing;
    }
    uint8_t length[2]{};
    auto r = package_.read(SectionType::RoadNames, reference - 1, length, 2);
    if (r != Result::Ok) return r;
    const auto n = u16(length);
    if (!n || n > 512) return Result::Invalid;
    if (n >= capacity) return Result::BudgetExceeded;
    r = package_.read(SectionType::RoadNames, reference + 1, reinterpret_cast<uint8_t*>(output), n);
    if (r == Result::Ok) output[n] = 0;
    return r;
}
Result GraphReader::ascentPrefix(uint32_t edge_row, double fraction, double& output)
{
    return elevationPrefix(edge_row, fraction, false, output);
}
Result GraphReader::descentPrefix(uint32_t edge_row, double fraction, double& output)
{
    return elevationPrefix(edge_row, fraction, true, output);
}
Result GraphReader::prefixRead(SectionType type, uint64_t offset, size_t bytes)
{
    const auto result = fixedFragment(package_, type, offset, ascent_.bytes.data(), bytes, ascent_.read_completed);
    if (result == Result::Ok) ascent_.read_completed = 0;
    return result;
}
Result GraphReader::elevationPrefix(uint32_t edge_row, double fraction, bool descending, double& output)
{
    if (!std::isfinite(fraction) || fraction < 0 || fraction > 1) return Result::Invalid;
    if (!initialized_)
    {
        const auto r = initialize();
        if (r != Result::Ok) return r;
    }
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    auto& c = ascent_;
    if (c.edge != edge_row || c.fraction != fraction || c.generation != package_.generation() || c.descending != descending)
    {
        reset(c);
        c.edge = edge_row;
        c.fraction = fraction;
        c.generation = package_.generation();
        c.descending = descending;
    }
    if (!package_.isOpen()) return Result::Cancelled;
    if (c.result != Result::More)
    {
        if (c.result == Result::Ok) output = c.value;
        return c.result;
    }
    for (size_t work = 0; work < 64; ++work)
    {
        auto* b = c.bytes.data();
        Result r;
        if (!c.stage)
        {
            c.raw_edge = UINT32_MAX;
            r = prefixRead(SectionType::Edges, uint64_t(edge_row) * 48, 48);
            if (r != Result::Ok) return r;
            if (!validEdge(info_, package_.section(SectionType::Shapes), b)) return Result::Invalid;
            const bool known = (u16(b + 10) & 2U) != 0;
            if (!fraction || fraction == 1)
            {
                c.result = !fraction || known ? Result::Ok : Result::Incomplete;
                c.value = fraction * u32(b + (descending ? 20 : 16)) / 100.0;
                if (c.result == Result::Ok) output = c.value;
                return c.result;
            }
            if (!known)
            {
                c.result = Result::Incomplete;
                return c.result;
            }
            c.length_cm = u32(b + 12);
            c.ascent_cm = u32(b + 16);
            c.descent_cm = u32(b + 20);
            c.stage = 1;
        }
        else if (c.stage == 1)
        {
            r = prefixRead(SectionType::EdgeTerrainIndex, uint64_t(edge_row) * 16, 16);
            if (r == Result::Missing) return Result::Incomplete;
            if (r != Result::Ok) return r;
            const auto* data = package_.section(SectionType::EdgeTerrainData);
            c.offset = u64(b);
            c.count = u32(b + 8);
            if (!c.count) return Result::Incomplete;
            if (c.count < 2 || !data || u32(b + 12) || !range(c.offset, uint64_t(c.count) * 12, data->bytes)) return Result::Invalid;
            c.stage = 2;
        }
        else if (c.stage == 2 || c.stage == 3)
        {
            r = prefixRead(SectionType::EdgeTerrainData, c.offset + (c.stage == 2 ? 0 : uint64_t(c.count - 1) * 12), 12);
            if (r != Result::Ok) return r;
            if (c.stage == 2 && (u32(b) || u32(b + 4) || u32(b + 8))) return Result::Invalid;
            if (c.stage == 3 && (u32(b) != c.length_cm || u32(b + 4) != c.ascent_cm || u32(b + 8) != c.descent_cm)) return Result::Invalid;
            if (++c.stage == 4)
            {
                c.lo = 1;
                c.hi = c.count - 1;
            }
        }
        else if (c.stage == 4)
        {
            if (c.lo == c.hi)
            {
                c.stage = 5;
                continue;
            }
            const auto mid = c.lo + (c.hi - c.lo) / 2;
            r = prefixRead(SectionType::EdgeTerrainData, c.offset + uint64_t(mid) * 12, 12);
            if (r != Result::Ok) return r;
            if (u32(b) < fraction * c.length_cm) c.lo = mid + 1;
            else c.hi = mid;
        }
        else if (c.stage == 5)
        {
            r = prefixRead(SectionType::EdgeTerrainData, c.offset + uint64_t(c.lo - 1) * 12, 12);
            if (r != Result::Ok) return r;
            c.lower_distance = u32(b);
            c.lower_ascent = u32(b + 4);
            c.lower_descent = u32(b + 8);
            c.stage = 6;
        }
        else
        {
            r = prefixRead(SectionType::EdgeTerrainData, c.offset + uint64_t(c.lo) * 12, 12);
            if (r != Result::Ok) return r;
            const double target = fraction * c.length_cm;
            if (c.lower_distance >= u32(b) || c.lower_distance > target || u32(b) < target ||
                c.lower_ascent > u32(b + 4) || u32(b + 4) > c.ascent_cm || c.lower_descent > u32(b + 8) ||
                u32(b + 8) > c.descent_cm) return Result::Invalid;
            const auto lower = descending ? c.lower_descent : c.lower_ascent;
            const auto upper = u32(b + (descending ? 8 : 4));
            c.value = (lower + (upper - lower) * (target - c.lower_distance) / double(u32(b) - c.lower_distance)) / 100.0;
            c.result = Result::Ok;
            output = c.value;
            return Result::Ok;
        }
    }
    return Result::More;
}
namespace
{
double closest(GeoPoint fix, GeoPoint a, GeoPoint b, double& fraction, GeoPoint& matched)
{
    const float scale = std::cos(float(fix.latitude) * 1e-7f * 3.14159265358979323846f / 180);
    const float ax = float(int64_t(a.longitude) - fix.longitude) * scale, ay = float(int64_t(a.latitude) - fix.latitude);
    const float dx = float(int64_t(b.longitude) - a.longitude) * scale, dy = float(int64_t(b.latitude) - a.latitude);
    const float d = dx * dx + dy * dy;
    const float t = d > 0 ? std::clamp(-(ax * dx + ay * dy) / d, 0.0f, 1.0f) : 0;
    fraction = t;
    matched = {a.latitude + int32_t(std::lround(float(int64_t(b.latitude) - a.latitude) * t)),
               a.longitude + int32_t(std::lround(float(int64_t(b.longitude) - a.longitude) * t))};
    return distanceMeters(fix, matched);
}
bool validMode(TravelMode mode) { return mode == TravelMode::Foot || mode == TravelMode::Bicycle; }
} // namespace
Result beginSnap(GraphReader& graph, GeoPoint point, TravelMode mode, double max_distance_m, SnapCursor& c)
{
    reset(c);
    if (!validMode(mode) || !coordinate(point.latitude, point.longitude) || !std::isfinite(max_distance_m) || max_distance_m <= 0 || max_distance_m > 10000) return Result::Invalid;
    auto r = graph.initialize();
    if (r != Result::Ok) return r;
    r = graph.validateRouting(mode);
    if (r != Result::Ok) return r;
    const auto* tree = graph.package().section(SectionType::EdgeSpatial);
    if (!tree || tree->count > UINT32_MAX || tree->bytes != tree->count * 48) return Result::Invalid;
    if (!tree->count) return Result::Missing;
    const float dy = (float(max_distance_m) + 1.0f) / 111000.0f * 1e7f;
    const float dx = dy / std::max(0.001f, std::cos(float(point.latitude) * 1e-7f * 3.14159265358979323846f / 180));
    const auto ix = int64_t(std::ceil(dx)), iy = int64_t(std::ceil(dy));
    c.bounds = {int32_t(std::max<int64_t>(-1800000000, int64_t(point.longitude) - ix)), int32_t(std::max<int64_t>(-900000000, int64_t(point.latitude) - iy)),
                int32_t(std::min<int64_t>(1800000000, int64_t(point.longitude) + ix)), int32_t(std::min<int64_t>(900000000, int64_t(point.latitude) + iy))};
    c.point = point;
    c.mode = mode;
    c.max_distance_m = max_distance_m;
    c.best.distance_m = max_distance_m;
    c.depth = 1;
    c.generation = graph.package().generation();
    c.complete = false;
    return Result::Ok;
}
Result snapStep(GraphReader& graph, SnapCursor& c, size_t budget)
{
    auto& package = graph.package();
    if (!budget) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!package.isOpen() || c.generation != package.generation()) return Result::Cancelled;
    if (c.complete) return c.best.edge == UINT32_MAX ? Result::Missing : Result::Ok;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.candidate_id != UINT32_MAX && !c.edge_pending)
        {
            GeoPoint a{}, b{};
            auto r = Result::Ok;
            if (!c.point_pending)
            {
                r = graph.point(c.candidate, c.segment, a);
                if (r != Result::Ok) return r;
                std::memcpy(c.read_bytes.data(), &a, sizeof(a));
                c.point_pending = true;
            }
            std::memcpy(&a, c.read_bytes.data(), sizeof(a));
            r = graph.point(c.candidate, c.segment + 1, b);
            if (r != Result::Ok) return r;
            c.point_pending = false;
            double t = 0;
            GeoPoint p{};
            const double d = closest(c.point, a, b, t, p);
            const double length = distanceMeters(a, b);
            if (d <= c.best.distance_m && (d < c.best.distance_m || c.candidate_id < c.best.edge))
            {
                c.best = {p, c.candidate_id, c.segment, t, d, 0};
                c.best_along_m = c.candidate_length_m + t * length;
            }
            c.candidate_length_m += length;
            if (++c.segment + 1 == c.candidate.points)
            {
                if (c.best.edge == c.candidate_id) c.best.along_fraction = c.candidate_length_m > 0 ? c.best_along_m / c.candidate_length_m : 0;
                c.candidate_id = UINT32_MAX;
            }
            continue;
        }
        if (c.remaining)
        {
            auto r = Result::Ok;
            if (!c.edge_pending)
            {
                r = package.read(SectionType::EdgeRefs, c.refs, c.read_bytes.data(), 4);
                if (r != Result::Ok) return r;
                c.candidate_id = u32(c.read_bytes.data());
                c.edge_pending = true;
            }
            r = graph.edge(c.candidate_id, c.candidate);
            if (r != Result::Ok) return r;
            c.edge_pending = false;
            c.refs += 4;
            --c.remaining;
            if (c.candidate.access & static_cast<uint8_t>(c.mode))
            {
                c.segment = 0;
                c.candidate_length_m = 0;
            }
            else c.candidate_id = UINT32_MAX;
            continue;
        }
        if (!c.depth)
        {
            c.complete = true;
            return c.best.edge == UINT32_MAX ? Result::Missing : Result::Ok;
        }
        auto& frame = c.stack[c.depth - 1];
        if (!frame.loaded)
        {
            auto* b = c.read_bytes.data();
            const auto r = resumableRecord(package, SectionType::EdgeSpatial, frame.row, 48, b, c.read);
            if (r != Result::Ok) return r;
            const auto box = bounds(b);
            const auto first = u32(b + 16);
            const auto count = u16(b + 20);
            const auto level = b[22];
            if (!count || b[23] || u64(b + 40) || box.west > box.east || box.south > box.north ||
                !coordinate(box.south, box.west) || !coordinate(box.north, box.east) ||
                (c.depth > 1 && level + 1 != c.stack[c.depth - 2].level)) return Result::Invalid;
            if (!box.intersects(c.bounds))
            {
                --c.depth;
                continue;
            }
            if (!level)
            {
                const auto* refs = package.section(SectionType::EdgeRefs);
                if (!refs || first || u32(b + 32) != uint32_t(count) * 4 || u32(b + 36) || !range(u64(b + 24), count * 4ULL, refs->bytes)) return Result::Invalid;
                c.refs = u64(b + 24);
                c.remaining = count;
                --c.depth;
                continue;
            }
            const auto* tree = package.section(SectionType::EdgeSpatial);
            if (!tree || first <= frame.row || level >= c.stack.size() || !range(first, count, tree->count) || u64(b + 24) || u64(b + 32)) return Result::Invalid;
            frame.next = first;
            frame.count = count;
            frame.level = level;
            frame.loaded = true;
        }
        else if (frame.count)
        {
            if (c.depth == c.stack.size()) return Result::Invalid;
            const auto next = frame.next++;
            --frame.count;
            c.stack[c.depth++] = {next, 0, 0, 0, false};
        }
        else --c.depth;
    }
    return Result::More;
}
namespace
{
bool cheaper(const RouteCursor& c, uint32_t a, uint32_t b)
{
    const auto& x = c.workspace.labels[a];
    const auto& y = c.workspace.labels[b];
    const double px = x.cost + (x.potential & 0x7fffffffU) / 100.0;
    const double py = y.cost + (y.potential & 0x7fffffffU) / 100.0;
    return px < py || (px == py && (x.cost > y.cost || (x.cost == y.cost && x.key < y.key)));
}
void swapHeap(RouteCursor& c, size_t a, size_t b)
{
    auto* heap = c.workspace.heap;
    std::swap(heap[a], heap[b]);
    c.workspace.labels[heap[a]].heap_position = uint32_t(a);
    c.workspace.labels[heap[b]].heap_position = uint32_t(b);
}
void raise(RouteCursor& c, size_t position)
{
    while (position)
    {
        const auto parent = (position - 1) / 2;
        if (!cheaper(c, c.workspace.heap[position], c.workspace.heap[parent])) break;
        swapHeap(c, position, parent);
        position = parent;
    }
}
uint32_t pop(RouteCursor& c)
{
    if (c.options.algorithm == RouteAlgorithm::QueueRelaxation)
    {
        const auto result = c.workspace.heap[c.queue_head];
        c.queue_head = uint32_t((size_t(c.queue_head) + 1) % c.workspace.capacity);
        --c.heap_size;
        c.workspace.labels[result].heap_position = UINT32_MAX;
        return result;
    }
    const auto result = c.workspace.heap[0];
    swapHeap(c, 0, c.heap_size - 1);
    --c.heap_size;
    c.workspace.labels[result].heap_position = UINT32_MAX;
    size_t pos = 0;
    while (pos * 2 + 1 < c.heap_size)
    {
        auto child = pos * 2 + 1;
        if (child + 1 < c.heap_size && cheaper(c, c.workspace.heap[child + 1], c.workspace.heap[child])) ++child;
        if (!cheaper(c, c.workspace.heap[child], c.workspace.heap[pos])) break;
        swapHeap(c, pos, child);
        pos = child;
    }
    return result;
}
uint32_t findSlot(RouteCursor& c, uint32_t key)
{
    const auto cap = c.workspace.capacity;
    size_t at = size_t((key * UINT32_C(2654435761)) % cap);
    for (size_t n = 0; n < cap; ++n, at = (at + 1) % cap)
        if (!c.workspace.labels[at].key || c.workspace.labels[at].key == key) return uint32_t(at);
    return UINT32_MAX;
}
Result finish(RouteCursor& c, Result r)
{
    c.complete = true;
    c.terminal = r;
    return r;
}
Result loadLandmarks(GraphReader& graph, RouteCursor& c)
{
    auto& package = graph.package();
    auto* bytes = c.landmark_scratch.data();
    if (!c.landmark_stage)
    {
        const auto* policy = package.section(SectionType::LandmarkPolicy);
        const auto* data = package.section(SectionType::LandmarkDistances);
        if (c.options.algorithm == RouteAlgorithm::Dijkstra || c.options.algorithm == RouteAlgorithm::QueueRelaxation || (!policy && !data))
        {
            if (c.options.algorithm == RouteAlgorithm::Landmarks) return Result::Missing;
            c.landmark_stage = 2;
            return Result::Ok;
        }
        if (!policy || !data || policy->bytes != 32 || policy->count != 1) return Result::Invalid;
        const auto r = package.read(SectionType::LandmarkPolicy, 0, bytes, 32);
        if (r != Result::Ok) return r;
        const auto count = u32(bytes);
        if (!count || count > 8 || u32(bytes + 4) != graph.info().nodes || u32(bytes + 8) != 1 || u32(bytes + 12) ||
            data->count != graph.info().nodes || data->bytes != uint64_t(graph.info().nodes) * count * 8) return Result::Invalid;
        c.landmark_count = uint8_t(count);
        c.landmark_stage = 1;
    }
    if (c.landmark_stage == 1)
    {
        const auto r = fixedFragment(package, SectionType::LandmarkDistances, uint64_t(c.destination) * c.landmark_count * 8, bytes, c.landmark_count * 8, c.landmark_completed);
        if (r != Result::Ok) return r;
        c.landmark_completed = 0;
        for (size_t i = 0; i < size_t(c.landmark_count) * 2; ++i) c.goal_landmarks[i] = u32(bytes + i * 4);
        c.landmark_stage = 2;
    }
    return Result::Ok;
}
Result lowerBound(GraphReader& graph, RouteCursor& c, uint32_t node, uint32_t& value)
{
    value = 0;
    if (!c.landmark_count) return Result::Ok;
    auto* bytes = c.landmark_scratch.data();
    const auto r = fixedFragment(graph.package(), SectionType::LandmarkDistances, uint64_t(node) * c.landmark_count * 8, bytes, c.landmark_count * 8, c.landmark_completed);
    if (r != Result::Ok) return r;
    c.landmark_completed = 0;
    for (size_t i = 0; i < c.landmark_count; ++i)
    {
        const auto from = u32(bytes + i * 8), to = u32(bytes + i * 8 + 4);
        const auto goal_from = c.goal_landmarks[i * 2], goal_to = c.goal_landmarks[i * 2 + 1];
        if (from != UINT32_MAX && goal_from != UINT32_MAX && goal_from > from) value = std::max(value, goal_from - from);
        if (to != UINT32_MAX && goal_to != UINT32_MAX && to > goal_to) value = std::max(value, to - goal_to);
    }
    value = std::min(value, UINT32_C(0x7fffffff));
    return Result::Ok;
}
} // namespace
Result beginRoute(GraphReader& graph, uint32_t start, uint32_t destination, const RouteOptions& options, RouteWorkspace w, RouteCursor& c)
{
    return beginRouteWithBoundaries(graph, start, destination, UINT32_MAX, UINT32_MAX, options, w, c);
}
Result beginRouteWithBoundaries(GraphReader& graph, uint32_t start, uint32_t destination, uint32_t initial_edge,
                                uint32_t final_edge, const RouteOptions& options, RouteWorkspace w, RouteCursor& c)
{
    reset(c);
    if (!w.labels || !w.heap || w.capacity < 2 || w.capacity > UINT32_MAX || !validMode(options.mode) || !options.max_expansions ||
        !std::isfinite(options.ascent_penalty) || options.ascent_penalty < 0 || uint8_t(options.algorithm) > uint8_t(RouteAlgorithm::QueueRelaxation)) return Result::Invalid;
    auto r = graph.initialize();
    if (r != Result::Ok) return r;
    if (start >= graph.info().nodes || destination >= graph.info().nodes) return Result::Invalid;
    r = graph.validateRouting(options.mode);
    if (r != Result::Ok) return r;
    c.workspace = w;
    c.options = options;
    c.generation = graph.package().generation();
    c.destination = destination;
    c.start = start;
    c.initial_edge = initial_edge;
    c.final_edge = final_edge;
    if ((initial_edge != UINT32_MAX && initial_edge >= graph.info().edges) || (final_edge != UINT32_MAX && final_edge >= graph.info().edges)) return Result::Invalid;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result routeStep(GraphReader& graph, RouteCursor& c, size_t budget)
{
    if (!budget || !c.workspace.labels) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return finish(c, Result::Cancelled);
    if (c.complete) return c.terminal;
    const auto policy = graph.validateRouting(c.options.mode);
    if (policy == Result::Busy || policy == Result::More) return policy;
    if (policy != Result::Ok) return finish(c, policy);
    for (size_t work = 0; work < budget; ++work)
    {
        if (!c.initialized)
        {
            if (c.landmark_stage != 2)
            {
                const auto r = loadLandmarks(graph, c);
                if (r == Result::Busy || r == Result::More) return r;
                if (r != Result::Ok) return finish(c, r);
                continue;
            }
            if (c.boundary_stage < 2)
            {
                const auto id = c.boundary_stage == 0 ? c.initial_edge : c.final_edge;
                if (id != UINT32_MAX)
                {
                    GraphEdge edge{};
                    auto r = graph.edge(id, edge);
                    if (r == Result::Busy || r == Result::More) return r;
                    if (r != Result::Ok) return finish(c, r);
                    if (!(edge.access & static_cast<uint8_t>(c.options.mode)) ||
                        (c.boundary_stage == 0 ? edge.to != c.start : edge.from != c.destination)) return finish(c, Result::Invalid);
                }
                ++c.boundary_stage;
                continue;
            }
            if (c.initialization < c.workspace.capacity)
            {
                c.workspace.labels[c.initialization++] = {};
                continue;
            }
            uint32_t root_key = UINT32_MAX;
            if (c.initial_edge != UINT32_MAX)
            {
                bool allowed = false;
                const auto result = graph.routeTransition(UINT32_MAX, c.initial_edge, c.options.mode, allowed, root_key);
                if (result == Result::Busy || result == Result::More) return result;
                if (result != Result::Ok) return finish(c, result);
                if (!allowed) return finish(c, Result::NoPath);
            }
            const auto slot = findSlot(c, root_key);
            uint32_t potential = 0;
            const auto r = lowerBound(graph, c, c.start, potential);
            if (r == Result::Busy || r == Result::More) return r;
            if (r != Result::Ok) return finish(c, r);
            c.workspace.labels[slot] = {0, root_key, c.start, c.initial_edge, UINT32_MAX, 0, potential};
            c.workspace.heap[0] = slot;
            c.heap_size = c.labels_used = 1;
            c.initialized = true;
            continue;
        }
        if (c.current == UINT32_MAX)
        {
            if (!c.heap_size) return finish(c, c.goal_label != UINT32_MAX ? Result::Ok : c.boundary_seen ? Result::OutsideCoverage
                                                                                                         : Result::NoPath);
            const auto slot = c.workspace.heap[c.options.algorithm == RouteAlgorithm::QueueRelaxation ? c.queue_head : 0];
            auto& label = c.workspace.labels[slot];
            if (label.node == c.destination)
            {
                bool allowed = true;
                if (c.final_edge != UINT32_MAX)
                {
                    uint32_t final_key = 0;
                    auto r = graph.routeTransition(label.key, c.final_edge, c.options.mode, allowed, final_key);
                    if (r == Result::Busy || r == Result::More) return r;
                    if (r != Result::Ok) return finish(c, r);
                }
                if (allowed)
                {
                    if (c.options.algorithm == RouteAlgorithm::QueueRelaxation)
                    {
                        if (c.goal_label == UINT32_MAX || label.cost < c.workspace.labels[c.goal_label].cost) c.goal_label = slot;
                        pop(c);
                        label.potential |= UINT32_C(0x80000000);
                        continue; // FIFO first arrival is not an optimality proof.
                    }
                    c.goal_label = pop(c);
                    return finish(c, Result::Ok);
                }
            }
            if (c.options.algorithm == RouteAlgorithm::QueueRelaxation && c.goal_label != UINT32_MAX &&
                label.cost >= c.workspace.labels[c.goal_label].cost)
            {
                pop(c);
                label.potential |= UINT32_C(0x80000000);
                continue;
            }
            GraphNode node{};
            auto r = graph.node(label.node, node);
            if (r == Result::Busy || r == Result::More) return r;
            if (r != Result::Ok) return finish(c, r);
            c.boundary_seen = c.boundary_seen || node.boundary;
            if (c.expansions >= c.options.max_expansions) return finish(c, Result::BudgetExceeded);
            ++c.expansions;
            c.current = pop(c);
            label.potential |= UINT32_C(0x80000000);
            c.first_edge = node.first_edge;
            c.edge_count = node.edge_count;
            c.next_edge = 0;
            continue;
        }
        if (c.next_edge == c.edge_count)
        {
            c.current = UINT32_MAX;
            continue;
        }
        auto& parent = c.workspace.labels[c.current];
        const auto edge_id = c.first_edge + c.next_edge;
        auto& edge = c.pending;
        Result r;
        if (!c.edge_loaded)
        {
            r = graph.edge(edge_id, edge);
            if (r == Result::Busy || r == Result::More) return r;
            if (r != Result::Ok) return finish(c, r);
            c.edge_loaded = true;
        }
        if (edge.from != parent.node) return finish(c, Result::Invalid);
        bool allowed = false;
        uint32_t next_key = 0;
        if (edge.access & static_cast<uint8_t>(c.options.mode))
        {
            r = graph.routeTransition(parent.key, edge_id, c.options.mode, allowed, next_key);
            if (r == Result::Busy || r == Result::More) return r;
            if (r != Result::Ok) return finish(c, r);
        }
        if (allowed)
        {
            if (c.options.ascent_penalty > 0 && !edge.has_elevation) return finish(c, Result::Incomplete);
            const auto slot = findSlot(c, next_key);
            if (slot == UINT32_MAX) return finish(c, Result::BudgetExceeded);
            auto& label = c.workspace.labels[slot];
            const double cost = parent.cost + edge.length_cm / 100.0 + edge.ascent_cm / 100.0 * c.options.ascent_penalty;
            if (!std::isfinite(cost)) return finish(c, Result::Invalid);
            if (!label.key || cost < label.cost)
            {
                uint32_t potential = label.potential & UINT32_C(0x7fffffff);
                if (!label.key)
                {
                    r = lowerBound(graph, c, edge.to, potential);
                    if (r == Result::Busy || r == Result::More) return r;
                    if (r != Result::Ok) return finish(c, r);
                }
                const bool queued = label.key && !(label.potential & UINT32_C(0x80000000));
                const auto position = queued ? label.heap_position : c.options.algorithm == RouteAlgorithm::QueueRelaxation ? uint32_t((size_t(c.queue_head) + c.heap_size) % c.workspace.capacity)
                                                                                                                            : uint32_t(c.heap_size);
                if (!queued) ++c.heap_size;
                if (!label.key) ++c.labels_used;
                label = {cost, next_key, edge.to, edge_id, c.current, position, potential};
                c.workspace.heap[position] = slot;
                if (c.options.algorithm != RouteAlgorithm::QueueRelaxation) raise(c, position);
            }
        }
        ++c.next_edge;
        c.edge_loaded = false;
    }
    return Result::More;
}
Result routeEdges(const RouteCursor& c, uint32_t* output, size_t capacity, size_t& count)
{
    count = 0;
    if (!c.complete || c.terminal != Result::Ok || c.goal_label == UINT32_MAX) return c.complete ? c.terminal : Result::More;
    auto at = c.goal_label;
    while (c.workspace.labels[at].previous != UINT32_MAX)
    {
        if (++count > c.workspace.capacity) return Result::Invalid;
        at = c.workspace.labels[at].previous;
        if (at >= c.workspace.capacity) return Result::Invalid;
    }
    if (count > capacity || (!output && count)) return Result::BudgetExceeded;
    at = c.goal_label;
    for (size_t n = count; n; --n)
    {
        output[n - 1] = c.workspace.labels[at].incoming;
        at = c.workspace.labels[at].previous;
    }
    return Result::Ok;
}
Result beginRouteGeometry(GraphReader& graph, const uint32_t* edges, size_t count, RouteGeometryCursor& c)
{
    reset(c);
    if (!edges && count) return Result::Invalid;
    auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.edges = edges;
    c.count = count;
    c.generation = graph.package().generation();
    c.complete = count == 0;
    return Result::Ok;
}
Result routeGeometryStep(GraphReader& graph, RouteGeometryCursor& c, size_t budget, RoutePointVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return Result::Cancelled;
    if (c.complete) return Result::Ok;
    for (size_t work = 0; work < budget; ++work)
    {
        auto& edge = c.scratch;
        Result r;
        if (!c.loaded)
        {
            r = graph.edge(c.edges[c.edge_index], edge);
            if (r != Result::Ok) return r;
            c.loaded = true;
        }
        GeoPoint point{};
        r = graph.point(edge, c.point_index, point);
        if (r != Result::Ok) return r;
        if (!visitor(context, point))
        {
            c.complete = true;
            c.cancelled = true;
            return Result::Cancelled;
        }
        if (++c.point_index == edge.points)
        {
            c.point_index = 1;
            c.loaded = false;
            if (++c.edge_index == c.count)
            {
                c.complete = true;
                return Result::Ok;
            }
        }
    }
    return Result::More;
}
Result beginManeuvers(GraphReader& graph, const uint32_t* edges, size_t count, ManeuverCursor& c)
{
    reset(c);
    if (!edges && count) return Result::Invalid;
    const auto r = graph.initialize();
    if (r != Result::Ok) return r;
    c.edges = edges;
    c.count = count;
    c.generation = graph.package().generation();
    c.complete = count == 0;
    c.terminal = c.complete ? Result::Ok : Result::More;
    return Result::Ok;
}
Result maneuverStep(GraphReader& graph, ManeuverCursor& c, size_t budget, ManeuverVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (!graph.package().isOpen() || c.generation != graph.package().generation()) return Result::Cancelled;
    if (c.complete) return c.terminal;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.index == c.count)
        {
            if (!visitor(context, {ManeuverKind::Arrive, c.arrival, c.distance_m, 0}))
            {
                c.complete = true;
                c.terminal = Result::Cancelled;
                return c.terminal;
            }
            c.complete = true;
            c.terminal = Result::Ok;
            return c.terminal;
        }
        auto& edge = c.scratch;
        Result r;
        if (!c.stage)
        {
            r = graph.edge(c.legs ? c.legs[c.index].edge : c.edges[c.index], edge);
            if (r != Result::Ok) return r;
            c.first_segment = c.legs ? c.legs[c.index].first_segment : 0;
            c.last_segment = c.legs && c.legs[c.index].last_segment != UINT32_MAX ? c.legs[c.index].last_segment : edge.points - 2;
            c.first_fraction = c.legs ? c.legs[c.index].first_fraction : 0;
            c.last_fraction = c.legs ? c.legs[c.index].last_fraction : 1;
            if (c.first_segment > c.last_segment || c.last_segment + 1 >= edge.points ||
                !std::isfinite(c.first_fraction) || !std::isfinite(c.last_fraction) || c.first_fraction < 0 || c.first_fraction > 1 ||
                c.last_fraction < 0 || c.last_fraction > 1 || (c.first_segment == c.last_segment && c.first_fraction > c.last_fraction)) return Result::Invalid;
            c.stage = 1;
            continue;
        }
        if (c.stage <= 4)
        {
            const auto point = c.stage == 1 ? c.first_segment : c.stage == 2 ? c.first_segment + 1
                                                            : c.stage == 3   ? c.last_segment
                                                                             : c.last_segment + 1;
            auto& output = c.stage == 1 ? c.start_a : c.stage == 2 ? c.start_b
                                                  : c.stage == 3   ? c.finish_a
                                                                   : c.finish_b;
            r = graph.point(edge, point, output);
            if (r != Result::Ok) return r;
            ++c.stage;
            continue;
        }
        if (c.stage == 6)
        {
            r = graph.point(edge, c.segment, c.start_a);
            if (r == Result::Ok) r = graph.point(edge, c.segment + 1, c.start_b);
            if (r != Result::Ok) return r;
            const double first = c.segment == c.first_segment ? c.first_fraction : 0;
            const double last = c.segment == c.last_segment ? c.last_fraction : 1;
            c.distance_m += distanceMeters(c.start_a, c.start_b) * (last - first);
            if (++c.segment > c.last_segment)
            {
                ++c.index;
                c.stage = 0;
            }
            continue;
        }
        const auto& a = c.start_a;
        const auto& b = c.start_b;
        ManeuverKind kind = ManeuverKind::Depart;
        if (c.index)
        {
            const float scale = std::cos(float(a.latitude) * 1e-7f * 3.14159265358979323846f / 180);
            const float bearing1 = std::atan2(float(int64_t(c.previous_b.longitude) - c.previous_a.longitude) * scale, float(int64_t(c.previous_b.latitude) - c.previous_a.latitude));
            const float bearing2 = std::atan2(float(int64_t(b.longitude) - a.longitude) * scale, float(int64_t(b.latitude) - a.latitude));
            const float angle = std::remainder((bearing2 - bearing1) * 180 / 3.14159265358979323846f, 360.0f);
            kind = std::abs(angle) > 150 ? ManeuverKind::UTurn : angle > 30 ? ManeuverKind::Right
                                                             : angle < -30  ? ManeuverKind::Left
                                                                            : ManeuverKind::Continue;
        }
        GeoPoint departure{int32_t(std::llround(a.latitude + (double(b.latitude) - a.latitude) * c.first_fraction)),
                           int32_t(std::llround(a.longitude + (double(b.longitude) - a.longitude) * c.first_fraction))};
        if (!visitor(context, {kind, departure, c.distance_m, edge.name_id}))
        {
            c.complete = true;
            c.terminal = Result::Cancelled;
            return c.terminal;
        }
        c.previous_a = c.finish_a;
        c.previous_b = c.finish_b;
        c.arrival = {int32_t(std::llround(c.finish_a.latitude + (double(c.finish_b.latitude) - c.finish_a.latitude) * c.last_fraction)),
                     int32_t(std::llround(c.finish_a.longitude + (double(c.finish_b.longitude) - c.finish_a.longitude) * c.last_fraction))};
        if (c.legs)
        {
            c.segment = c.first_segment;
            c.stage = 6;
        }
        else
        {
            c.distance_m += edge.length_cm / 100.0;
            ++c.index;
            c.stage = 0;
        }
    }
    return Result::More;
}
Result Navigator::begin(const GeoPoint* route, size_t count, const NavigationOptions& options)
{
    route_ = nullptr;
    count_ = 0;
    total_m_ = 0;
    state_ = {};
    ++update_generation_;
    if (!route || !count || !std::isfinite(options.deviation_m) || options.deviation_m <= 0 ||
        !std::isfinite(options.arrival_m) || options.arrival_m <= 0 || !options.deviation_fixes) return Result::Invalid;
    for (size_t i = 0; i < count; ++i)
    {
        if (!coordinate(route[i].latitude, route[i].longitude)) return Result::Invalid;
        if (i) total_m_ += distanceMeters(route[i - 1], route[i]);
    }
    route_ = route;
    count_ = count;
    options_ = options;
    state_.remaining_m = total_m_;
    return Result::Ok;
}
Result Navigator::beginUpdate(GeoPoint fix, double accuracy_m, NavigationCursor& c)
{
    reset(c);
    if (!route_ || !coordinate(fix.latitude, fix.longitude) || !std::isfinite(accuracy_m) || accuracy_m < 0) return Result::Invalid;
    c.fix = fix;
    c.accuracy_m = accuracy_m;
    c.best_m = std::numeric_limits<double>::infinity();
    c.generation = ++update_generation_;
    c.complete = false;
    return Result::Ok;
}
Result Navigator::updateStep(NavigationCursor& c, size_t budget, NavigationState& output)
{
    if (!budget || !route_) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (c.generation != update_generation_) return Result::Cancelled;
    if (c.complete)
    {
        output = state_;
        return Result::Ok;
    }
    if (count_ == 1)
    {
        c.best_m = distanceMeters(c.fix, route_[0]);
        c.matched = route_[0];
        c.along_m = 0;
    }
    for (size_t work = 0; work < budget && c.next_segment + 1 < count_; ++work, ++c.next_segment)
    {
        const auto i = c.next_segment;
        double t = 0;
        GeoPoint p{};
        const double d = closest(c.fix, route_[i], route_[i + 1], t, p), length = distanceMeters(route_[i], route_[i + 1]);
        const double progress = c.accumulated_m + t * length;
        if (d < c.best_m - 0.5 || (std::abs(d - c.best_m) <= 0.5 && std::abs(progress - state_.progress_m) < std::abs(c.along_m - state_.progress_m)))
        {
            c.best_m = d;
            c.along_m = progress;
            c.matched = p;
            c.matched_segment = uint32_t(i);
        }
        c.accumulated_m += length;
    }
    if (c.next_segment + 1 < count_) return Result::More;
    const double threshold = std::max(options_.deviation_m, c.accuracy_m * 2);
    if (c.best_m > threshold)
    {
        if (state_.outside_fixes < 255) ++state_.outside_fixes;
    }
    else if (c.best_m < threshold * 0.7) state_.outside_fixes = 0;
    state_.off_route = state_.outside_fixes >= options_.deviation_fixes;
    state_.cross_track_m = c.best_m;
    state_.matched = c.matched;
    state_.segment = c.matched_segment;
    state_.progress_m = c.along_m;
    state_.remaining_m = std::max(0.0, total_m_ - c.along_m);
    state_.arrived = state_.remaining_m <= options_.arrival_m && distanceMeters(c.fix, route_[count_ - 1]) <= options_.arrival_m;
    c.complete = true;
    output = state_;
    return Result::Ok;
}
Result Navigator::update(GeoPoint fix, double accuracy_m, size_t budget, NavigationState& output)
{
    if (!route_ || !budget) return Result::Invalid;
    if (budget < count_ - 1) return Result::BudgetExceeded;
    NavigationCursor c{};
    const auto r = beginUpdate(fix, accuracy_m, c);
    return r == Result::Ok ? updateStep(c, budget, output) : r;
}
} // namespace tmap
