#pragma once
#include "tmap/route_archive.h"
#include <algorithm>
#include <cstring>
namespace tmap
{
namespace route_detail
{
constexpr uint64_t Header = RouteArchiveWorkspace::kHeaderBytes, Page = RouteArchiveWorkspace::kPageBytes;
constexpr uint32_t MaxChunks = 1u << 30;
inline uint64_t archiveNodeCount(uint32_t chunks)
{
    uint64_t nodes = 0;
    for (uint64_t width = chunks; width; width = width == 1 ? 0 : (width + 1) / 2) nodes += width;
    return nodes;
}
inline uint32_t u32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
inline void put32(uint8_t* p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline void put64(uint8_t* p, uint64_t v)
{
    put32(p, uint32_t(v));
    put32(p + 4, uint32_t(v >> 32));
}
inline void encodeLeg(uint8_t* p, const IntegerRouteLeg& leg)
{
    put32(p, leg.edge);
    put32(p + 4, leg.first_cm);
    put32(p + 8, leg.last_cm);
}
inline void decodeLeg(const uint8_t* p, IntegerRouteLeg& leg)
{
    leg.edge = u32(p);
    leg.first_cm = u32(p + 4);
    leg.last_cm = u32(p + 8);
}
inline void encodePoint(uint8_t* p, const RouteArchivePoint& point)
{
    put32(p, uint32_t(point.point.latitude));
    put32(p + 4, uint32_t(point.point.longitude));
    put64(p + 8, point.distance_cm);
    put32(p + 16, point.leg);
    put32(p + 20, point.break_before ? 1 : 0);
}
inline void decodePoint(const uint8_t* p, RouteArchivePoint& point)
{
    point.point.latitude = int32_t(u32(p));
    point.point.longitude = int32_t(u32(p + 4));
    point.distance_cm = u64(p + 8);
    point.leg = u32(p + 16);
    point.break_before = u32(p + 20) != 0;
}
inline bool validPoint(GeoPoint p)
{
    return p.latitude >= -900000000 && p.latitude <= 900000000 && p.longitude >= -1800000000 && p.longitude <= 1800000000;
}
inline bool validBounds(const Bounds& b)
{
    return validPoint({b.south, b.west}) && validPoint({b.north, b.east}) && b.south <= b.north;
}
inline Bounds bounds(const uint8_t* p) { return {int32_t(u32(p)), int32_t(u32(p + 4)), int32_t(u32(p + 8)), int32_t(u32(p + 12))}; }
inline void storeBounds(uint8_t* p, const Bounds& b)
{
    put32(p, uint32_t(b.west));
    put32(p + 4, uint32_t(b.south));
    put32(p + 8, uint32_t(b.east));
    put32(p + 12, uint32_t(b.north));
}
inline bool intersects(const Bounds& b, const Bounds& view)
{
    if (b.north < view.south || b.south > view.north) return false;
    return view.west <= view.east ? b.west <= view.east && b.east >= view.west : b.east >= view.west || b.west <= view.east;
}
inline uint32_t archiveChecksum(const uint8_t* bytes, size_t count)
{
    static constexpr uint32_t table[16] = {0, 0x105ec76f, 0x20bd8ede, 0x30e349b1, 0x417b1dbc, 0x5125dad3, 0x61c69362, 0x7198540d,
                                           0x82f63b78, 0x92a8fc17, 0xa24bb5a6, 0xb21572c9, 0xc38d26c4, 0xd3d3e1ab, 0xe330a81a, 0xf36e6f75};
    uint32_t value = UINT32_MAX;
    while (count--)
    {
        value ^= *bytes++;
        value = (value >> 4) ^ table[value & 15];
        value = (value >> 4) ^ table[value & 15];
    }
    return ~value;
}
inline Result archiveIo(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return Result::Ok;
    case Status::Missing:
        return Result::Missing;
    case Status::Busy:
        return Result::Busy;
    case Status::More:
        return Result::More;
    case Status::Invalid:
        return Result::Invalid;
    case Status::Cancelled:
        return Result::Cancelled;
    default:
        return Result::IoError;
    }
}
inline bool crc(uint8_t* p, size_t bytes, size_t offset)
{
    const auto expected = u32(p + offset);
    put32(p + offset, 0);
    const bool ok = archiveChecksum(p, bytes) == expected;
    put32(p + offset, expected);
    return ok;
}
inline void seal(uint8_t* p, size_t bytes, size_t offset)
{
    put32(p + offset, 0);
    put32(p + offset, archiveChecksum(p, bytes));
}
inline bool visible(GeoPoint a, GeoPoint b, const Bounds& view)
{
    // Integer conservative bounds; projected clipping belongs to the callback.
    int64_t bx = b.longitude;
    if (bx - a.longitude > 1800000000LL) bx -= 3600000000LL;
    if (bx - a.longitude < -1800000000LL) bx += 3600000000LL;
    const int64_t east = int64_t(view.east) + (view.west > view.east ? 3600000000LL : 0);
    for (int shift = -1; shift <= 1; ++shift)
        if (std::max(int64_t(a.longitude), bx) >= view.west + shift * 3600000000LL &&
            std::min(int64_t(a.longitude), bx) <= east + shift * 3600000000LL &&
            std::max(a.latitude, b.latitude) >= view.south && std::min(a.latitude, b.latitude) <= view.north) return true;
    return false;
}
} // namespace route_detail
} // namespace tmap
