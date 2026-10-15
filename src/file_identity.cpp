#include "tmap/file_identity.h"
#include <algorithm>
namespace tmap
{
void cancel(FileIdentityCursor& c) { c.terminal = Result::Cancelled; }
Result beginFileIdentity(RandomAccessFile& file, const FileIdentity& expected, FileIdentityCursor& c,
                         uint64_t max_bytes, FileEpoch epoch, void* context)
{
    c.terminal = Result::Invalid;
    c.file = nullptr;
    c.offset = 0;
    c.digest.fill(0);
    if (expected.bytes > max_bytes || expected.bytes > UINT64_MAX / 8) return c.terminal = Result::BudgetExceeded;
    if (file.size() != expected.bytes) return c.terminal = Result::Incompatible;
    c.file = &file;
    c.bytes = expected.bytes;
    c.expected = expected.sha256;
    c.epoch = epoch;
    c.epoch_context = context;
    c.initial_epoch = epoch ? epoch(context) : 0;
    c.hash.reset();
    return c.terminal = Result::More;
}
Result fileIdentityStep(FileIdentityCursor& c, size_t budget)
{
    if (!budget) return Result::Invalid;
    if (c.terminal != Result::More && c.terminal != Result::Ok) return c.terminal;
    if (!c.file || c.file->size() != c.bytes || (c.epoch && c.epoch(c.epoch_context) != c.initial_epoch))
        return c.terminal = Result::Cancelled;
    if (c.terminal == Result::Ok) return c.terminal;
    for (size_t work = 0; work < budget && c.offset < c.bytes; ++work)
    {
        const auto amount = size_t(std::min<uint64_t>(c.scratch.size(), c.bytes - c.offset));
        const auto status = c.file->readAt(c.offset, c.scratch.data(), amount);
        if (status == Status::Busy || status == Status::More) return convert(status);
        if (status != Status::Ok) return c.terminal = convert(status);
        const auto result = c.hash.update(c.scratch.data(), amount);
        if (result != Result::Ok) return c.terminal = result;
        c.offset += amount;
    }
    if (c.offset != c.bytes) return Result::More;
    c.hash.finish(c.digest);
    // Recheck after the final read, including the adapter's media epoch.
    if (c.file->size() != c.bytes || (c.epoch && c.epoch(c.epoch_context) != c.initial_epoch))
        return c.terminal = Result::Cancelled;
    return c.terminal = c.digest == c.expected ? Result::Ok : Result::Incompatible;
}
} // namespace tmap
