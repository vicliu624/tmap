#include "route_archive_wire.h"
#include <cstdlib>
namespace tmap
{
using namespace route_detail;
Result RouteArchiveWriter::begin(WritableRouteFile& file, const OutdoorInfo& map, uint32_t legs, uint64_t cost, uint64_t max_bytes,
                                 AscentRatio ascent, TravelMode mode)
{
    if (terminal_ == Result::More)
    {
        if (stage_ != 255 || file_ != &file || info_.map_id != map.id || info_.map_build != map.build ||
            info_.map_series != map.series || info_.map_revision != map.revision || info_.legs != legs ||
            info_.cost_units != cost || max_bytes_ != max_bytes || info_.ascent.numerator != ascent.numerator ||
            info_.ascent.denominator != ascent.denominator || info_.mode != mode) return Result::Invalid;
    }
    else
    {
        if (file.size() || !ascent.denominator || (mode != TravelMode::Foot && mode != TravelMode::Bicycle)) return Result::Invalid;
        const uint64_t data = (Header + uint64_t(legs) * 16 + Page - 1) / Page * Page;
        if (data > max_bytes || max_bytes - data < Page + 64) return Result::BudgetExceeded;
        info_.map_id = map.id;
        info_.map_build = map.build;
        info_.map_series = map.series;
        info_.map_revision = map.revision;
        info_.cost_units = cost;
        info_.ascent = ascent;
        info_.mode = mode;
        info_.legs = legs;
        info_.length_cm = info_.points = 0;
        info_.chunks = legs_written_ = fill_ = row_ = level_count_ = 0;
        data_ = data;
        max_bytes_ = max_bytes;
        have_previous_ = false;
        file_ = &file;
        substage_ = 0;
        stage_ = 255;
        terminal_ = Result::More;
        workspace_.page.fill(0);
    }
    workspace_.scratch.fill(0);
    const auto r = archiveIo(file.writeAt(0, workspace_.scratch.data(), Header));
    if (r == Result::Ok) stage_ = 0;
    else if (r != Result::Busy && r != Result::More) terminal_ = r;
    return r;
}
Result RouteArchiveWriter::writeLeg(uint32_t index, const IntegerRouteLeg& leg)
{
    if (terminal_ != Result::More || stage_ || index != legs_written_ || index >= info_.legs) return Result::Invalid;
    if (leg.edge == UINT32_MAX || leg.first_cm > leg.last_cm) return Result::Invalid;
    encodeLeg(workspace_.scratch.data(), leg);
    seal(workspace_.scratch.data(), 16, 12);
    const auto r = archiveIo(file_->writeAt(Header + uint64_t(index) * 16, workspace_.scratch.data(), 16));
    if (r == Result::Ok) ++legs_written_;
    else if (r != Result::Busy && r != Result::More) terminal_ = r;
    return r;
}
Result RouteArchiveWriter::flush()
{
    if (!fill_) return Result::Ok;
    if (info_.chunks >= MaxChunks) return Result::BudgetExceeded;
    // Exact index reservation includes unary parents in odd-width levels.
    const uint64_t count = uint64_t(info_.chunks) + 1, end = data_ + count * Page;
    const uint64_t nodes = archiveNodeCount(uint32_t(count));
    if (end > max_bytes_ || nodes * 64 > max_bytes_ - end) return Result::BudgetExceeded;
    auto* p = workspace_.page.data();
    std::memcpy(p, "RCHK", 4);
    put32(p + 8, info_.chunks);
    put32(p + 12, fill_);
    Bounds box{previous_.point.longitude, previous_.point.latitude, previous_.point.longitude, previous_.point.latitude};
    for (uint32_t i = 0; i < fill_; ++i)
    {
        const auto* q = p + 64 + i * 24;
        const auto lat = int32_t(u32(q)), lon = int32_t(u32(q + 4));
        box.west = std::min(box.west, lon);
        box.east = std::max(box.east, lon);
        box.south = std::min(box.south, lat);
        box.north = std::max(box.north, lat);
        // A date-line segment must not be falsely rejected by its box.
        if (i && std::abs(int64_t(lon) - int32_t(u32(q - 20))) > 1800000000LL)
        {
            box.west = -1800000000;
            box.east = 1800000000;
        }
    }
    storeBounds(p + 16, box);
    put64(p + 32, u64(p + 64 + 8));
    put64(p + 40, previous_.distance_cm);
    seal(p, Page, 4);
    const auto r = archiveIo(file_->writeAt(data_ + uint64_t(info_.chunks) * Page, p, Page));
    if (r == Result::Ok)
    {
        ++info_.chunks;
        workspace_.page.fill(0);
        encodePoint(p + 64, previous_);
        fill_ = 1; // Seam overlap: every segment belongs to exactly one page.
    }
    return r;
}
Result RouteArchiveWriter::append(const RouteArchivePoint& point)
{
    if (terminal_ != Result::More || stage_ || legs_written_ != info_.legs || !validPoint(point.point) ||
        (info_.legs ? point.leg >= info_.legs : point.leg != 0) || (!have_previous_ && point.distance_cm) ||
        (have_previous_ && point.distance_cm < previous_.distance_cm)) return Result::Invalid;
    if (fill_ == RouteArchiveWorkspace::kPoints)
    {
        const auto r = flush();
        if (r != Result::Ok)
        {
            if (r != Result::Busy && r != Result::More) terminal_ = r;
            return r;
        }
    }
    encodePoint(workspace_.page.data() + 64 + fill_ * 24, point);
    previous_ = point;
    have_previous_ = true;
    ++fill_;
    ++info_.points;
    info_.length_cm = point.distance_cm;
    return Result::Ok;
}
Result RouteArchiveWriter::finishUnit()
{
    auto* s = workspace_.scratch.data();
    if (stage_ == 1)
    {
        if (!info_.points || legs_written_ != info_.legs) return Result::Invalid;
        const auto r = flush();
        if (r != Result::Ok) return r;
        fill_ = 0;
        index_ = data_ + uint64_t(info_.chunks) * Page;
        row_ = 0;
        stage_ = 2;
    }
    else if (stage_ == 2)
    {
        if (row_ == info_.chunks)
        {
            cursor_ = index_;
            next_level_ = file_bytes_ = index_ + uint64_t(info_.chunks) * 64;
            level_count_ = info_.chunks;
            row_ = 0;
            stage_ = 3;
            return Result::More;
        }
        if (!substage_)
        {
            const auto r = archiveIo(file_->readAt(data_ + uint64_t(row_) * Page, workspace_.page.data(), Page));
            if (r != Result::Ok) return r;
            if (!crc(workspace_.page.data(), Page, 4)) return Result::Invalid;
            std::memset(s, 0, 64);
            std::memcpy(s, workspace_.page.data() + 16, 32);
            put32(s + 48, row_);
            seal(s, 64, 60);
            substage_ = 1;
        }
        else
        {
            const auto r = archiveIo(file_->writeAt(index_ + uint64_t(row_) * 64, s, 64));
            if (r != Result::Ok) return r;
            substage_ = 0;
            ++row_;
        }
    }
    else if (stage_ == 3)
    {
        if (level_count_ == 1)
        {
            root_ = cursor_;
            stage_ = 4;
            return Result::More;
        }
        if (row_ >= level_count_)
        {
            cursor_ = next_level_;
            next_level_ = file_bytes_;
            level_count_ = (level_count_ + 1) / 2;
            row_ = 0;
            return Result::More;
        }
        if (substage_ < 2)
        {
            const uint32_t child = row_ + substage_;
            if (child < level_count_)
            {
                const auto r = archiveIo(file_->readAt(cursor_ + uint64_t(child) * 64, s + substage_ * 64, 64));
                if (r != Result::Ok) return r;
                if (!crc(s + substage_ * 64, 64, 60)) return Result::Invalid;
            }
            else std::memcpy(s + 64, s, 64);
            ++substage_;
        }
        else
        {
            auto* out = s + 128;
            std::memset(out, 0, 64);
            const auto a = bounds(s), b = bounds(s + 64);
            storeBounds(out, {std::min(a.west, b.west), std::min(a.south, b.south), std::max(a.east, b.east), std::max(a.north, b.north)});
            put64(out + 16, u64(s + 16));
            put64(out + 24, u64(s + 64 + 24));
            put64(out + 32, cursor_ + uint64_t(row_) * 64);
            put64(out + 40, row_ + 1 < level_count_ ? cursor_ + uint64_t(row_ + 1) * 64 : 0);
            put32(out + 48, UINT32_MAX);
            seal(out, 64, 60);
            if (file_bytes_ > max_bytes_ || max_bytes_ - file_bytes_ < 64) return Result::BudgetExceeded;
            const auto r = archiveIo(file_->writeAt(file_bytes_, out, 64));
            if (r != Result::Ok) return r;
            file_bytes_ += 64;
            row_ += 2;
            substage_ = 0;
        }
    }
    else
    {
        workspace_.scratch.fill(0);
        std::memcpy(s, "TMRT", 4);
        put32(s + 4, 1);
        put32(s + 8, uint32_t(Header));
        put32(s + 12, 1);
        std::memcpy(s + 16, info_.map_id.data(), 16);
        std::memcpy(s + 32, info_.map_build.data(), 16);
        std::memcpy(s + 48, info_.map_series.data(), 16);
        put64(s + 64, info_.map_revision);
        put64(s + 72, info_.cost_units);
        put64(s + 80, info_.length_cm);
        put64(s + 88, info_.points);
        put32(s + 96, info_.legs);
        put32(s + 100, info_.chunks);
        put64(s + 104, data_);
        put64(s + 112, index_);
        put64(s + 120, root_);
        put64(s + 128, file_bytes_);
        put32(s + 140, uint32_t(info_.mode));
        put32(s + 144, info_.ascent.numerator);
        put32(s + 148, info_.ascent.denominator);
        seal(s, Header, 136);
        const auto r = archiveIo(file_->writeAt(0, s, Header));
        if (r != Result::Ok) return r;
        terminal_ = Result::Ok;
        return Result::Ok;
    }
    return Result::More;
}
Result RouteArchiveWriter::finishStep(size_t budget)
{
    if (!budget) return Result::Invalid;
    if (terminal_ != Result::More) return terminal_;
    if (stage_ == 255) return Result::Invalid;
    if (!stage_) stage_ = 1;
    while (budget--)
    {
        const auto r = finishUnit();
        if (r == Result::Busy || r == Result::More)
        {
            if (r == Result::Busy) return r;
        }
        else return terminal_ = r;
    }
    return Result::More;
}
void RouteArchiveWriter::cancel()
{
    if (terminal_ == Result::More) terminal_ = Result::Cancelled;
}
} // namespace tmap
