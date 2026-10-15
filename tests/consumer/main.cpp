#include <tmap/navigation.h>
#include <tmap/outdoor_session.h>
#include <tmap/package.h>
#include <tmap/profile_job.h>
#include <tmap/resource_budget.h>
#include <tmap/route_archive.h>
#include <tmap/route_storage.h>
static tmap::PackageWorkspace storage;
static tmap::RouteArchiveWorkspace route_pages;
static tmap::RouteArchiveQueryCursor route_query;
class EmptyFile final : public tmap::RandomAccessFile
{
  public:
    uint64_t size() const override { return 0; }
    tmap::Status readAt(uint64_t, uint8_t*, size_t) override { return tmap::Status::Missing; }
};
class UnavailableMemory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t, size_t, tmap::MemoryRegion region) override
    {
        if (region == tmap::MemoryRegion::Internal) ++internal_requests;
        return nullptr;
    }
    void release(void*, size_t, size_t, tmap::MemoryRegion) override { ++releases; }
    size_t internal_requests = 0, releases = 0;
};
int main()
{
    tmap::ResourceOptions options;
    options.integer_route = true;
    options.integer_label_scan = true;
    options.route_states = 128;
    options.route_legs = 64;
    tmap::ResourceBudget budget;
    if (tmap::resourceBudget(options, budget) != tmap::Result::Ok ||
        budget.total_bytes < sizeof(tmap::PackageWorkspace) + 128 * sizeof(tmap::IntegerRouteLabel)) return 2;
    UnavailableMemory provider;
    EmptyFile file;
    tmap::RouteArchiveReader archive(route_pages);
    if (archive.open(file) != tmap::Result::Incomplete || archive.beginQuery({}, route_query) != tmap::Result::Invalid) return 6;
    tmap::MemoryObject<tmap::RouteArchiveWorkspace> saved_pages(provider);
    if (saved_pages.create() != tmap::Result::BudgetExceeded || provider.internal_requests) return 7;
    tmap::ProfileJob profile(provider);
    const tmap::GeoPoint line[] = {{0, 0}, {1000, 1000}};
    if (profile.begin(file, line, 2) != tmap::Result::BudgetExceeded || profile.reservedBytes() || provider.internal_requests)
        return 5;
    tmap::IntegerRouteStorage route(provider);
    if (route.beginLabelScan(128, 64, budget.total_bytes) != tmap::Result::More ||
        route.initializeStep(1) != tmap::Result::BudgetExceeded || route.reservedBytes()) return 3;
    tmap::MemoryObject<tmap::PackageWorkspace> pages(provider);
    if (pages.create() != tmap::Result::BudgetExceeded || pages.get() ||
        provider.internal_requests || provider.releases) return 4;
    tmap::OutdoorPackage package(storage);
    package.close();
    tmap::ScreenPoint point{};
    if (!tmap::project({{0, 0}, 14, 320, 240}, {0, 0}, point)) return 1;
    return package.isOpen() || point.x != 160 || point.y != 120 ? 1 : 0;
}
