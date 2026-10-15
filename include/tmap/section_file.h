#pragma once
#include "tmap/package.h"
namespace tmap
{
// Logical section view for streaming algorithms. Uses the package's CRC-checked
// cache and advances at most one payload page per readAt attempt. No owned cache.
// Borrowed package, view and output must keep their addresses while a read waits.
class PackageSectionFile : public RandomAccessFile
{
  public:
    PackageSectionFile(OutdoorPackage& package, SectionType type);
    PackageSectionFile(const PackageSectionFile&) = delete;
    PackageSectionFile& operator=(const PackageSectionFile&) = delete;
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override;
    uint64_t size() const override;
    static uint64_t epoch(void* context);
    // Only after the sole consumer abandons its pending read. Keeps map identity.
    void discardPending();

  private:
    OutdoorPackage& package_;
    SectionType type_;
    uint64_t generation_ = 0, bytes_ = 0;
    ReadCursor read_{};
    bool pending_ = false;
};
} // namespace tmap
