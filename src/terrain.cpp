#include "tmap/terrain.h"
#include "binary.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tmap
{
using namespace detail;
double distanceMeters(GeoPoint a, GeoPoint b) { return distance(a.latitude, a.longitude, b.latitude, b.longitude); }
Result TerrainReader::initialize()
{
    initialized_ = false;
    reset(elevation_cursor_);
    uint8_t r[64]{};
    auto status = package_.read(SectionType::Terrain, 0, r, sizeof(r));
    if (status != Result::Ok) return status;
    auto& pending = info_;
    pending = {};
    pending.west = i32(r);
    pending.south = i32(r + 4);
    pending.step_lon = i32(r + 8);
    pending.step_lat = i32(r + 12);
    pending.columns = u32(r + 16);
    pending.rows = u32(r + 20);
    pending.block_side = u16(r + 24);
    pending.resolution_m = u32(r + 28);
    pending.vertical_datum = u32(r + 32);
    std::copy(r + 48, r + 64, pending.source.begin());
    if (!coordinate(pending.south, pending.west) || pending.step_lon <= 0 || pending.step_lat <= 0 ||
        pending.columns < 2 || pending.rows < 2 || pending.block_side < 2 || pending.block_side > 64 ||
        u16(r + 26) != 1 || !pending.resolution_m || u32(r + 36) || u64(r + 40) ||
        int64_t(pending.west) + int64_t(pending.columns - 1) * pending.step_lon > 1800000000 ||
        int64_t(pending.south) + int64_t(pending.rows - 1) * pending.step_lat > 900000000) return Result::Invalid;
    const auto* index = package_.section(SectionType::DemIndex);
    const auto* data = package_.section(SectionType::DemData);
    const uint64_t nx = (uint64_t(pending.columns) + pending.block_side - 1) / pending.block_side;
    const uint64_t ny = (uint64_t(pending.rows) + pending.block_side - 1) / pending.block_side;
    if (!index || !data || index->count != nx * ny || index->count > index->bytes / 32 || index->bytes != index->count * 32) return Result::Invalid;
    generation_ = package_.generation();
    initialized_ = true;
    return Result::Ok;
}
Result TerrainReader::sample(ElevationCursor& c, uint32_t x, uint32_t y, int16_t& output)
{
    if (!c.sample_pending)
    {
        const uint64_t nx = (uint64_t(info_.columns) + info_.block_side - 1) / info_.block_side;
        const auto bx = x / info_.block_side, by = y / info_.block_side;
        uint8_t record[32]{};
        auto r = package_.record(SectionType::DemIndex, by * nx + bx, 32, record);
        if (r != Result::Ok) return r;
        const auto col = u32(record), row = u32(record + 4);
        const auto w = u16(record + 8), h = u16(record + 10);
        const auto offset = u64(record + 16);
        const auto length = u32(record + 24);
        if (col != bx * info_.block_side || row != by * info_.block_side ||
            w != std::min<uint32_t>(info_.block_side, info_.columns - col) ||
            h != std::min<uint32_t>(info_.block_side, info_.rows - row) || u32(record + 12) || u32(record + 28) || length != uint32_t(w) * h * 2)
            return Result::Invalid;
        const auto* data = package_.section(SectionType::DemData);
        if (!data || !range(offset, length, data->bytes)) return Result::Invalid;
        c.sample_offset = offset + ((y - row) * w + x - col) * 2;
        c.sample_pending = true;
    }
    uint8_t value[2]{};
    const auto r = package_.read(SectionType::DemData, c.sample_offset, value, 2);
    if (r == Result::Ok)
    {
        output = static_cast<int16_t>(u16(value));
        c.sample_pending = false;
    }
    return r;
}
Result TerrainReader::elevation(GeoPoint point, ElevationSample& output)
{
    auto& c = elevation_cursor_;
    if (c.complete || c.point.latitude != point.latitude || c.point.longitude != point.longitude || c.generation != package_.generation())
    {
        const auto r = beginElevation(point, c);
        if (r != Result::Ok) return r;
    }
    return elevationStep(c, 4, output);
}
void cancelElevation(ElevationCursor& c)
{
    c.complete = true;
    c.terminal = Result::Cancelled;
}
Result TerrainReader::beginElevation(GeoPoint point, ElevationCursor& c)
{
    reset(c);
    if (!initialized_)
    {
        const auto r = initialize();
        if (r != Result::Ok) return r;
    }
    if (!package_.isOpen() || generation_ != package_.generation()) return Result::Cancelled;
    if (!coordinate(point.latitude, point.longitude)) return Result::Invalid;
    const int64_t x = int64_t(point.longitude) - info_.west, y = int64_t(point.latitude) - info_.south;
    if (x < 0 || y < 0 || x > int64_t(info_.columns - 1) * info_.step_lon || y > int64_t(info_.rows - 1) * info_.step_lat) return Result::Missing;
    c.x = std::min<uint32_t>(static_cast<uint32_t>(x / info_.step_lon), info_.columns - 2);
    c.y = std::min<uint32_t>(static_cast<uint32_t>(y / info_.step_lat), info_.rows - 2);
    c.fraction_x = static_cast<uint32_t>(x - int64_t(c.x) * info_.step_lon);
    c.fraction_y = static_cast<uint32_t>(y - int64_t(c.y) * info_.step_lat);
    c.point = point;
    c.generation = generation_;
    c.complete = false;
    c.terminal = Result::More;
    return Result::Ok;
}
Result TerrainReader::elevationStep(ElevationCursor& c, size_t budget, ElevationSample& output)
{
    if (!budget) return Result::Invalid;
    if (c.complete && c.terminal != Result::Ok) return c.terminal;
    if (!initialized_ || !package_.isOpen() || c.generation != generation_ || c.generation != package_.generation())
    {
        cancelElevation(c);
        return c.terminal;
    }
    if (c.complete)
    {
        output = {c.value, info_.resolution_m, info_.vertical_datum};
        return c.terminal;
    }
    for (size_t work = 0; work < budget && c.next < 4; ++work)
    {
        const auto i = c.next;
        const auto wx = i % 2 ? c.fraction_x : uint32_t(info_.step_lon) - c.fraction_x;
        const auto wy = i / 2 ? c.fraction_y : uint32_t(info_.step_lat) - c.fraction_y;
        const double weight = double(uint64_t(wx) * wy) / (uint64_t(info_.step_lon) * info_.step_lat);
        if (weight == 0)
        {
            ++c.next;
            continue;
        }
        int16_t z = 0;
        const auto r = sample(c, c.x + (i % 2), c.y + (i / 2), z);
        if (r == Result::Busy || r == Result::More) return r;
        if (r != Result::Ok || z == std::numeric_limits<int16_t>::min())
        {
            c.complete = true;
            c.terminal = r == Result::Ok ? Result::Missing : r;
            return c.terminal;
        }
        c.value += weight * z;
        ++c.next;
    }
    if (c.next == 4)
    {
        output = {c.value, info_.resolution_m, info_.vertical_datum};
        c.complete = true;
        c.terminal = Result::Ok;
    }
    return c.terminal;
}
Result beginProfile(TerrainReader& terrain, OutdoorPackage& package, const GeoPoint* points, size_t count,
                    const ProfileOptions& options, ProfileCursor& c)
{
    reset(c);
    if (!points || count < 2 || !std::isfinite(options.spacing_m) || options.spacing_m <= 0 ||
        !std::isfinite(options.noise_threshold_m) || options.noise_threshold_m < 0) return Result::Invalid;
    for (size_t i = 0; i < count; ++i)
    {
        if (!coordinate(points[i].latitude, points[i].longitude)) return Result::Invalid;
        if (i && std::abs(int64_t(points[i].longitude) - points[i - 1].longitude) > 1800000000) return Result::Unsupported;
    }
    auto r = terrain.initialize();
    if (r != Result::Ok) return r;
    c.points = points;
    c.count = count;
    c.options = options;
    c.generation = package.generation();
    c.complete = false;
    return Result::Ok;
}
namespace
{
void prepareProfileSample(ProfileCursor& c)
{
    auto& sample = c.scratch;
    reset(sample);
    const auto interpolate = [&c](int32_t a, int32_t b)
    {
        const int64_t numerator = int64_t(a) * c.subdivisions + (int64_t(b) - a) * c.subdivision;
        const int64_t denominator = c.subdivisions;
        return int32_t(numerator >= 0 ? (numerator + denominator / 2) / denominator
                                      : -((-numerator + denominator / 2) / denominator));
    };
    const auto& a = c.points[c.segment];
    const auto& b = c.points[c.segment + 1];
    sample.point = {interpolate(a.latitude, b.latitude), interpolate(a.longitude, b.longitude)};
    sample.distance_m = c.segment_start_m + c.segment_length_m * (double(c.subdivision) / c.subdivisions);
}
} // namespace
Result profileStep(TerrainReader& terrain, OutdoorPackage& package, ProfileCursor& c, size_t budget,
                   ProfileVisitor visitor, void* context)
{
    if (!visitor || !budget || !c.points) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!package.isOpen() || c.generation != package.generation()) return Result::Cancelled;
    if (c.complete) return c.metrics.gaps ? Result::Incomplete : Result::Ok;
    for (size_t work = 0; work < budget; ++work)
    {
        if (!c.started)
        {
            c.segment_length_m = distanceMeters(c.points[c.segment], c.points[c.segment + 1]);
            const double divisions = std::ceil(c.segment_length_m / c.options.spacing_m);
            if (!std::isfinite(divisions) || divisions > 10000000) return Result::BudgetExceeded;
            c.subdivisions = std::max<size_t>(1, static_cast<size_t>(divisions));
            c.subdivision = c.segment == 0 ? 0 : 1;
            c.started = true;
        }
        prepareProfileSample(c);
        auto& sample = c.scratch;
        const auto r = terrain.elevation(sample.point, c.elevation_scratch);
        if (r != Result::Ok && r != Result::Missing) return r;
        sample.valid = r == Result::Ok;
        sample.elevation_m = r == Result::Ok ? c.elevation_scratch.meters : 0;
        sample.break_before = !c.previous_valid;
        if (!visitor(context, sample))
        {
            c.complete = true;
            c.cancelled = true;
            return Result::Cancelled;
        }
        ++c.metrics.samples;
        c.metrics.distance_m = sample.distance_m;
        if (sample.valid)
        {
            if (!c.metrics.valid_samples) c.metrics.minimum_m = c.metrics.maximum_m = sample.elevation_m;
            c.metrics.minimum_m = std::min(c.metrics.minimum_m, sample.elevation_m);
            c.metrics.maximum_m = std::max(c.metrics.maximum_m, sample.elevation_m);
            ++c.metrics.valid_samples;
            if (c.previous_valid) c.metrics.valid_distance_m += sample.distance_m - c.last_distance_m;
            if (!c.filter_initialized)
            {
                c.accepted_m = sample.elevation_m;
                c.filter_initialized = true;
            }
            else
            {
                const double delta = sample.elevation_m - c.accepted_m;
                if (std::abs(delta) >= c.options.noise_threshold_m)
                {
                    if (delta >= 0) c.metrics.ascent_m += delta;
                    else c.metrics.descent_m -= delta;
                    c.accepted_m = sample.elevation_m;
                }
            }
        }
        else
        {
            if (c.previous_valid || c.metrics.samples == 1) ++c.metrics.gaps;
            c.filter_initialized = false;
        }
        c.previous_valid = sample.valid;
        c.last_distance_m = sample.distance_m;
        if (++c.subdivision > c.subdivisions)
        {
            c.segment_start_m += c.segment_length_m;
            c.started = false;
            if (++c.segment + 1 == c.count)
            {
                c.complete = true;
                return c.metrics.gaps ? Result::Incomplete : Result::Ok;
            }
        }
    }
    return Result::More;
}
ContourPolicy contourPolicy(uint8_t zoom)
{
    if (zoom < 12 || zoom > 18) return {};
    if (zoom < 14) return {100, 100, true};
    if (zoom < 16) return {20, 100, true};
    return {10, 50, true};
}
Result beginContours(OutdoorPackage& package, const Bounds& box, uint8_t zoom, ContourCursor& c)
{
    const auto request = box;
    reset(c);
    if (!package.isOpen() || !coordinate(request.south, request.west) || !coordinate(request.north, request.east) ||
        request.west >= request.east || request.south >= request.north) return Result::Invalid;
    c.policy = contourPolicy(zoom);
    c.zoom = zoom;
    c.bounds = request;
    c.generation = package.generation();
    if (!c.policy.interval_m) return Result::Ok;
    const auto* overview_index = package.section(SectionType::ContourOverviewIndex);
    const auto* overview_data = package.section(SectionType::ContourOverviewData);
    if ((overview_index != nullptr) != (overview_data != nullptr)) return Result::Invalid;
    if (zoom <= 14 && overview_index)
    {
        c.overview = true;
        c.index_type = SectionType::ContourOverviewIndex;
        c.data_type = SectionType::ContourOverviewData;
    }
    const auto* index = package.section(c.index_type);
    if (!index) return Result::Missing;
    if (!index->count) return Result::Ok;
    if (index->count > std::numeric_limits<uint32_t>::max() || index->bytes != index->count * 48) return Result::Invalid;
    c.depth = 1;
    c.complete = false;
    return Result::Ok;
}
namespace
{
uint32_t heightMask(uint32_t interval)
{
    if (interval >= 100 && interval % 100 == 0) return 1;
    if (!interval || 100 % interval) return 0xfffff;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 20; ++i)
        if ((i * 5) % interval == 0) mask |= 1U << i;
    return mask;
}
Result varint(const uint8_t* bytes, size_t count, size_t& at, int32_t& out)
{
    uint32_t n = 0;
    size_t pos = at;
    for (unsigned i = 0; i < 5; ++i)
    {
        if (pos >= count) return Result::Invalid;
        const auto b = bytes[pos];
        if (i == 4 && (b & 0xf0)) return Result::Invalid;
        n |= uint32_t(b & 127) << (i * 7);
        ++pos;
        if (!(b & 128))
        {
            out = static_cast<int32_t>((n >> 1) ^ (0U - (n & 1)));
            at = pos;
            return Result::Ok;
        }
    }
    return Result::Invalid;
}
} // namespace
Result contourStep(OutdoorPackage& package, ContourCursor& c, size_t budget, ContourVisitor visitor, void* context)
{
    if (!visitor || !budget) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!package.isOpen() || c.generation != package.generation()) return Result::Cancelled;
    if (c.complete) return Result::Ok;
    if (c.overview && c.policy.interval_m % 20 && c.policy.interval_m % 50) return Result::Unsupported;
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.points && c.seen < c.points)
        {
            if (c.offset >= c.end) return Result::Invalid;
            const auto available = static_cast<size_t>(std::min<uint64_t>(c.lod ? 11 : 10, c.end - c.offset));
            auto r = resumableRead(package, c.data_type, c.offset, c.read_bytes.data(), available, c.read);
            if (r != Result::Ok) return r;
            size_t pos = 0;
            int32_t dy = 0, dx = 0;
            r = varint(c.read_bytes.data(), available, pos, dy);
            if (r == Result::Ok) r = varint(c.read_bytes.data(), available, pos, dx);
            if (r != Result::Ok) return r;
            uint8_t min_zoom = 12;
            if (c.lod)
            {
                if (pos >= available) return Result::Invalid;
                min_zoom = c.read_bytes[pos];
                ++pos;
                if (min_zoom < 12 || min_zoom > 18 || ((c.seen == 0 || c.seen + 1 == c.points) && min_zoom != 12)) return Result::Invalid;
            }
            const int64_t lat = int64_t(c.latitude) + dy, lon = int64_t(c.longitude) + dx;
            if (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000) return Result::Invalid;
            ContourVertex v{{int32_t(lat), int32_t(lon)}, c.id, c.elevation_m, c.seen == 0, c.seen + 1 == c.points};
            if (min_zoom <= c.zoom && !visitor(context, v))
            {
                c.complete = true;
                c.cancelled = true;
                return Result::Cancelled;
            }
            c.latitude = v.point.latitude;
            c.longitude = v.point.longitude;
            c.offset += pos;
            ++c.seen;
            if (c.seen == c.points && c.offset != c.end) return Result::Invalid;
            continue;
        }
        if (c.remaining_lines)
        {
            if (!range(c.offset, 24, c.block_end)) return Result::Invalid;
            auto* h = c.read_bytes.data();
            auto r = resumableRead(package, c.data_type, c.offset, h, 24, c.read);
            if (r != Result::Ok) return r;
            const auto points = u32(h + 12), bytes = u32(h + 16);
            const auto* data = package.section(c.data_type);
            if (!data || !u64(h) || points < 2 || points > 1000000 || bytes < points * ((u32(h + 20) & 1U) ? 3ULL : 2ULL) || (u32(h + 20) & ~1U) ||
                !range(c.offset, 24, data->bytes) || !range(c.offset + 24, bytes, c.block_end)) return Result::Invalid;
            c.id = u64(h);
            c.elevation_m = i32(h + 8);
            c.offset += 24;
            c.end = c.offset + bytes;
            c.points = points;
            c.lod = (u32(h + 20) & 1U) != 0;
            c.seen = 0;
            c.latitude = c.longitude = 0;
            --c.remaining_lines;
            if (c.elevation_m % int32_t(c.policy.interval_m))
            {
                c.offset = c.end;
                c.points = 0;
            }
            continue;
        }
        if (c.block_end && c.offset != c.block_end) return Result::Invalid;
        if (!c.depth)
        {
            c.complete = true;
            return Result::Ok;
        }
        auto& frame = c.stack[c.depth - 1];
        if (!frame.loaded)
        {
            auto* row = c.read_bytes.data();
            auto r = resumableRecord(package, c.index_type, frame.row, 48, row, c.read);
            if (r != Result::Ok) return r;
            const auto box = bounds(row);
            const auto first = u32(row + 16);
            const auto count = u16(row + 20);
            const auto level = row[22];
            const auto* index = package.section(c.index_type);
            if (!coordinate(box.south, box.west) || !coordinate(box.north, box.east) || box.west > box.east || box.south > box.north ||
                (row[23] & ~1U) || (!(row[23] & 1U) && u32(row + 36)) || (u32(row + 36) & 0x7ff00000U) || u64(row + 40) || !count || (c.depth > 1 && level + 1 != c.stack[c.depth - 2].level)) return Result::Invalid;
            if (!box.intersects(c.bounds))
            {
                --c.depth;
                continue;
            }
            if ((row[23] & 1U) && !(u32(row + 36) & 0x80000000U) && !(u32(row + 36) & heightMask(c.policy.interval_m)))
            {
                --c.depth;
                continue;
            }
            if (!level)
            {
                const auto* data = package.section(c.data_type);
                if (!data || first || !range(u64(row + 24), u32(row + 32), data->bytes)) return Result::Invalid;
                c.offset = u64(row + 24);
                c.block_end = c.offset + u32(row + 32);
                c.remaining_lines = count;
                c.points = 0;
                --c.depth;
                continue;
            }
            if (!index || level >= c.stack.size() || first <= frame.row || !range(first, count, index->count) || u64(row + 24) || u32(row + 32)) return Result::Invalid;
            frame.next = first;
            frame.count = count;
            frame.level = level;
            frame.loaded = true;
        }
        else if (frame.count)
        {
            if (c.depth == c.stack.size()) return Result::Invalid;
            const auto row = frame.next++;
            --frame.count;
            c.stack[c.depth++] = {row, 0, 0, 0, false};
        }
        else --c.depth;
    }
    return Result::More;
}
bool project(const MapViewport& v, GeoPoint p, ScreenPoint& output)
{
    if (v.zoom > 29 || !coordinate(p.latitude, p.longitude) || !coordinate(v.center.latitude, v.center.longitude) ||
        !std::isfinite(v.width) || !std::isfinite(v.height) || v.width <= 0 || v.height <= 0) return false;
    constexpr float pi = 3.14159265358979323846f;
    const float scale = std::ldexp(256.0f, v.zoom);
    int64_t dx = int64_t(p.longitude) - v.center.longitude;
    if (dx > 1800000000) dx -= 3600000000LL;
    else if (dx < -1800000000) dx += 3600000000LL;
    const auto lat = std::clamp<int32_t>(p.latitude, -850511287, 850511287), center = std::clamp<int32_t>(v.center.latitude, -850511287, 850511287);
    const float a = pi / 4 + float(lat) * 1e-7f * pi / 360;
    const float b = pi / 4 + float(center) * 1e-7f * pi / 360;
    // Stable local Mercator difference: avoid subtracting two large world
    // coordinates in single precision at z18.
    const float ratio = std::sin(float(int64_t(lat) - center) * 1e-7f * pi / 360) / (std::cos(a) * std::sin(b));
    const float dy = -std::log1p(ratio) / (2 * pi);
    output = {v.width / 2 + float(dx) * (scale / 3600000000.0f), v.height / 2 + dy * scale};
    return true;
}
bool clipSegment(float width, float height, ScreenPoint& a, ScreenPoint& b)
{
    if (width <= 0 || height <= 0 || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y)) return false;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x, width - a.x, a.y, height - a.y};
    float lo = 0, hi = 1;
    for (size_t i = 0; i < 4; ++i)
    {
        if (p[i] == 0)
        {
            if (q[i] < 0) return false;
        }
        else if (p[i] < 0) lo = std::max(lo, q[i] / p[i]);
        else hi = std::min(hi, q[i] / p[i]);
        if (lo > hi) return false;
    }
    b = {a.x + hi * dx, a.y + hi * dy};
    a = {a.x + lo * dx, a.y + lo * dy};
    return true;
}
} // namespace tmap
