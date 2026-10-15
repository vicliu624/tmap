#include "tmap/virtual_map.h"
#include "binary.h"
#include <cstring>
namespace tmap
{
namespace
{
bool valid(const VirtualSource& s)
{
    return s.file && s.file->size() == s.verified_bytes && (!s.epoch || s.epoch(s.context) == s.verified_epoch);
}
Result stop(VirtualMapCursor& c, Result result)
{
    if (c.output) c.output->detach();
    c.terminal = result;
    return result;
}
} // namespace
void VerifiedVirtualFile::detach()
{
    view_.detach();
    ready_ = false;
    sources_ = nullptr;
    count_ = 0;
}
uint64_t VerifiedVirtualFile::size() const
{
    if (!ready_) return 0;
    for (size_t i = 0; i < count_; ++i)
        if (!valid(sources_[i])) return 0;
    return view_.size();
}
Status VerifiedVirtualFile::readAt(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (!ready_) return Status::Invalid;
    for (size_t i = 0; i < count_; ++i)
        if (!valid(sources_[i])) return Status::Cancelled;
    const auto result = view_.readAt(offset, output, bytes);
    for (size_t i = 0; i < count_; ++i)
        if (!valid(sources_[i])) return Status::Cancelled;
    return result;
}
void cancel(VirtualMapCursor& c)
{
    cancel(c.verification);
    stop(c, Result::Cancelled);
}
Result beginVirtualMap(RandomAccessFile& manifest, const FileIdentity& identity, VirtualSource* sources, size_t count,
                       FileExtent* extents, size_t capacity, VerifiedVirtualFile& output, VirtualMapCursor& c,
                       uint64_t max_bytes, FileEpoch epoch, void* context)
{
    output.detach();
    c.output = &output;
    c.terminal = Result::Invalid;
    if (!sources || !count || count > 256 || !extents || !capacity || capacity > 65536) return c.terminal;
    c.manifest = &manifest;
    c.sources = sources;
    c.available_sources = count;
    c.extents = extents;
    c.capacity = capacity;
    c.remaining_source_bytes = max_bytes;
    c.manifest_epoch = epoch;
    c.manifest_context = context;
    c.initial_epoch = epoch ? epoch(context) : 0;
    c.manifest_bytes = identity.bytes;
    c.stage = 0;
    c.index = c.source_count = c.extent_count = 0;
    c.end = 0;
    const auto result = beginFileIdentity(manifest, identity, c.verification, 64 + 256 * 40 + 65536 * 32, epoch, context);
    return c.terminal = result;
}
Result virtualMapStep(VirtualMapCursor& c, size_t budget)
{
    using namespace detail;
    if (!budget) return Result::Invalid;
    if (c.terminal != Result::More && c.terminal != Result::Ok) return c.terminal;
    if (c.manifest->size() != c.manifest_bytes || (c.manifest_epoch && c.manifest_epoch(c.manifest_context) != c.initial_epoch))
        return stop(c, Result::Cancelled);
    if (c.terminal == Result::Ok) return c.output->size() == c.virtual_bytes ? Result::Ok : stop(c, Result::Cancelled);
    for (size_t work = 0; work < budget; ++work)
    {
        if (c.stage == 0 || c.stage == 3 || c.stage == 6)
        {
            const auto result = fileIdentityStep(c.verification, 1);
            if (result == Result::Busy || result == Result::More) return result;
            if (result != Result::Ok) return stop(c, result);
            if (c.stage == 6)
            {
                for (size_t i = 0; i < c.source_count; ++i)
                    if (!valid(c.sources[i])) return stop(c, Result::Cancelled);
                c.output->ready_ = true;
                return c.terminal = Result::Ok;
            }
            c.stage = c.stage == 0 ? 1 : 2;
            if (c.stage == 2) ++c.index;
            continue;
        }
        if (c.stage == 1)
        {
            const auto status = c.manifest->readAt(0, c.scratch.data(), 64);
            if (status == Status::Busy || status == Status::More) return convert(status);
            if (status != Status::Ok) return stop(c, convert(status));
            const auto* p = c.scratch.data();
            if (std::memcmp(p, "TMV1\r\n\x1a\n", 8) || u16(p + 8) != 1 || u16(p + 10) != 64 || u32(p + 20)) return stop(c, Result::Invalid);
            c.source_count = u32(p + 12);
            c.extent_count = u32(p + 16);
            c.virtual_bytes = u64(p + 24);
            if (c.source_count != c.available_sources) return stop(c, Result::Incompatible);
            if (!c.extent_count || c.extent_count > c.capacity) return stop(c, Result::BudgetExceeded);
            if (c.manifest_bytes != 64 + uint64_t(c.source_count) * 40 + uint64_t(c.extent_count) * 32) return stop(c, Result::Invalid);
            std::memcpy(c.virtual_digest.data(), p + 32, 32);
            c.stage = 2;
            c.index = 0;
            continue;
        }
        if (c.stage == 2)
        {
            if (c.index == c.source_count)
            {
                c.stage = 4;
                c.index = 0;
                continue;
            }
            const auto status = c.manifest->readAt(64 + c.index * 40, c.scratch.data(), 40);
            if (status == Status::Busy || status == Status::More) return convert(status);
            if (status != Status::Ok) return stop(c, convert(status));
            FileIdentity identity;
            identity.bytes = u64(c.scratch.data());
            std::memcpy(identity.sha256.data(), c.scratch.data() + 8, 32);
            auto& source = c.sources[c.index];
            if (!source.file || source.file == c.output) return stop(c, Result::Invalid);
            if (identity.bytes > c.remaining_source_bytes) return stop(c, Result::BudgetExceeded);
            const auto result = beginFileIdentity(*source.file, identity, c.verification, c.remaining_source_bytes, source.epoch, source.context);
            if (result != Result::More) return stop(c, result);
            c.remaining_source_bytes -= identity.bytes;
            source.verified_bytes = identity.bytes;
            source.verified_epoch = c.verification.initial_epoch;
            c.stage = 3;
            continue;
        }
        if (c.stage == 4)
        {
            if (c.index == c.extent_count)
            {
                c.stage = 5;
                continue;
            }
            const auto status = c.manifest->readAt(64 + c.source_count * 40 + c.index * 32, c.scratch.data(), 32);
            if (status == Status::Busy || status == Status::More) return convert(status);
            if (status != Status::Ok) return stop(c, convert(status));
            const auto* p = c.scratch.data();
            const auto source = u32(p);
            if (source >= c.source_count || u32(p + 4)) return stop(c, Result::Invalid);
            auto& e = c.extents[c.index];
            e = {c.sources[source].file, u64(p + 8), u64(p + 16), u64(p + 24)};
            if (!e.bytes || e.virtual_offset != c.end || e.bytes > UINT64_MAX - c.end ||
                !range(e.source_offset, e.bytes, c.sources[source].verified_bytes)) return stop(c, Result::Invalid);
            c.end += e.bytes;
            ++c.index;
            continue;
        }
        if (c.stage == 5)
        {
            if (c.end != c.virtual_bytes) return stop(c, Result::Invalid);
            for (size_t i = 0; i < c.source_count; ++i)
                if (!valid(c.sources[i])) return stop(c, Result::Cancelled);
            const auto status = c.output->view_.bind(c.extents, c.extent_count);
            if (status != Status::Ok) return stop(c, convert(status));
            c.output->sources_ = c.sources;
            c.output->count_ = c.source_count;
            FileIdentity identity;
            identity.bytes = c.virtual_bytes;
            identity.sha256 = c.virtual_digest;
            const auto result = beginFileIdentity(c.output->view_, identity, c.verification);
            if (result != Result::More) return stop(c, result);
            c.stage = 6;
        }
    }
    return Result::More;
}
} // namespace tmap
