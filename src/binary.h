#pragma once
#include "tmap/package.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
namespace tmap::detail
{
inline Result fixedFragment(OutdoorPackage& package, SectionType type, uint64_t offset, uint8_t* output, size_t bytes, uint8_t& completed)
{
    if (!bytes || bytes > UINT8_MAX || completed > bytes) return Result::Invalid;
    if (completed == bytes) return Result::Ok;
    const auto at = offset + completed;
    const auto count = std::min<size_t>(bytes - completed, package.payloadBytes() - at % package.payloadBytes());
    const auto result = package.read(type, at, output + completed, count);
    if (result != Result::Ok) return result;
    completed += static_cast<uint8_t>(count);
    return completed == bytes ? Result::Ok : Result::More;
}
// Scratch belongs to the consumer. Cache eviction cannot discard a completed
// fragment; a copied cursor rebinds its output to the copied scratch on retry.
inline Result resumableRead(OutdoorPackage& package, SectionType type, uint64_t offset, uint8_t* output, size_t bytes, ReadCursor& cursor)
{
    if (cursor.complete || cursor.type != type || cursor.offset != offset || cursor.bytes != bytes)
    {
        const auto result = beginRead(package, type, offset, output, bytes, cursor);
        if (result != Result::Ok) return result;
    }
    cursor.output = output;
    return readStep(package, cursor, 1);
}
inline Result resumableRecord(OutdoorPackage& package, SectionType type, uint64_t row, size_t stride, uint8_t* output, ReadCursor& cursor)
{
    const auto* section = package.section(type);
    if (!section) return package.isOpen() ? Result::Missing : Result::Invalid;
    if (!stride || row >= section->count || section->bytes % stride || section->count != section->bytes / stride) return Result::Invalid;
    return resumableRead(package, type, row * stride, output, stride, cursor);
}
// Aggregate assignment can materialize a large temporary on Xtensa. Construct
// directly in caller-owned storage; these types have trivial destruction.
template <class T>
inline void reset(T& output)
{
    static_assert(std::is_trivially_destructible<T>::value, "Scratch reset requires trivial ownership");
    ::new (static_cast<void*>(&output)) T{};
}
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
inline uint64_t u64(const uint8_t* p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }
inline int32_t i32(const uint8_t* p) { return static_cast<int32_t>(u32(p)); }
inline Bounds bounds(const uint8_t* p) { return {i32(p), i32(p + 4), i32(p + 8), i32(p + 12)}; }
inline bool range(uint64_t offset, uint64_t bytes, uint64_t capacity) { return offset <= capacity && bytes <= capacity - offset; }
inline bool coordinate(int32_t lat, int32_t lon) { return lat >= -900000000 && lat <= 900000000 && lon >= -1800000000 && lon <= 1800000000; }
constexpr double kPi = 3.14159265358979323846;
inline double distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2)
{
    const double a = lat1 * 1e-7 * kPi / 180, b = lat2 * 1e-7 * kPi / 180;
    const double x = std::sin((b - a) / 2), y = std::sin((double(lon2) - lon1) * 1e-7 * kPi / 360);
    const double h = x * x + std::cos(a) * std::cos(b) * y * y;
    return 12742000 * std::asin(std::sqrt(std::fmin(1.0, std::fmax(0.0, h))));
}
} // namespace tmap::detail
