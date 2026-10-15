#include "tmap/staged_memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    alignas(16) unsigned char raw[128];
    bool external = true, fail = false, misaligned = false;
    size_t internal = 0, outside = 0, released = 0;
    bool hasExternalMemory() const override { return external; }
    void* allocate(size_t bytes, size_t, MemoryRegion region) override
    {
        (region == MemoryRegion::External ? outside : internal)++;
        std::memset(raw, 0xA5, sizeof(raw));
        return fail || bytes > sizeof(raw) ? nullptr : raw + (misaligned ? 1 : 0);
    }
    void release(void*, size_t, size_t, MemoryRegion) override { ++released; }
};
int main()
{
    Provider provider;
    StagedMemoryBuffer<uint32_t> buffer(provider);
    CHECK(buffer.initializeStep(1) == Result::Invalid);
    CHECK(buffer.beginReserve(8) == Result::More && buffer.bytes() == 32 && !buffer.data());
    CHECK(buffer.initializeStep(0) == Result::More && buffer.initialized() == 0);
    CHECK(buffer.initializeStep(3) == Result::More && buffer.initialized() == 3 && !buffer.data());
    for (size_t i = 0; i < 12; ++i) CHECK(provider.raw[i] == 0);
    for (size_t i = 12; i < 32; ++i) CHECK(provider.raw[i] == 0xA5);
    CHECK(buffer.beginReserve(2) == Result::Invalid && buffer.initialized() == 3);
    buffer.cancel();
    CHECK(buffer.initializeStep(100) == Result::Cancelled && !buffer.bytes() && provider.released == 1);
    CHECK(buffer.beginReserve(8) == Result::More);
    CHECK(buffer.initializeStep(100) == Result::Ok && buffer.data()[7] == 0);
    CHECK(buffer.initializeStep(0) == Result::Ok);
    buffer.reset();
    buffer.reset();
    CHECK(provider.released == 2);
    provider.fail = true;
    CHECK(buffer.beginReserve(8) == Result::BudgetExceeded && provider.internal == 0);
    provider.fail = false;
    provider.misaligned = true;
    CHECK(buffer.beginReserve(8) == Result::Invalid && provider.released == 3);
    provider.misaligned = false;
    CHECK(buffer.beginReserve(SIZE_MAX) == Result::Invalid && !buffer.bytes());
    provider.external = false;
    StagedMemoryBuffer<uint32_t> required(provider, MemoryPolicy::RequireExternal);
    CHECK(required.beginReserve(8) == Result::Missing && provider.internal == 0);
    CHECK(buffer.beginReserve(2) == Result::More && buffer.region() == MemoryRegion::Internal);
    std::printf("PASS bounded untouched suffix, hidden partial array, cancellation/restart, ownership, overflow, external priority\n");
}
