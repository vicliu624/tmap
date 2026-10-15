#pragma once
#include "tmap/package.h"
namespace tmap
{
// Streaming SHA-256 for source identity, per FIPS 180-4. No allocation or global
// mutable state. Caller chooses update chunk size to bound work per invocation.
class Sha256
{
  public:
    Sha256() { reset(); }
    void reset();
    Result update(const uint8_t* bytes, size_t count);
    Result finish(std::array<uint8_t, 32>& digest);

  private:
    void block();
    std::array<uint32_t, 8> state_{};
    std::array<uint32_t, 16> schedule_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t bytes_ = 0;
    uint8_t used_ = 0;
    bool finished_ = false;
};
static_assert(sizeof(Sha256) <= 192, "Bounded streaming identity workspace");
} // namespace tmap
