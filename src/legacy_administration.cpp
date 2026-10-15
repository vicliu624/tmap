#include "tmap/legacy_administration.h"
#include "binary.h"
#include "tmap/administrative_path.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace tmap
{
using namespace detail;
namespace
{
Result finish(LegacyAdministrativeCursor& c, Result r)
{
    c.complete = true;
    c.terminal = r;
    auto* path = c.workspace ? c.workspace->path.data() : nullptr;
    if (path)
    {
        path[0] = 0;
        if ((r == Result::Ok || r == Result::Incomplete) && c.count)
        {
            std::memcpy(path, c.candidates[0].path.data(), 513);
            for (size_t i = 1; i < c.count; ++i) commonAdministrativePath(path, c.candidates[i].path.data());
        }
    }
    c.result.matches = uint32_t(c.count);
    c.result.depth = path && *path ? 1 : 0;
    if (path)
        for (const char* p = path; *p; ++p)
            if (*p == '/') ++c.result.depth;
    return r;
}
bool better(const LegacyAdministrativeCandidate& a, const LegacyAdministrativeCandidate& b)
{
    return a.distance_cm < b.distance_cm || (a.distance_cm == b.distance_cm && std::memcmp(a.id.data(), b.id.data(), 16) < 0);
}
void accept(LegacyAdministrativeCursor& c)
{
    auto& candidate = c.workspace->candidate;
    if (!candidate.path[0] || (candidate.flags & 2)) return;
    size_t slot = c.count;
    for (size_t i = 0; i < c.count; ++i)
        if (candidate.id == c.candidates[i].id) return;
    if (slot == c.capacity)
    {
        slot = 0;
        for (size_t i = 1; i < c.count; ++i)
            if (better(c.candidates[slot], c.candidates[i])) slot = i;
        if (!better(candidate, c.candidates[slot])) return;
    }
    else ++c.count;
    std::memcpy(&c.candidates[slot], &candidate, sizeof(candidate));
}
} // namespace
Result LegacyAdministrativeQuery::begin(Reader& reader, GeoPoint point, LegacyAdministrativeWorkspace& workspace,
                                        LegacyAdministrativeCandidate* candidates, size_t capacity, LegacyAdministrativeCursor& c,
                                        uint32_t radius_m, uint32_t max_candidates, uint32_t max_operations)
{
    reset(c);
    c.workspace = &workspace;
    workspace.path[0] = 0;
    c.result.path = workspace.path.data();
    c.result.approximate = true;
    if (!reader.isOpen() || !coordinate(point.latitude, point.longitude) || !candidates || !capacity || capacity > 32 ||
        !radius_m || radius_m > 100000 || !max_candidates || !max_operations) return Result::Invalid;
    const auto* tree = reader.section(24);
    const auto* refs = reader.section(50);
    const auto* paths = reader.section(51);
    if (!refs && !paths) return Result::Missing;
    if (!tree || !refs || !paths || refs->count != reader.poiCount()) return Result::Invalid;
    if (!tree->root) return Result::Missing;
    // Conservative band. A broad longitude range at poles/date line is exact
    // filtered by distance below; it does not pretend to wrap one Bounds box.
    const float dy = (float(radius_m) + 1) / 110000.0f * 1e7f;
    const float dx = dy / std::max(0.0001f, std::cos(float(point.latitude) * 1e-7f * 3.14159265358979323846f / 180));
    const auto iy = int64_t(std::ceil(dy)), ix = int64_t(std::ceil(dx));
    const bool wraps = std::abs(int64_t(point.latitude)) > 800000000 || int64_t(point.longitude) - ix < -1800000000 || int64_t(point.longitude) + ix > 1800000000;
    c.bounds = {wraps ? -1800000000 : int32_t(int64_t(point.longitude) - ix),
                int32_t(std::max<int64_t>(-900000000, int64_t(point.latitude) - iy)),
                wraps ? 1799999999 : int32_t(int64_t(point.longitude) + ix),
                int32_t(std::min<int64_t>(900000000, int64_t(point.latitude) + iy))};
    c.point = point;
    c.radius_cm = radius_m * 100;
    c.candidates = candidates;
    c.capacity = capacity;
    c.max_candidates = max_candidates;
    c.max_operations = max_operations;
    c.build = reader.package().build;
    c.revision = reader.package().revision;
    c.stack[0].offset = tree->root;
    c.depth = 1;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
void LegacyAdministrativeQuery::cancel(LegacyAdministrativeCursor& c) { finish(c, Result::Cancelled); }
Result LegacyAdministrativeQuery::step(Reader& reader, LegacyAdministrativeCursor& c, size_t budget)
{
    if (!budget || !c.workspace) return Result::Invalid;
    if (c.complete) return c.terminal;
    if (!reader.isOpen() || reader.package().build != c.build || reader.package().revision != c.revision) return finish(c, Result::Cancelled);
    const auto* tree = reader.section(24);
    if (!tree) return finish(c, Result::Invalid);
    auto* bytes = reader.workspace_.record.data();
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.operations == c.max_operations)
        {
            c.result.limited = true;
            return finish(c, Result::BudgetExceeded);
        }
        Status status;
        if (c.stage == 1)
        {
            status = reader.row(50, c.row, 16, bytes);
            if (status != Status::Ok) return status == Status::Busy || status == Status::More ? convert(status) : finish(c, convert(status));
            c.path_ref = u64(bytes);
            c.workspace->candidate.levels = bytes[8];
            c.workspace->candidate.flags = bytes[9];
            if ((bytes[8] & ~63U) || (bytes[9] & ~3U) || bytes[13] || bytes[14] || bytes[15]) return finish(c, Result::Invalid);
            c.stage = c.path_ref && !(bytes[9] & 2) ? 2 : 3;
        }
        else if (c.stage == 2)
        {
            status = reader.string(c.path_ref, c.workspace->candidate.path.data(), 513, 51);
            if (status != Status::Ok) return status == Status::Busy || status == Status::More ? convert(status) : finish(c, convert(status));
            accept(c);
            c.stage = 3;
        }
        else if (c.stage == 3)
        {
            ++c.row;
            --c.remaining_rows;
            c.stage = 0;
        }
        else if (c.remaining_rows)
        {
            status = reader.row(20, c.row, 96, bytes);
            if (status != Status::Ok) return status == Status::Busy || status == Status::More ? convert(status) : finish(c, convert(status));
            c.candidate_point = {i32(bytes + 16), i32(bytes + 20)};
            if (!coordinate(c.candidate_point.latitude, c.candidate_point.longitude)) return finish(c, Result::Invalid);
            c.stage = 3;
            if (c.bounds.contains(c.candidate_point.latitude, c.candidate_point.longitude))
            {
                if (c.scanned == c.max_candidates)
                {
                    c.result.limited = true;
                    return finish(c, c.count ? Result::Incomplete : Result::BudgetExceeded);
                }
                ++c.scanned;
                const auto distance = distanceMeters(c.point, c.candidate_point);
                if (distance <= c.radius_cm / 100.0)
                {
                    reset(c.workspace->candidate);
                    std::memcpy(c.workspace->candidate.id.data(), bytes, 16);
                    c.workspace->candidate.distance_cm = uint32_t(std::llround(distance * 100));
                    c.stage = 1;
                }
            }
        }
        else if (!c.depth) return finish(c, c.count ? Result::Ok : Result::Missing);
        else
        {
            auto& frame = c.stack[c.depth - 1];
            const uint8_t* page = nullptr;
            status = reader.page(*tree, frame.offset, page);
            if (status != Status::Ok) return status == Status::Busy || status == Status::More ? convert(status) : finish(c, convert(status));
            const auto type = u16(page + 4), level = u16(page + 6);
            const auto count = u32(page + 16);
            if (!count || count > 126 || u16(page + 20) != 32 || (type != 13 && type != 14) || (type == 14 && level) ||
                (c.depth > 1 && level + 1 != c.stack[c.depth - 2].level)) return finish(c, Result::Invalid);
            if (!frame.loaded)
            {
                frame.count = count;
                frame.level = level;
                frame.loaded = true;
            }
            if (frame.next == frame.count)
            {
                --c.depth;
                ++c.operations;
                continue;
            }
            const auto* item = page + 64 + frame.next * 32;
            const auto box = bounds(item);
            const auto child = u64(item + 16);
            const auto rows = u32(item + 24);
            if (!coordinate(box.south, box.west) || !coordinate(box.north, box.east) || box.west > box.east || box.south > box.north) return finish(c, Result::Invalid);
            ++frame.next;
            if (box.intersects(c.bounds))
            {
                if (type == 13)
                {
                    if (c.depth == c.stack.size()) return finish(c, Result::Invalid);
                    c.stack[c.depth].offset = child;
                    c.stack[c.depth].next = c.stack[c.depth].count = 0;
                    c.stack[c.depth].loaded = false;
                    ++c.depth;
                }
                else
                {
                    if (!child || !rows || rows > 42 || child > reader.poiCount() || rows > reader.poiCount() - child + 1) return finish(c, Result::Invalid);
                    c.row = child;
                    c.remaining_rows = rows;
                }
            }
        }
        ++c.operations;
    }
    return Result::More;
}
} // namespace tmap
