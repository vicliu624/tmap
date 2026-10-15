#include "tmap/composite_file.h"
#include <algorithm>
namespace tmap
{
void CompositeFile::restartRead()
{
    active_ = cancelled_ = false;
    output_ = nullptr;
    request_bytes_ = done_ = 0;
}
void CompositeFile::cancelRead()
{
    restartRead();
    cancelled_ = true;
}
void CompositeFile::detach()
{
    restartRead();
    extents_ = nullptr;
    count_ = 0;
    size_ = 0;
}
Status CompositeFile::bind(const FileExtent* extents, size_t count, size_t budget)
{
    detach();
    if (!extents || !count || count > 65536 || !budget || budget > count) return Status::Invalid;
    uint64_t end = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& e = extents[i];
        if (!e.source || e.source == this || !e.bytes || e.virtual_offset != end || e.bytes > UINT64_MAX - end ||
            e.source_offset > e.source->size() || e.bytes > e.source->size() - e.source_offset) return Status::Invalid;
        end += e.bytes;
    }
    extents_ = extents;
    count_ = count;
    budget_ = budget;
    size_ = end;
    return Status::Ok;
}
Status CompositeFile::readAt(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (cancelled_) return Status::Cancelled;
    if (!extents_ || offset > size_ || bytes > size_ - offset || (!output && bytes)) return Status::Invalid;
    if (!bytes) return Status::Ok;
    if (!active_ || request_offset_ != offset || request_bytes_ != bytes || output_ != output)
    {
        request_offset_ = offset;
        request_bytes_ = bytes;
        output_ = output;
        done_ = 0;
        active_ = true;
    }
    for (size_t work = 0; work < budget_; ++work)
    {
        const uint64_t position = offset + done_;
        size_t lo = 0, hi = count_;
        while (lo + 1 < hi)
        {
            const auto middle = lo + (hi - lo) / 2;
            if (extents_[middle].virtual_offset <= position) lo = middle;
            else hi = middle;
        }
        const auto& e = extents_[lo];
        const uint64_t inside = position - e.virtual_offset;
        const auto amount = size_t(std::min<uint64_t>(bytes - done_, e.bytes - inside));
        const auto result = e.source->readAt(e.source_offset + inside, output + done_, amount);
        if (result == Status::Busy || result == Status::More) return result;
        if (result != Status::Ok)
        {
            active_ = false;
            return result;
        }
        done_ += amount;
        if (done_ == bytes)
        {
            active_ = false;
            return Status::Ok;
        }
    }
    return Status::More;
}
} // namespace tmap
