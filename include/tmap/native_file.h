#pragma once
#include "tmap/reader.h"
#include <cstdio>
namespace tmap
{
class NativeFile final : public RandomAccessFile
{
  public:
    NativeFile() = default;
    explicit NativeFile(const char* path) { open(path); }
    ~NativeFile() override { close(); }
    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;
    bool open(const char* path);
    void close();
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override;
    uint64_t size() const override { return size_; }

  private:
    std::FILE* file_ = nullptr;
    uint64_t size_ = 0;
};
} // namespace tmap
