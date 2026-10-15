#include "tmap/administration.h"
#include "tmap/legacy_administration.h"
#include "tmap/native_file.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
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
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0;
    bool busy = true;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (++calls % 2 == 0 && busy) return Status::Busy;
        return native.readAt(offset, output, bytes);
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    auto storage = std::make_unique<PackageWorkspace>();
    auto workspace = std::make_unique<AdministrativeWorkspace>();
    auto cursor = std::make_unique<AdministrativeCursor>();
    OutdoorPackage package(*storage);
    File file;
    CHECK(file.native.open((std::string(argv[1]) + "/administration.tmap").c_str()));
    Result r;
    size_t steps = 0;
    do
    {
        r = package.open(file);
        CHECK(++steps < 100);
    } while (retry(r));
    CHECK(r == Result::Ok);
    const auto query = [&](GeoPoint point, const char* expected, bool boundary, uint8_t depth)
    {
        CHECK(beginAdministrativeQuery(package, point, *workspace, *cursor) == Result::Ok);
        size_t count = 0;
        do
        {
            r = administrativeStep(package, *cursor, 1);
            CHECK(++count < 10000);
        } while (retry(r));
        CHECK(r == Result::Ok && std::strcmp(cursor->result.path, expected) == 0);
        CHECK(cursor->result.depth == depth && cursor->result.boundary == boundary && !cursor->result.approximate);
    };
    query({10000, 10000}, "Country / Province / West", false, 3);
    query({10000, 30000}, "Country / Province / East", false, 3);
    query({10000, 60000}, u8"中国 / 云南省 / 昆明市", false, 3);
    query({10000, 20000}, "Country / Province", true, 2);
    query({30000, 10000}, "Country / Province", false, 2);       // Hole in West district.
    query({25000, 10000}, "Country / Province / West", true, 3); // Hole boundary.
    CHECK(beginAdministrativeQuery(package, {90000, 90000}, *workspace, *cursor) == Result::Ok);
    do
    {
        r = administrativeStep(package, *cursor, 2);
    } while (retry(r));
    CHECK(r == Result::Missing && !cursor->result.path[0]);
    CHECK(beginAdministrativeQuery(package, {10000, 10000}, *workspace, *cursor, 2) == Result::Ok);
    do
    {
        r = administrativeStep(package, *cursor, 1);
    } while (retry(r));
    CHECK(r == Result::BudgetExceeded && cursor->result.limited && !cursor->result.path[0]);
    CHECK(beginAdministrativeQuery(package, {10000, 10000}, *workspace, *cursor) == Result::Ok);
    cancelAdministrativeQuery(*cursor);
    CHECK(administrativeStep(package, *cursor, 1) == Result::Cancelled);
    CHECK(beginAdministrativeQuery(package, {10000, 10000}, *workspace, *cursor) == Result::Ok);
    package.close();
    CHECK(administrativeStep(package, *cursor, 1) == Result::Cancelled);
    CHECK(file.native.open((std::string(argv[1]) + "/bad-admin-utf8.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    CHECK(beginAdministrativeQuery(package, {10000, 10000}, *workspace, *cursor) == Result::Ok);
    do
    {
        r = administrativeStep(package, *cursor, 2);
    } while (retry(r));
    CHECK(r == Result::Invalid && !cursor->result.path[0]);
    std::printf("PASS exact E7 containment, holes, shared boundaries/common hierarchy, Busy, budget, cancellation; workspace=%zu cursor=%zu\n", sizeof(AdministrativeWorkspace), sizeof(AdministrativeCursor));
    auto legacy_storage = std::make_unique<Workspace>();
    auto legacy_workspace = std::make_unique<LegacyAdministrativeWorkspace>();
    auto legacy_cursor = std::make_unique<LegacyAdministrativeCursor>();
    auto candidates = std::make_unique<LegacyAdministrativeCandidate[]>(4);
    Reader reader(*legacy_storage);
    CHECK(file.native.open((std::string(argv[1]) + "/legacy-administration.tmap").c_str()));
    file.busy = false;
    CHECK(reader.open(file) == Status::Ok);
    file.busy = true;
    const auto legacy_query = [&](size_t capacity, uint32_t limit, Result expected, const char* path)
    {
        CHECK(LegacyAdministrativeQuery::begin(reader, {10000, 10000}, *legacy_workspace, candidates.get(), capacity, *legacy_cursor, 30000, limit) == Result::Ok);
        size_t count = 0;
        do
        {
            r = LegacyAdministrativeQuery::step(reader, *legacy_cursor, 1);
            CHECK(++count < 10000);
        } while (retry(r));
        CHECK(r == expected && std::strcmp(legacy_cursor->result.path, path) == 0 && legacy_cursor->result.approximate);
    };
    legacy_query(4, 256, Result::Ok, "Country / Province");
    legacy_query(1, 256, Result::Ok, "Country / Province / West");
    legacy_query(4, 1, Result::Incomplete, "Country / Province / West");
    CHECK(legacy_cursor->result.limited);
    CHECK(LegacyAdministrativeQuery::begin(reader, {10000, 10000}, *legacy_workspace, candidates.get(), 4, *legacy_cursor) == Result::Ok);
    LegacyAdministrativeQuery::cancel(*legacy_cursor);
    CHECK(LegacyAdministrativeQuery::step(reader, *legacy_cursor, 1) == Result::Cancelled);
    CHECK(LegacyAdministrativeQuery::begin(reader, {899000000, 0}, *legacy_workspace, candidates.get(), 4, *legacy_cursor, 30000) == Result::Ok);
    CHECK(legacy_cursor->bounds.west == -1800000000 && legacy_cursor->bounds.east == 1799999999);
    std::printf("PASS v1 approximate coordinate hierarchy, nearest candidate capacity, common complete segments, Busy, explicit partial result; workspace=%zu cursor=%zu candidate=%zu\n",
                sizeof(LegacyAdministrativeWorkspace), sizeof(LegacyAdministrativeCursor), sizeof(LegacyAdministrativeCandidate));
}
