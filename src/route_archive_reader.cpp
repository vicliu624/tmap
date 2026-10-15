#include "route_archive_wire.h"
#include <new>
namespace tmap
{
using namespace route_detail;
void RouteArchiveReader::close()
{
    file_ = nullptr;
    cached_ = UINT64_MAX;
    ::new (static_cast<void*>(&info_)) RouteArchiveInfo{};
    ++generation_;
}
Result RouteArchiveReader::open(RandomAccessFile& file)
{
    close();
    if (file.size() < Header) return Result::Incomplete;
    auto* s = workspace_.scratch.data();
    const auto r = archiveIo(file.readAt(0, s, Header));
    if (r != Result::Ok) return r;
    if (std::memcmp(s, "TMRT", 4) || u32(s + 12) != 1) return Result::Incomplete;
    if (u32(s + 4) != 1 || u32(s + 8) != Header) return Result::Unsupported;
    if (!crc(s, Header, 136)) return Result::Invalid;
    for (size_t i = 152; i < Header; ++i)
        if (s[i]) return Result::Unsupported;
    std::memcpy(info_.map_id.data(), s + 16, 16);
    std::memcpy(info_.map_build.data(), s + 32, 16);
    std::memcpy(info_.map_series.data(), s + 48, 16);
    info_.map_revision = u64(s + 64);
    info_.cost_units = u64(s + 72);
    info_.length_cm = u64(s + 80);
    info_.points = u64(s + 88);
    info_.legs = u32(s + 96);
    info_.chunks = u32(s + 100);
    if ((u32(s + 140) != uint32_t(TravelMode::Foot) && u32(s + 140) != uint32_t(TravelMode::Bicycle)) || !u32(s + 148)) return Result::Invalid;
    info_.mode = TravelMode(u32(s + 140));
    info_.ascent.numerator = u32(s + 144);
    info_.ascent.denominator = u32(s + 148);
    data_ = u64(s + 104);
    index_ = u64(s + 112);
    root_ = u64(s + 120);
    file_bytes_ = u64(s + 128);
    const uint64_t expected_data = (Header + uint64_t(info_.legs) * 16 + Page - 1) / Page * Page;
    const uint64_t nodes = archiveNodeCount(info_.chunks);
    const auto expected_chunks = info_.points > 1 ? (info_.points - 2) / (RouteArchiveWorkspace::kPoints - 1) + 1 : 1;
    if (!info_.points || !info_.chunks || info_.chunks > MaxChunks || info_.chunks != expected_chunks || data_ != expected_data ||
        index_ != data_ + uint64_t(info_.chunks) * Page || file_bytes_ != file.size() || file_bytes_ < index_ + 64 ||
        (file_bytes_ - index_) % 64 || root_ != file_bytes_ - 64 ||
        (file_bytes_ - index_) / 64 != nodes) return Result::Invalid;
    file_ = &file;
    return Result::Ok;
}
bool RouteArchiveReader::matches(const OutdoorInfo& map) const
{
    return file_ && info_.map_id == map.id && info_.map_build == map.build && info_.map_series == map.series && info_.map_revision == map.revision;
}
Result RouteArchiveReader::readLeg(uint32_t index, IntegerRouteLeg& leg, const OutdoorInfo& map)
{
    if (!file_) return Result::Invalid;
    if (!matches(map)) return Result::Incompatible;
    if (index >= info_.legs) return Result::Missing;
    const auto r = archiveIo(file_->readAt(Header + uint64_t(index) * 16, workspace_.scratch.data(), 16));
    if (r == Result::Ok)
    {
        if (!crc(workspace_.scratch.data(), 16, 12)) return Result::Invalid;
        decodeLeg(workspace_.scratch.data(), leg);
    }
    return r;
}
Result RouteArchiveReader::beginQuery(const RouteArchiveQuery& query, RouteArchiveQueryCursor& c)
{
    ::new (static_cast<void*>(&c)) RouteArchiveQueryCursor{};
    if (!file_ || !validBounds(query.viewport) || query.first_cm > query.last_cm) return Result::Invalid;
    c.query = query;
    c.generation = generation_;
    c.stack[0] = root_;
    c.depth = 1;
    c.terminal = Result::More;
    return Result::Ok;
}
Result RouteArchiveReader::queryUnit(RouteArchiveQueryCursor& c, RouteArchiveVisitor visitor, void* context)
{
    if (c.chunk)
    {
        auto* p = workspace_.page.data();
        if (cached_ != c.chunk)
        {
            const auto r = archiveIo(file_->readAt(c.chunk, p, Page));
            if (r != Result::Ok) return r;
            cached_ = UINT64_MAX;
            if (std::memcmp(p, "RCHK", 4) || !crc(p, Page, 4) || u32(p + 8) != (c.chunk - data_) / Page ||
                !u32(p + 12) || u32(p + 12) > RouteArchiveWorkspace::kPoints) return Result::Invalid;
            cached_ = c.chunk;
        }
        if (c.vertex >= u32(p + 12))
        {
            c.chunk = 0;
            return Result::More;
        }
        decodePoint(p + 64 + (c.vertex - 1) * 24, c.segment.first);
        decodePoint(p + 64 + c.vertex * 24, c.segment.last);
        const auto& a = c.segment.first;
        const auto& b = c.segment.last;
        if (!validPoint(a.point) || !validPoint(b.point) || a.distance_cm > b.distance_cm ||
            b.distance_cm > info_.length_cm || (info_.legs ? (a.leg >= info_.legs || b.leg >= info_.legs) : (a.leg || b.leg)) ||
            u32(p + 64 + c.vertex * 24 + 20) > 1) return Result::Invalid;
        if (c.segments >= c.query.max_segments) return Result::BudgetExceeded;
        ++c.segments;
        ++c.vertex;
        if (!b.break_before && a.distance_cm <= c.query.last_cm && b.distance_cm >= c.query.first_cm &&
            visible(a.point, b.point, c.query.viewport) && !visitor(context, c.segment)) return Result::Cancelled;
        return Result::More;
    }
    if (!c.depth) return Result::Ok;
    if (c.nodes >= c.query.max_nodes) return Result::BudgetExceeded;
    const auto offset = c.stack[c.depth - 1];
    if (offset < index_ || offset >= file_bytes_ || (offset - index_) % 64) return Result::Invalid;
    auto* s = workspace_.scratch.data();
    const auto r = archiveIo(file_->readAt(offset, s, 64));
    if (r != Result::Ok) return r;
    if (!crc(s, 64, 60)) return Result::Invalid;
    const auto box = bounds(s);
    if (!validBounds(box) || box.west > box.east || u64(s + 16) > u64(s + 24)) return Result::Invalid;
    --c.depth;
    ++c.nodes;
    if (!intersects(box, c.query.viewport) || u64(s + 16) > c.query.last_cm || u64(s + 24) < c.query.first_cm) return Result::More;
    const auto chunk = u32(s + 48);
    if (chunk != UINT32_MAX)
    {
        if (chunk >= info_.chunks || u64(s + 32) || u64(s + 40)) return Result::Invalid;
        c.chunk = data_ + uint64_t(chunk) * Page;
        c.vertex = 1;
    }
    else
    {
        const auto left = u64(s + 32), right = u64(s + 40);
        if (!left || left >= offset || (right && (right >= offset || right == left))) return Result::Invalid;
        if (size_t(c.depth) + (right ? 2 : 1) > c.stack.size()) return Result::BudgetExceeded;
        if (right) c.stack[c.depth++] = right;
        c.stack[c.depth++] = left;
    }
    return Result::More;
}
Result RouteArchiveReader::queryStep(RouteArchiveQueryCursor& c, size_t budget, RouteArchiveVisitor visitor, void* context)
{
    if (!budget || !visitor) return Result::Invalid;
    if (!file_ || c.generation != generation_) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    while (budget--)
    {
        const auto r = queryUnit(c, visitor, context);
        if (r == Result::Busy) return r;
        if (r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
Result RouteArchiveReader::beginValidation(RouteArchiveValidationCursor& c)
{
    ::new (static_cast<void*>(&c)) RouteArchiveValidationCursor{};
    if (!file_) return Result::Invalid;
    c.generation = generation_;
    c.terminal = Result::More;
    return Result::Ok;
}
Result RouteArchiveReader::validationUnit(RouteArchiveValidationCursor& c)
{
    auto* s = workspace_.scratch.data();
    if (c.stage == 0)
    {
        if (c.row == info_.legs)
        {
            c.row = 0;
            c.stage = 1;
            return Result::More;
        }
        const auto r = archiveIo(file_->readAt(Header + c.row * 16, s, 16));
        if (r != Result::Ok) return r;
        if (!crc(s, 16, 12) || u32(s) == UINT32_MAX || u32(s + 4) > u32(s + 8)) return Result::Invalid;
        ++c.row;
    }
    else if (c.stage == 1)
    {
        if (c.row == info_.chunks)
        {
            if (c.points != info_.points || c.previous.distance_cm != info_.length_cm) return Result::Invalid;
            c.row = index_;
            c.stage = 2;
            return Result::More;
        }
        cached_ = UINT64_MAX;
        auto* p = workspace_.page.data();
        const auto r = archiveIo(file_->readAt(data_ + c.row * Page, p, Page));
        if (r != Result::Ok) return r;
        const auto count = u32(p + 12);
        if (std::memcmp(p, "RCHK", 4) || !crc(p, Page, 4) || u32(p + 8) != c.row ||
            !count || count > RouteArchiveWorkspace::kPoints || (c.row + 1 < info_.chunks && count != RouteArchiveWorkspace::kPoints)) return Result::Invalid;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* q = p + 64 + i * 24;
            decodePoint(q, c.point);
            if (!validPoint(c.point.point) || u32(q + 20) > 1 || (info_.legs && c.point.leg >= info_.legs) ||
                (!info_.legs && c.point.leg) || c.point.distance_cm > info_.length_cm ||
                (c.points && c.point.distance_cm < c.previous.distance_cm)) return Result::Invalid;
            if (c.row && !i)
            {
                encodePoint(s, c.previous);
                if (std::memcmp(s, q, 24)) return Result::Invalid;
            }
            else
            {
                if (!c.points && c.point.distance_cm) return Result::Invalid;
                ++c.points;
            }
            c.previous = c.point;
        }
        ++c.row;
    }
    else
    {
        if (c.row == file_bytes_) return Result::Ok;
        const auto r = archiveIo(file_->readAt(c.row, s, 64));
        if (r != Result::Ok) return r;
        const auto box = bounds(s);
        if (!crc(s, 64, 60) || !validBounds(box) || box.west > box.east ||
            u64(s + 16) > u64(s + 24) || u64(s + 24) > info_.length_cm) return Result::Invalid;
        const auto left = u64(s + 32), right = u64(s + 40);
        if (u32(s + 48) != UINT32_MAX)
        {
            if (u32(s + 48) >= info_.chunks || left || right) return Result::Invalid;
        }
        else if (left < index_ || left >= c.row || (left - index_) % 64 ||
                 (right && (right < index_ || right >= c.row || right == left || (right - index_) % 64))) return Result::Invalid;
        c.row += 64;
    }
    return Result::More;
}
Result RouteArchiveReader::validationStep(RouteArchiveValidationCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (!file_ || c.generation != generation_) c.terminal = Result::Cancelled;
    if (c.terminal != Result::More) return c.terminal;
    while (budget--)
    {
        const auto r = validationUnit(c);
        if (r == Result::Busy) return r;
        if (r != Result::More) return c.terminal = r;
    }
    return Result::More;
}
} // namespace tmap
