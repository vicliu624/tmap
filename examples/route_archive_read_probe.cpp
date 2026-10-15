#include "tmap/memory_object.h"
#include "tmap/native_file.h"
#include "tmap/route_archive.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
using namespace tmap;
class ExternalMemory final : public MemoryProvider
{
  public:
    size_t live = 0, peak = 0, external = 0, internal = 0;
    bool refuse = false;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t, MemoryRegion region) override
    {
        if (region == MemoryRegion::Internal)
        {
            ++internal;
            return nullptr;
        }
        ++external;
        if (refuse) return nullptr;
        auto* p = std::malloc(bytes);
        if (p)
        {
            live += bytes;
            peak = std::max(peak, live);
        }
        return p;
    }
    void release(void* p, size_t bytes, size_t, MemoryRegion) override
    {
        live -= bytes;
        std::free(p);
    }
};
bool accept(void* context, const RouteArchiveSegment&)
{
    ++*static_cast<size_t*>(context);
    return true;
}
int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) return 2;
    NativeFile file;
    if (!file.open(argv[1])) return 1;
    ExternalMemory provider;
    provider.refuse = argc == 3;
    Result r = Result::Invalid;
    size_t segments = 0;
    using Pages = MemoryObject<RouteArchiveWorkspace>;
    using ReaderOwner = MemoryObject<RouteArchiveReader>;
    using QueryOwner = MemoryObject<RouteArchiveQueryCursor>;
    {
        Pages pages(provider);
        ReaderOwner reader(provider);
        QueryOwner query(provider);
        r = pages.create();
        if (r == Result::Ok) r = reader.create(*pages.get());
        if (r == Result::Ok) r = query.create();
        if (r == Result::Ok) r = reader.get()->open(file);
        if (r == Result::Ok) r = reader.get()->beginQuery({}, *query.get());
        if (r == Result::Ok)
            do
            {
                r = reader.get()->queryStep(*query.get(), 1, accept, &segments);
            } while (r == Result::More || r == Result::Busy);
    }
    std::printf("{\"result\":\"%s\",\"external_requests\":%zu,\"internal_requests\":%zu,\"provider_peak_bytes\":%zu,"
                "\"provider_live_after_reset\":%zu,\"owner_controls_bytes\":%zu,\"provider_control_bytes\":%zu,\"segments\":%zu}\n",
                r == Result::Ok ? "ok" : r == Result::BudgetExceeded ? "budget_exceeded"
                                                                     : "error",
                provider.external, provider.internal, provider.peak, provider.live,
                sizeof(Pages) + sizeof(ReaderOwner) + sizeof(QueryOwner), sizeof(ExternalMemory), segments);
    return provider.internal || provider.live || (provider.refuse ? r != Result::BudgetExceeded : r != Result::Ok) ? 1 : 0;
}
