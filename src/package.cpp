#include "tmap/package.h"
#include "binary.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace tmap
{
using namespace detail;
void detail::packageStorageAbi(PackageStorageAbi<PackageWorkspace::kPageBytes, PackageWorkspace::kCachePages, PackageWorkspace::kSections>*) {}
namespace
{
uint32_t appendDirectoryCrc(uint32_t crc, const uint8_t* data, size_t size)
{
    static constexpr uint32_t table[] = {0, 0x105ec76f, 0x20bd8ede, 0x30e349b1, 0x417b1dbc, 0x5125dad3, 0x61c69362, 0x7198540d,
                                         0x82f63b78, 0x92a8fc17, 0xa24bb5a6, 0xb21572c9, 0xc38d26c4, 0xd3d3e1ab, 0xe330a81a, 0xf36e6f75};
    while (size--)
    {
        crc ^= *data++;
        crc = (crc >> 4) ^ table[crc & 15];
        crc = (crc >> 4) ^ table[crc & 15];
    }
    return crc;
}
} // namespace
Result convert(Status s)
{
    switch (s)
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
const char* resultName(Result r)
{
    constexpr const char* names[] = {"ok", "missing", "busy", "more", "invalid", "io_error", "cancelled", "unsupported", "no_path", "budget_exceeded", "incomplete", "outside_coverage", "incompatible"};
    const auto n = static_cast<size_t>(r);
    return n < sizeof(names) / sizeof(names[0]) ? names[n] : "unknown";
}
void OutdoorPackage::close()
{
    file_ = nullptr;
    opening_file_ = nullptr;
    header_loaded_ = false;
    page_bytes_ = opening_next_ = opening_chunk_rows_ = opening_chunk_next_ = 0;
    opening_running_crc_ = UINT32_MAX;
    sections_ = 0;
    info_ = {};
    ++generation_;
    for (auto& p : workspace_.pages) p.type = 0;
    clock_ = last_payload_type_ = 0;
    dual_payload_ = false;
}
const OutdoorSection* OutdoorPackage::section(SectionType type) const
{
    if (!isOpen()) return nullptr;
    for (size_t i = 0; i < sections_; ++i)
        if (workspace_.sections[i].type == type) return &workspace_.sections[i];
    return nullptr;
}
const OutdoorSection* OutdoorPackage::sectionAt(size_t index) const
{
    return isOpen() && index < sections_ ? &workspace_.sections[index] : nullptr;
}
Result OutdoorPackage::open(RandomAccessFile& file, size_t section_budget)
{
    if (!section_budget) return Result::Invalid;
    if (opening_file_ != &file)
    {
        close();
        opening_file_ = &file;
        opening_size_ = file.size();
    }
    const auto fail = [this](Result r)
    { if (r != Result::Busy && r != Result::More) close(); return r; };
    if (file.size() != opening_size_) return fail(Result::Invalid);
    auto& scratch = workspace_.pages[0].bytes;
    Result r;
    if (!header_loaded_)
    {
        r = convert(file.readAt(0, scratch.data(), 256));
        if (r != Result::Ok) return fail(r);
        auto* h = scratch.data();
        constexpr uint8_t magic[] = {'T', 'M', 'A', 'P', '\r', '\n', 0x1a, '\n'};
        if (std::memcmp(h, magic, 8)) return fail(Result::Invalid);
        if (u16(h + 8) != 2 || u16(h + 10) != 0) return fail(Result::Unsupported);
        const auto expected = u32(h + 160);
        std::memset(h + 160, 0, 4);
        page_bytes_ = u32(h + 20);
        if (Reader::crc32c(h, 256) != expected || u32(h + 12) != 256 || u32(h + 16) != 0x01020304 ||
            (page_bytes_ != 1024 && page_bytes_ != 2048 && page_bytes_ != 4096) ||
            u64(h + 24) || u64(h + 32) != file.size() || u64(h + 40) != page_bytes_ || u32(h + 52) != 64) return fail(Result::Invalid);
        const auto count = u32(h + 48), directory_crc = u32(h + 164), required = u32(h + 140);
        if (!count || count > 64) return fail(Result::Invalid);
        if (count > PackageWorkspace::kSections || page_bytes_ > PackageWorkspace::kPageBytes) return fail(Result::BudgetExceeded);
        if (required & ~15U) return fail(Result::Unsupported);
        auto& pending = info_;
        std::memcpy(pending.id.data(), h + 56, 16);
        std::memcpy(pending.build.data(), h + 72, 16);
        std::memcpy(pending.series.data(), h + 88, 16);
        pending.revision = u64(h + 104);
        pending.bounds = bounds(h + 120);
        pending.capabilities = required;
        if (!coordinate(pending.bounds.south, pending.bounds.west) || !coordinate(pending.bounds.north, pending.bounds.east) ||
            pending.bounds.west >= pending.bounds.east || pending.bounds.south >= pending.bounds.north) return fail(Result::Invalid);
        opening_count_ = count;
        opening_crc_ = directory_crc;
        opening_previous_end_ = ((uint64_t(page_bytes_) + uint64_t(count) * 64 + page_bytes_ - 1) / page_bytes_) * page_bytes_;
        header_loaded_ = true;
    }
    const auto count = opening_count_;
    const auto required = info_.capabilities;
    for (size_t work = 0; work < section_budget && opening_next_ < count; ++work)
    {
        if (!opening_chunk_rows_)
        {
            const auto rows = std::min<uint32_t>(count - opening_next_, uint32_t(scratch.size() / 64));
            r = convert(file.readAt(uint64_t(page_bytes_) + uint64_t(opening_next_) * 64, scratch.data(), rows * 64));
            if (r != Result::Ok) return fail(r);
            if (!opening_next_ && rows == count) opening_running_crc_ = ~Reader::crc32c(scratch.data(), rows * 64);
            else opening_running_crc_ = appendDirectoryCrc(opening_running_crc_, scratch.data(), rows * 64);
            opening_chunk_rows_ = rows;
            opening_chunk_next_ = 0;
        }
        const auto i = opening_next_;
        const auto* d = scratch.data() + opening_chunk_next_ * 64;
        auto& s = workspace_.sections[i];
        s = {static_cast<SectionType>(u32(d)), u64(d + 16), u64(d + 24), u64(d + 48), u64(d + 32)};
        const auto flags = u16(d + 6);
        if (!u32(d) || u16(d + 4) != 1 || flags != 2 || u32(d + 8) != u32(d) || u32(d + 12) || u64(d + 40) || u64(d + 56) ||
            s.offset % page_bytes_ || s.offset < opening_previous_end_ || s.length % page_bytes_ || !range(s.offset, s.length, file.size()) ||
            s.bytes > std::numeric_limits<uint64_t>::max() - (payloadBytes() - 1) ||
            (s.bytes + payloadBytes() - 1) / payloadBytes() != s.length / page_bytes_) return fail(Result::Invalid);
        for (uint32_t j = 0; j < i; ++j)
            if (workspace_.sections[j].type == s.type) return fail(Result::Invalid);
        opening_previous_end_ = s.offset + s.length;
        ++opening_next_;
        if (++opening_chunk_next_ == opening_chunk_rows_) opening_chunk_rows_ = 0;
    }
    if (opening_next_ != count) return Result::More;
    if (opening_previous_end_ != file.size() || ~opening_running_crc_ != opening_crc_) return fail(Result::Invalid);
    sections_ = count;
    file_ = &file;
    opening_file_ = nullptr;
    header_loaded_ = false;
    page_reads_ = 0;
    for (auto& p : workspace_.pages) p.type = 0;
    const auto has = [this](SectionType type)
    { return section(type) != nullptr; };
    if (((required & Elevation) && (!has(SectionType::Terrain) || !has(SectionType::DemIndex) || !has(SectionType::DemData))) ||
        ((required & Contours) && (!has(SectionType::ContourIndex) || !has(SectionType::ContourData))) ||
        ((required & Routing) && (!has(SectionType::Graph) || !has(SectionType::Nodes) || !has(SectionType::Edges) ||
                                  !has(SectionType::Shapes) || !has(SectionType::Restrictions) || !has(SectionType::EdgeSpatial) || !has(SectionType::EdgeRefs))) ||
        ((required & Administration) && (!has(SectionType::AdministrativeIndex) || !has(SectionType::AdministrativeRecords) || !has(SectionType::AdministrativeGeometry) || !has(SectionType::AdministrativePaths))))
    {
        close();
        return Result::Invalid;
    }
    return Result::Ok;
}
Result OutdoorPackage::page(const OutdoorSection& section, uint64_t index, const uint8_t*& payload, bool dual)
{
    payload = nullptr;
    if (!file_ || index >= section.length / page_bytes_) return Result::Invalid;
    const uint32_t type = static_cast<uint32_t>(section.type);
    const bool table = section.type == SectionType::DemIndex || section.type == SectionType::ContourIndex || section.type == SectionType::ContourOverviewIndex ||
                       section.type == SectionType::Nodes || section.type == SectionType::Restrictions ||
                       section.type == SectionType::EdgeSpatial || section.type == SectionType::EdgeRefs || section.type == SectionType::EdgeTerrainIndex ||
                       section.type == SectionType::LandmarkPolicy || section.type == SectionType::LandmarkDistances ||
                       section.type == SectionType::RouteHistoryStates || section.type == SectionType::RouteHistoryTransitions ||
                       section.type == SectionType::AdministrativeIndex || section.type == SectionType::AdministrativeRecords;
    if (table) dual_payload_ = false;
    else
    {
        if (last_payload_type_ == type && index < last_payload_page_ && last_payload_page_ - index == 1) dual_payload_ = true;
        last_payload_type_ = type;
        last_payload_page_ = index;
    }
    if (++clock_ == 0)
    {
        clock_ = 1;
        for (auto& p : workspace_.pages) p.age = 0;
    }
    for (auto& p : workspace_.pages)
        if (p.type == type && p.index == index)
        {
            p.age = clock_;
            payload = p.bytes.data() + 32;
            return Result::Ok;
        }
    // Preserve one table and one streaming payload without growing the cache.
    // Cross-page records and backward retries temporarily use both pages as LRU.
    auto* target = &workspace_.pages[table ? 0 : 1];
    if (dual || dual_payload_)
        for (auto& slot : workspace_.pages)
        {
            if (!slot.type)
            {
                target = &slot;
                break;
            }
            if (slot.age < target->age) target = &slot;
        }
    auto& p = *target;
    p.type = 0;
    auto r = convert(file_->readAt(section.offset + index * page_bytes_, p.bytes.data(), page_bytes_));
    if (r != Result::Ok) return r;
    ++page_reads_;
    auto* h = p.bytes.data();
    const auto crc = u32(h + 16);
    std::memset(h + 16, 0, 4);
    if (Reader::crc32c(h, page_bytes_) != crc || std::memcmp(h, "TPG2", 4) || u32(h + 4) != static_cast<uint32_t>(section.type) ||
        u32(h + 8) != index || u32(h + 12) != std::min<uint64_t>(payloadBytes(), section.bytes - index * payloadBytes()) ||
        u32(h + 20) || u64(h + 24)) return Result::Invalid;
    p.type = type;
    p.age = clock_;
    p.index = index;
    payload = h + 32;
    return Result::Ok;
}
Result OutdoorPackage::read(SectionType type, uint64_t offset, uint8_t* output, size_t bytes)
{
    const auto* s = section(type);
    if (!s) return isOpen() ? Result::Missing : Result::Invalid;
    if ((!output && bytes) || !range(offset, bytes, s->bytes)) return Result::Invalid;
    const auto payload_bytes = payloadBytes();
    const bool dual = bytes && (offset / payload_bytes != (offset + bytes - 1) / payload_bytes);
    while (bytes)
    {
        const uint8_t* p = nullptr;
        auto r = page(*s, offset / payload_bytes, p, dual);
        if (r != Result::Ok) return r;
        const size_t within = static_cast<size_t>(offset % payload_bytes), n = std::min<size_t>(bytes, payload_bytes - within);
        std::memcpy(output, p + within, n);
        output += n;
        offset += n;
        bytes -= n;
    }
    return Result::Ok;
}
Result OutdoorPackage::record(SectionType type, uint64_t row, size_t stride, uint8_t* output)
{
    const auto* s = section(type);
    if (!s) return isOpen() ? Result::Missing : Result::Invalid;
    if (!stride || row >= s->count || s->count > s->bytes / stride || s->bytes % stride || s->count != s->bytes / stride) return Result::Invalid;
    return read(type, row * stride, output, stride);
}
Result beginValidation(OutdoorPackage& package, ValidationCursor& c)
{
    reset(c);
    if (!package.isOpen()) return Result::Invalid;
    c.generation = package.generation();
    c.complete = false;
    return Result::Ok;
}
Result beginRead(OutdoorPackage& package, SectionType type, uint64_t offset, uint8_t* output, size_t bytes, ReadCursor& c)
{
    reset(c);
    const auto* section = package.section(type);
    if (!section) return package.isOpen() ? Result::Missing : Result::Invalid;
    if ((!output && bytes) || !range(offset, bytes, section->bytes)) return Result::Invalid;
    c.generation = package.generation();
    c.offset = offset;
    c.output = output;
    c.bytes = bytes;
    c.type = type;
    c.complete = !bytes;
    c.terminal = bytes ? Result::More : Result::Ok;
    return Result::Ok;
}
void cancelRead(ReadCursor& c)
{
    c.complete = true;
    c.terminal = Result::Cancelled;
}
Result readStep(OutdoorPackage& package, ReadCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (c.complete) return c.terminal;
    if (!package.isOpen() || c.generation != package.generation())
    {
        cancelRead(c);
        return c.terminal;
    }
    for (size_t work = 0; work < budget && c.completed < c.bytes; ++work)
    {
        const auto at = c.offset + c.completed;
        const size_t count = std::min<size_t>(c.bytes - c.completed, package.payloadBytes() - at % package.payloadBytes());
        const auto r = package.read(c.type, at, c.output + c.completed, count);
        if (r == Result::Busy || r == Result::More) return r;
        if (r != Result::Ok)
        {
            c.complete = true;
            c.terminal = r;
            return r;
        }
        c.completed += count;
    }
    if (c.completed == c.bytes)
    {
        c.complete = true;
        c.terminal = Result::Ok;
    }
    return c.terminal;
}
void cancelValidation(ValidationCursor& c)
{
    c.cancelled = c.complete = true;
}
Result validationStep(OutdoorPackage& package, ValidationCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (c.cancelled) return Result::Cancelled;
    if (!package.isOpen() || c.generation != package.generation()) return Result::Cancelled;
    if (c.complete) return Result::Ok;
    for (size_t work = 0; work < budget; ++work)
    {
        while (const auto* section = package.sectionAt(c.section))
        {
            if (c.offset < section->bytes) break;
            ++c.section;
            c.offset = 0;
        }
        const auto* section = package.sectionAt(c.section);
        if (!section)
        {
            c.complete = true;
            return Result::Ok;
        }
        uint8_t byte = 0;
        const auto r = package.read(section->type, c.offset, &byte, 1);
        if (r != Result::Ok) return r;
        c.offset += package.payloadBytes();
    }
    return Result::More;
}
} // namespace tmap
