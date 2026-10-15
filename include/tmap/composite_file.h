#pragma once
#include "tmap/reader.h"
namespace tmap
{
struct FileExtent
{
    RandomAccessFile* source = nullptr;
    uint64_t virtual_offset = 0, source_offset = 0, bytes = 0;
};
// Borrowed, contiguous virtual extents. No file handles, buffers or allocation
// are owned here. One serialized exact-read continuation is retained.
class CompositeFile : public RandomAccessFile
{
  public:
    Status bind(const FileExtent* extents, size_t count, size_t extents_per_read = 1);
    void detach();
    void cancelRead();
    void restartRead();
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override;
    uint64_t size() const override { return size_; }

  private:
    const FileExtent* extents_ = nullptr;
    size_t count_ = 0, budget_ = 1;
    uint64_t size_ = 0, request_offset_ = 0;
    uint8_t* output_ = nullptr;
    size_t request_bytes_ = 0, done_ = 0;
    bool active_ = false, cancelled_ = false;
};
} // namespace tmap
