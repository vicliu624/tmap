#include "tmap/routing_profile.h"

namespace tmap
{
namespace
{
uint32_t word(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
} // namespace
Result readRoutingProfile(OutdoorPackage& package, RoutingProfile& output)
{
    if (!package.isOpen()) return Result::Invalid;
    const auto* section = package.section(SectionType::RoutingProfile);
    if (!section) return Result::Missing;
    if (section->bytes != 24 || section->count != 1) return Result::Invalid;
    uint8_t raw[24];
    const auto result = package.read(SectionType::RoutingProfile, 0, raw, sizeof(raw));
    if (result != Result::Ok) return result;
    const auto flags = word(raw + 4), modes = word(raw + 20);
    const auto offset_bits = word(raw + 16);
    const int64_t offset = offset_bits <= INT32_MAX ? int64_t(offset_bits) : int64_t(offset_bits) - 4294967296LL;
    if (word(raw) != 1 || flags > 1 || modes < 1 || modes > 3 || offset <= -86400 || offset >= 86400)
        return Result::Invalid;
    const auto seconds = uint64_t(word(raw + 8)) | (uint64_t(word(raw + 12)) << 32);
    if ((!flags && (seconds || offset)) || (seconds > INT64_MAX)) return Result::Invalid;
    output.snapshot_utc_seconds = seconds;
    output.local_offset_seconds = static_cast<int32_t>(offset);
    output.travel_modes = modes;
    output.conditional_snapshot = flags != 0;
    return Result::Ok;
}
Result checkRoutingProfile(const RoutingProfile& profile, uint64_t utc_seconds, int32_t offset, uint32_t mode)
{
    if ((mode != 1 && mode != 2) || profile.travel_modes < 1 || profile.travel_modes > 3 ||
        profile.local_offset_seconds <= -86400 || profile.local_offset_seconds >= 86400 ||
        profile.snapshot_utc_seconds > INT64_MAX || utc_seconds > INT64_MAX ||
        (!profile.conditional_snapshot && (profile.snapshot_utc_seconds || profile.local_offset_seconds)))
        return Result::Invalid;
    if (!(profile.travel_modes & mode)) return Result::Incompatible;
    if (profile.conditional_snapshot &&
        (offset != profile.local_offset_seconds || utc_seconds / 60 != profile.snapshot_utc_seconds / 60))
        return Result::Incompatible;
    return Result::Ok;
}
} // namespace tmap
