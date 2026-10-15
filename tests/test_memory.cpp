#include "tmap/contour_renderer.h"
#include "tmap/coordinate_route.h"
#include "tmap/memory.h"
#include <cstdio>
#include <cstdlib>
using namespace tmap;
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
class Provider : public MemoryProvider
{
  public:
    alignas(16) uint8_t storage[1024]{};
    bool external = true, fail = false, misaligned = false;
    size_t internal_calls = 0, external_calls = 0, releases = 0, last_bytes = 0;
    MemoryRegion last_region = MemoryRegion::Internal;
    bool hasExternalMemory() const override { return external; }
    void* allocate(size_t bytes, size_t, MemoryRegion region) override
    {
        (region == MemoryRegion::External ? external_calls : internal_calls)++;
        last_bytes = bytes;
        last_region = region;
        return fail || bytes > sizeof(storage) ? nullptr : storage + (misaligned ? 1 : 0);
    }
    void release(void*, size_t bytes, size_t, MemoryRegion region) override
    {
        CHECK(bytes == last_bytes && region == last_region);
        ++releases;
    }
};
int main()
{
    Provider provider;
    {
        MemoryBuffer<uint32_t> buffer(provider);
        CHECK(buffer.reserve(8) == Result::Ok && provider.external_calls == 1 && provider.internal_calls == 0);
        CHECK(buffer.region() == MemoryRegion::External && buffer.bytes() == 32 && buffer.data()[7] == 0);
        CHECK(buffer.reserve(1) == Result::Invalid);
    }
    CHECK(provider.releases == 1);
    provider.fail = true;
    MemoryBuffer<uint32_t> buffer(provider);
    CHECK(buffer.reserve(8) == Result::BudgetExceeded && provider.internal_calls == 0);
    provider.external = false;
    CHECK(buffer.reserve(8) == Result::BudgetExceeded && provider.internal_calls == 1);
    provider.fail = false;
    CHECK(buffer.reserve(8) == Result::Ok && buffer.region() == MemoryRegion::Internal);
    buffer.reset();
    MemoryBuffer<uint32_t> required(provider, MemoryPolicy::RequireExternal);
    const auto calls = provider.internal_calls;
    CHECK(required.reserve(8) == Result::Missing && provider.internal_calls == calls);
    provider.external = true;
    provider.misaligned = true;
    CHECK(buffer.reserve(8) == Result::Invalid && !buffer.data());
    const auto total = provider.internal_calls + provider.external_calls;
    CHECK(buffer.reserve(0) == Result::Invalid);
    CHECK(buffer.reserve(SIZE_MAX) == Result::Invalid);
    CHECK(provider.internal_calls + provider.external_calls == total);
    std::printf("PASS external-first, no silent fallback, no-external target, failure, alignment, overflow, ownership; provider_calls=%zu\n", total);
    std::printf("SIZES reader=%zu package=%zu terrain=%zu graph=%zu profile_cursor=%zu buffer_wrapper=%zu navigator=%zu\n",
                sizeof(Reader), sizeof(OutdoorPackage), sizeof(TerrainReader), sizeof(GraphReader), sizeof(ProfileCursor), sizeof(MemoryBuffer<PackageWorkspace>), sizeof(Navigator));
}
