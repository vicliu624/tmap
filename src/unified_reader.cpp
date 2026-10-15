#include "binary.h"
#include "tmap/reader.h"
#include <cstring>

namespace tmap
{
using namespace detail;
namespace
{
uint32_t appendCrc(uint32_t crc, const uint8_t* bytes, size_t count)
{
    while (count--)
    {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
    }
    return crc;
}
bool displaySection(uint32_t type)
{
    return type == 1 || type == 2 || type == 10 || type == 11 || (type >= 20 && type <= 24) ||
           (type >= 30 && type <= 32) || (type >= 40 && type <= 44) || (type >= 50 && type <= 54);
}
} // namespace
Status Reader::openUnified(RandomAccessFile& file)
{
    close();
    file_ = &file;
    page_reads_ = 0;
    auto& scratch = workspace_.pages[0].bytes;
    const auto fail = [this](Status status)
    { close(); return status; };
    auto status = read(0, scratch.data(), 256);
    if (status != Status::Ok) return fail(status);
    auto* h = scratch.data();
    constexpr uint8_t magic[] = {'T', 'M', 'A', 'P', '\r', '\n', 0x1a, '\n'};
    const auto count = u32(h + 48), expected = u32(h + 160), directory_crc = u32(h + 164);
    const auto meta_offset = u64(h + 144), meta_size = u64(h + 152);
    std::memset(h + 160, 0, 4);
    if (std::memcmp(h, magic, 8) || u16(h + 8) != 2 || u16(h + 10) || u32(h + 12) != 256 ||
        u32(h + 16) != 0x01020304 || u32(h + 20) != 4096 || u64(h + 24) != 8 ||
        u64(h + 32) != file.size() || u64(h + 40) != 4096 || !count || count > 128 || u32(h + 52) != 64 ||
        u32(h + 168) != 1 || u32(h + 172) != 2 || crc32c(h, 256) != expected ||
        (u32(h + 140) & ~31U) || !(u32(h + 140) & 16U) || (u32(h + 176) & ~7U)) return fail(Status::Invalid);
    std::memcpy(package_.id.data(), h + 56, 16);
    std::memcpy(package_.build.data(), h + 72, 16);
    std::memcpy(package_.series.data(), h + 88, 16);
    package_.revision = u64(h + 104);
    package_.bounds = bounds(h + 120);
    package_.zoom_mask = u32(h + 136);
    package_.capabilities = u32(h + 176);
    if (package_.bounds.west < -1800000000 || package_.bounds.east > 1800000000 ||
        package_.bounds.south < -900000000 || package_.bounds.north > 900000000 ||
        package_.bounds.west >= package_.bounds.east || package_.bounds.south >= package_.bounds.north) return fail(Status::Invalid);
    uint32_t running = UINT32_MAX;
    uint64_t previous_end = ((4096 + uint64_t(count) * 64 + 4095) / 4096) * 4096;
    for (uint32_t first = 0; first < count;)
    {
        const auto rows = (count - first < 64) ? count - first : 64;
        status = read(4096 + uint64_t(first) * 64, scratch.data(), rows * 64);
        if (status != Status::Ok) return fail(status);
        running = appendCrc(running, scratch.data(), rows * 64);
        for (uint32_t row = 0; row < rows; ++row)
        {
            const auto* d = scratch.data() + row * 64;
            const auto type = u32(d);
            const auto flags = u16(d + 6);
            const auto offset = u64(d + 16), length = u64(d + 24);
            if (u16(d + 4) != 1 || (flags & ~3U) || offset % 4096 || offset < previous_end ||
                !range(offset, length, file.size()) || ((flags & 2U) && length % 4096)) return fail(Status::Invalid);
            previous_end = offset + length;
            if (type >= 1000) continue; // Outdoor sections have a separate ID namespace.
            if (!displaySection(type))
            {
                if (flags & 1U) return fail(Status::Invalid);
                continue;
            }
            if (section_count_ == Workspace::kMaxSections) return fail(Status::Invalid);
            auto& section = workspace_.sections[section_count_];
            section = {type, u32(d + 8), u32(d + 12), offset, length, u64(d + 32), u64(d + 40), (flags & 2U) != 0};
            if (section.root && (section.root % 4096 || section.root >= length)) return fail(Status::Invalid);
            for (size_t earlier = 0; earlier < section_count_; ++earlier)
                if (workspace_.sections[earlier].id == section.id) return fail(Status::Invalid);
            ++section_count_;
        }
        first += rows;
    }
    if (~running != directory_crc || ((previous_end + 4095) / 4096) * 4096 != file.size()) return fail(Status::Invalid);
    for (const auto id : {1U, 2U, 20U, 21U, 22U, 23U, 24U, 30U, 31U, 32U, 40U, 41U, 42U})
    {
        const auto* s = section(id);
        if (!s || s->type != id) return fail(Status::Invalid);
    }
    const auto* meta = section(1);
    if (meta->offset != meta_offset || meta->length != meta_size || meta_size > 65536 || meta->paged ||
        section(2)->paged || section(2)->length % 96) return fail(Status::Invalid);
    opened_ = true;
    status = metadata();
    if (status != Status::Ok) close();
    return status;
}
} // namespace tmap
