#pragma once
#include "tmap/sha256.h"
namespace tmap
{
struct FileIdentity
{
    uint64_t bytes = 0;
    std::array<uint8_t, 32> sha256{};
};
using FileEpoch = uint64_t (*)(void*);
struct FileIdentityCursor
{
    Sha256 hash{};
    std::array<uint8_t, 64> scratch{};
    std::array<uint8_t, 32> expected{}, digest{};
    RandomAccessFile* file = nullptr;
    FileEpoch epoch = nullptr;
    void* epoch_context = nullptr;
    uint64_t bytes = 0, offset = 0, initial_epoch = 0;
    Result terminal = Result::Invalid;
};
// Optional epoch callback detects same-size media replacement. Otherwise the
// application must hold an immutable source snapshot throughout its use.
Result beginFileIdentity(RandomAccessFile& file, const FileIdentity& expected, FileIdentityCursor& cursor,
                         uint64_t max_bytes = UINT64_MAX / 8, FileEpoch epoch = nullptr, void* epoch_context = nullptr);
Result fileIdentityStep(FileIdentityCursor& cursor, size_t read_budget);
void cancel(FileIdentityCursor& cursor);
static_assert(sizeof(FileIdentityCursor) <= 384, "Bounded file verification state");
} // namespace tmap
