#include "tmap/administration.h"
#include "binary.h"
#include "tmap/administrative_path.h"
#include <algorithm>
#include <cstring>
namespace tmap
{
using namespace detail;
namespace
{
Result finish(AdministrativeCursor& c, Result result)
{
    c.complete = true;
    c.terminal = result;
    if (result != Result::Ok && c.workspace)
    {
        c.workspace->path[0] = 0;
        c.result.depth = 0;
    }
    return result;
}
bool onSegment(GeoPoint p, GeoPoint a, GeoPoint b)
{
    // Compare products; subtracting them could overflow at global E7 extents.
    return (int64_t(p.longitude) - a.longitude) * (int64_t(b.latitude) - a.latitude) ==
               (int64_t(p.latitude) - a.latitude) * (int64_t(b.longitude) - a.longitude) &&
           p.latitude >= std::min(a.latitude, b.latitude) && p.latitude <= std::max(a.latitude, b.latitude) &&
           p.longitude >= std::min(a.longitude, b.longitude) && p.longitude <= std::max(a.longitude, b.longitude);
}
bool crosses(GeoPoint p, GeoPoint a, GeoPoint b)
{
    if ((a.latitude > p.latitude) == (b.latitude > p.latitude)) return false;
    const auto left = (int64_t(p.longitude) - a.longitude) * (int64_t(b.latitude) - a.latitude);
    const auto right = (int64_t(b.longitude) - a.longitude) * (int64_t(p.latitude) - a.latitude);
    return b.latitude > a.latitude ? left < right : left > right;
}
uint8_t pathDepth(const char* path)
{
    uint8_t count = *path ? 1 : 0;
    bool segment = false;
    for (; *path; ++path)
    {
        if (*path == '/')
        {
            if (!segment || ++count > 32) return 0;
            segment = false;
        }
        else if (*path != ' ') segment = true;
    }
    return segment ? count : 0;
}
bool utf8(const uint8_t* text, size_t size)
{
    for (size_t i = 0; i < size;)
    {
        const auto first = text[i++];
        if (first < 0x80) continue;
        uint32_t value = 0, minimum = 0;
        size_t count = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            value = first & 31;
            minimum = 0x80;
            count = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            value = first & 15;
            minimum = 0x800;
            count = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            value = first & 7;
            minimum = 0x10000;
            count = 3;
        }
        else return false;
        if (count > size - i) return false;
        while (count--)
        {
            const auto next = text[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
} // namespace
Result beginAdministrativeQuery(OutdoorPackage& package, GeoPoint point, AdministrativeWorkspace& workspace,
                                AdministrativeCursor& c, uint32_t max_operations)
{
    reset(c);
    workspace.path[0] = workspace.candidate[0] = 0;
    c.workspace = &workspace;
    c.result.path = workspace.path.data();
    if (!package.isOpen() || !coordinate(point.latitude, point.longitude) || !max_operations) return Result::Invalid;
    const auto* tree = package.section(SectionType::AdministrativeIndex);
    const auto* records = package.section(SectionType::AdministrativeRecords);
    if (!tree || !records || !package.section(SectionType::AdministrativeGeometry) || !package.section(SectionType::AdministrativePaths)) return Result::Missing;
    if (!tree->count || tree->count > UINT32_MAX || tree->bytes != tree->count * 48 ||
        records->count > UINT32_MAX || records->bytes != records->count * 48) return Result::Invalid;
    c.point = point;
    c.generation = package.generation();
    c.max_operations = max_operations;
    c.depth = 1;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
void cancelAdministrativeQuery(AdministrativeCursor& c) { finish(c, Result::Cancelled); }
Result administrativeStep(OutdoorPackage& package, AdministrativeCursor& c, size_t budget)
{
    if (!budget || !c.workspace) return Result::Invalid;
    if (c.complete) return c.terminal;
    if (!package.isOpen() || c.generation != package.generation()) return finish(c, Result::Cancelled);
    auto* bytes = c.scratch.data();
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.operations == c.max_operations)
        {
            c.result.limited = true;
            return finish(c, Result::BudgetExceeded);
        }
        Result r;
        if (c.stage == 1)
        {
            r = resumableRead(package, SectionType::AdministrativeGeometry, c.geometry, bytes, 4, c.read);
            if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
            c.ring_points = u32(bytes);
            if (c.ring_points < 4 || c.ring_points > c.points) return finish(c, Result::Invalid);
            c.geometry += 4;
            c.has_previous = false;
            c.stage = 2;
        }
        else if (c.stage == 2)
        {
            r = resumableRead(package, SectionType::AdministrativeGeometry, c.geometry, bytes, 8, c.read);
            if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
            const GeoPoint next{i32(bytes), i32(bytes + 4)};
            if (!coordinate(next.latitude, next.longitude)) return finish(c, Result::Invalid);
            if (c.has_previous)
            {
                if (std::abs(int64_t(next.longitude) - c.previous.longitude) > 1800000000) return finish(c, Result::Unsupported);
                c.boundary = c.boundary || onSegment(c.point, c.previous, next);
                c.inside = c.inside != crosses(c.point, c.previous, next);
            }
            else c.first = next;
            c.previous = next;
            c.has_previous = true;
            c.geometry += 8;
            --c.points;
            if (!--c.ring_points)
            {
                if (c.first.latitude != next.latitude || c.first.longitude != next.longitude) return finish(c, Result::Invalid);
                c.stage = --c.rings ? 1 : 3;
                if (!c.rings && c.points) return finish(c, Result::Invalid);
                if (!c.rings && !c.inside && !c.boundary) c.stage = 0;
            }
        }
        else if (c.stage == 3)
        {
            r = resumableRead(package, SectionType::AdministrativePaths, c.path_ref, bytes, 2, c.read);
            if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
            c.path_bytes = u16(bytes);
            if (!c.path_bytes || c.path_bytes > 512) return finish(c, Result::Invalid);
            c.stage = 4;
        }
        else if (c.stage == 4)
        {
            auto* path = c.workspace->candidate.data();
            r = resumableRead(package, SectionType::AdministrativePaths, c.path_ref + 2, reinterpret_cast<uint8_t*>(path), c.path_bytes, c.read);
            if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
            if (std::memchr(path, 0, c.path_bytes) || !utf8(reinterpret_cast<const uint8_t*>(path), c.path_bytes)) return finish(c, Result::Invalid);
            path[c.path_bytes] = 0;
            if (pathDepth(path) != c.candidate_depth) return finish(c, Result::Invalid);
            if (c.candidate_depth > c.best_depth)
            {
                std::memcpy(c.workspace->path.data(), path, c.path_bytes + 1);
                c.best_depth = c.candidate_depth;
                c.result.matches = 1;
                c.result.boundary = c.boundary;
            }
            else if (c.candidate_depth == c.best_depth)
            {
                commonAdministrativePath(c.workspace->path.data(), path);
                ++c.result.matches;
                c.result.boundary = c.result.boundary || c.boundary;
            }
            c.result.depth = pathDepth(c.workspace->path.data());
            c.stage = 0;
        }
        else if (c.remaining_rows)
        {
            r = resumableRecord(package, SectionType::AdministrativeRecords, c.row, 48, bytes, c.read);
            if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
            const auto box = bounds(bytes);
            if (!coordinate(box.south, box.west) || !coordinate(box.north, box.east) || box.west > box.east || box.south > box.north) return finish(c, Result::Invalid);
            ++c.row;
            --c.remaining_rows;
            if (!box.contains(c.point.latitude, c.point.longitude))
            {
                ++c.operations;
                continue;
            }
            c.geometry = u64(bytes + 16);
            c.rings = u32(bytes + 24);
            c.points = u32(bytes + 28);
            c.path_ref = u64(bytes + 32);
            c.candidate_depth = bytes[40];
            if (!c.rings || c.rings > c.points / 4 || !c.path_ref || !c.candidate_depth || c.candidate_depth > 32 || bytes[41] || u16(bytes + 42) || u32(bytes + 44)) return finish(c, Result::Invalid);
            --c.path_ref;
            c.inside = c.boundary = false;
            c.stage = 1;
        }
        else if (!c.depth) return finish(c, c.result.matches ? Result::Ok : Result::Missing);
        else
        {
            auto& frame = c.stack[c.depth - 1];
            if (!frame.loaded)
            {
                r = resumableRecord(package, SectionType::AdministrativeIndex, frame.row, 48, bytes, c.read);
                if (r != Result::Ok) return r == Result::Busy || r == Result::More ? r : finish(c, r);
                const auto box = bounds(bytes);
                const auto first = u32(bytes + 16);
                const auto count = u16(bytes + 20);
                const auto level = bytes[22];
                if (!count || bytes[23] || u32(bytes + 36) || u64(bytes + 40) || !coordinate(box.south, box.west) || !coordinate(box.north, box.east) ||
                    box.west > box.east || box.south > box.north || (c.depth > 1 && level + 1 != c.stack[c.depth - 2].level)) return finish(c, Result::Invalid);
                if (!box.contains(c.point.latitude, c.point.longitude)) --c.depth;
                else if (!level)
                {
                    const auto* records = package.section(SectionType::AdministrativeRecords);
                    const auto offset = u64(bytes + 24);
                    if (first || offset % 48 || u32(bytes + 32) != uint32_t(count) * 48 || !range(offset, uint64_t(count) * 48, records->bytes)) return finish(c, Result::Invalid);
                    c.row = offset / 48;
                    c.remaining_rows = count;
                    --c.depth;
                }
                else
                {
                    const auto* tree = package.section(SectionType::AdministrativeIndex);
                    if (first <= frame.row || level >= c.stack.size() || !range(first, count, tree->count) || u64(bytes + 24) || u32(bytes + 32)) return finish(c, Result::Invalid);
                    frame.next = first;
                    frame.count = count;
                    frame.level = level;
                    frame.loaded = true;
                }
            }
            else if (frame.count)
            {
                if (c.depth == c.stack.size()) return finish(c, Result::Invalid);
                const auto next = frame.next++;
                --frame.count;
                c.stack[c.depth++] = {next, 0, 0, 0, false};
            }
            else --c.depth;
        }
        ++c.operations;
    }
    return Result::More;
}
} // namespace tmap
