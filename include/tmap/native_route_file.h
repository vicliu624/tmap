#pragma once
#include "tmap/route_archive.h"
#include <cstdio>
namespace tmap
{
// Optional desktop adapter, excluded when TMAP_NATIVE_FILE=OFF.
// flush() flushes stdio, not a promise of physical-media durability.
class NativeRouteFile final : public WritableRouteFile
{
  public:
    ~NativeRouteFile() override { close(); }
    NativeRouteFile() = default;
    NativeRouteFile(const NativeRouteFile&) = delete;
    NativeRouteFile& operator=(const NativeRouteFile&) = delete;
    bool openNew(const char* path); // Exclusive create; never overwrites a route.
    bool flush();
    void close();
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override;
    Status writeAt(uint64_t offset, const uint8_t* bytes, size_t count) override;
    uint64_t size() const override { return size_; }

  private:
    std::FILE* file_ = nullptr;
    uint64_t size_ = 0;
};
} // namespace tmap
