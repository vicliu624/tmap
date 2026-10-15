#include "tmap/memory_object.h"
#include "tmap/navigation.h"
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
    alignas(16) unsigned char storage[128]{};
    bool external = true, fail = false, misaligned = false;
    size_t internal = 0, outside = 0, releases = 0;
    bool hasExternalMemory() const override { return external; }
    void* allocate(size_t bytes, size_t, MemoryRegion region) override
    {
        (region == MemoryRegion::External ? outside : internal)++;
        return fail || bytes > sizeof(storage) ? nullptr : storage + (misaligned ? 1 : 0);
    }
    void release(void*, size_t, size_t, MemoryRegion) override { ++releases; }
};
struct Owned
{
    int& destroyed;
    int value;
    Owned(int& counter, int v) : destroyed(counter), value(v) {}
    ~Owned() noexcept { ++destroyed; }
};
class HostProvider : public MemoryProvider
{
  public:
    size_t allocated = 0, released = 0;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t alignment, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External && alignment <= alignof(std::max_align_t));
        ++allocated;
        return std::malloc(bytes);
    }
    void release(void* pointer, size_t, size_t, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External);
        ++released;
        std::free(pointer);
    }
};
int main()
{
    HostProvider host;
    {
        MemoryObject<PackageWorkspace> workspace(host);
        MemoryObject<OutdoorPackage> package(host);
        MemoryObject<TerrainReader> terrain(host);
        MemoryObject<GraphReader> graph(host);
        CHECK(workspace.create() == Result::Ok);
        CHECK(package.create(*workspace.get()) == Result::Ok);
        CHECK(terrain.create(*package.get()) == Result::Ok);
        CHECK(graph.create(*package.get()) == Result::Ok);
        CHECK(graph.get()->info().nodes == 0);
        CHECK(host.allocated == 4 && host.released == 0);
    }
    CHECK(host.released == 4);
    Provider provider;
    int destroyed = 0;
    {
        MemoryObject<Owned> object(provider);
        CHECK(object.create(destroyed, 42) == Result::Ok && object.get()->value == 42);
        CHECK(object.region() == MemoryRegion::External && provider.internal == 0);
        CHECK(object.create(destroyed, 1) == Result::Invalid);
        object.reset();
        object.reset();
        CHECK(destroyed == 1 && provider.releases == 1 && !object.get() && !object.bytes());
        provider.fail = true;
        CHECK(object.create(destroyed, 1) == Result::BudgetExceeded && provider.internal == 0);
        provider.fail = false;
        provider.misaligned = true;
        CHECK(object.create(destroyed, 1) == Result::Invalid && destroyed == 1 && provider.releases == 2);
        provider.misaligned = false;
        provider.external = false;
        CHECK(object.create(destroyed, 7) == Result::Ok && object.region() == MemoryRegion::Internal);
    }
    CHECK(destroyed == 2 && provider.releases == 3);
    MemoryObject<Owned> required(provider, MemoryPolicy::RequireExternal);
    const auto calls = provider.internal;
    CHECK(required.create(destroyed, 0) == Result::Missing && provider.internal == calls);
    provider.external = true;
    MemoryObject<Owned> internal(provider, MemoryPolicy::InternalOnly);
    CHECK(internal.create(destroyed, 0) == Result::Ok && internal.region() == MemoryRegion::Internal);
    std::printf("PASS parameterized construction, destruction, priority, failure, alignment, repeated reset; wrapper=%zu\n", sizeof(MemoryObject<Owned>));
}
