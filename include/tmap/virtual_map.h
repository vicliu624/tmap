#pragma once
#include "tmap/composite_file.h"
#include "tmap/file_identity.h"
namespace tmap
{
struct VirtualSource
{
    RandomAccessFile* file = nullptr;
    FileEpoch epoch = nullptr;
    void* context = nullptr;
    uint64_t verified_bytes = 0, verified_epoch = 0;
};
struct VirtualMapCursor;
class VerifiedVirtualFile : public RandomAccessFile
{
  public:
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override;
    uint64_t size() const override;
    void detach();

  private:
    friend Result virtualMapStep(VirtualMapCursor&, size_t);
    CompositeFile view_;
    VirtualSource* sources_ = nullptr;
    size_t count_ = 0;
    bool ready_ = false;
};
struct VirtualMapCursor
{
    FileIdentityCursor verification{};
    std::array<uint8_t, 64> scratch{};
    std::array<uint8_t, 32> virtual_digest{};
    RandomAccessFile* manifest = nullptr;
    VirtualSource* sources = nullptr;
    FileExtent* extents = nullptr;
    VerifiedVirtualFile* output = nullptr;
    FileEpoch manifest_epoch = nullptr;
    void* manifest_context = nullptr;
    uint64_t initial_epoch = 0, manifest_bytes = 0, virtual_bytes = 0, remaining_source_bytes = 0, end = 0;
    size_t available_sources = 0, capacity = 0, source_count = 0, extent_count = 0, index = 0;
    uint8_t stage = 0;
    Result terminal = Result::Invalid;
};
Result beginVirtualMap(RandomAccessFile& manifest, const FileIdentity& trusted_identity,
                       VirtualSource* sources, size_t source_count, FileExtent* extents, size_t capacity,
                       VerifiedVirtualFile& output, VirtualMapCursor& cursor, uint64_t max_source_bytes,
                       FileEpoch manifest_epoch = nullptr, void* manifest_context = nullptr);
Result virtualMapStep(VirtualMapCursor& cursor, size_t work_budget);
void cancel(VirtualMapCursor& cursor);
} // namespace tmap
