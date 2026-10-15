#include "tmap/administration.h"
#include "tmap/composite_file.h"
#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/terrain.h"
#include "tmap/virtual_map.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
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
    std::vector<uint8_t> bytes;
    size_t calls = 0;
    uint64_t generation = 1;
    uint64_t size() const override { return bytes.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        if (++calls % 2) return Status::Busy;
        if (at > bytes.size() || count > bytes.size() - at) return Status::Invalid;
        std::memcpy(output, bytes.data() + size_t(at), count);
        return Status::Ok;
    }
};
uint64_t sourceEpoch(void* file) { return static_cast<File*>(file)->generation; }
bool contourVertex(void* context, const ContourVertex&)
{
    ++*static_cast<size_t*>(context);
    return true;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    File a, b, c;
    a.bytes = {'x', 'A', 'B', 'C'};
    b.bytes = {'x', 'D', 'E', 'F'};
    c.bytes = {'x', 'G', 'H', 'I'};
    FileExtent extents[3]{{&a, 0, 1, 3}, {&b, 3, 1, 3}, {&c, 6, 1, 3}};
    CompositeFile file;
    CHECK(file.bind(extents, 3) == Status::Ok && file.size() == 9);
    uint8_t output[9]{};
    Status status = Status::More;
    size_t steps = 0;
    do
    {
        status = file.readAt(0, output, 9);
        CHECK(++steps < 20);
    } while (status == Status::Busy || status == Status::More);
    CHECK(status == Status::Ok && !std::memcmp(output, "ABCDEFGHI", 9));
    CHECK(a.calls == 2 && b.calls == 2 && c.calls == 2);
    file.cancelRead();
    CHECK(file.readAt(0, output, 9) == Status::Cancelled);
    file.restartRead();
    CHECK(file.readAt(0, output, 9) == Status::Busy);
    file.cancelRead();
    file.restartRead();
    CHECK(file.readAt(1, output, 2) == Status::Ok && !std::memcmp(output, "BC", 2));
    CHECK(file.readAt(8, output, 2) == Status::Invalid);
    CHECK(file.readAt(9, nullptr, 0) == Status::Ok);
    FileExtent bad[2]{{&a, 0, 1, 3}, {&b, 4, 1, 3}};
    CHECK(file.bind(bad, 2) == Status::Invalid && file.size() == 0);
    bad[0].source = &file;
    CHECK(file.bind(bad, 2) == Status::Invalid);
    NativeFile native;
    CHECK(native.open((std::string(argv[1]) + "/chain.tmap").c_str()));
    std::vector<uint8_t> bytes(size_t(native.size()));
    CHECK(native.readAt(0, bytes.data(), bytes.size()) == Status::Ok);
    const size_t cut = 2 * TMAP_CACHE_PAGE_BYTES + 73;
    CHECK(cut < bytes.size());
    a.bytes.assign(bytes.begin(), bytes.begin() + cut);
    b.bytes.assign(bytes.begin() + cut, bytes.end());
    FileExtent package_extents[2]{{&a, 0, 0, cut}, {&b, cut, 0, b.bytes.size()}};
    CHECK(file.bind(package_extents, 2) == Status::Ok);
    auto workspace = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*workspace);
    Result result;
    steps = 0;
    do
    {
        result = package.open(file);
        CHECK(++steps < 1000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    TerrainReader terrain(package);
    ElevationSample sample;
    do
    {
        result = terrain.elevation({0, 0}, sample);
        CHECK(++steps < 1000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && sample.meters == 100);
    package.close();
    file.detach();
    CHECK(!file.size());
    File sources[3];
    const char* names[3]{"virtual-a.tmap", "virtual-b.tmap", "virtual-patch.bin"};
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(native.open((std::string(argv[1]) + "/" + names[i]).c_str()));
        sources[i].bytes.resize(size_t(native.size()));
        CHECK(native.readAt(0, sources[i].bytes.data(), sources[i].bytes.size()) == Status::Ok);
    }
    auto manifest = std::fopen((std::string(argv[1]) + "/virtual-extents.txt").c_str(), "r");
    CHECK(manifest);
    std::vector<FileExtent> mapped;
    unsigned source;
    unsigned long long virtual_offset, source_offset, length;
    while (std::fscanf(manifest, "%u %llu %llu %llu", &source, &virtual_offset, &source_offset, &length) == 4)
    {
        CHECK(source < 3);
        mapped.push_back({sources + source, virtual_offset, source_offset, length});
    }
    std::fclose(manifest);
    CHECK(file.bind(mapped.data(), mapped.size()) == Status::Ok);
    CHECK(native.open((std::string(argv[1]) + "/virtual-reference.tmap").c_str()));
    bytes.resize(size_t(native.size()));
    CHECK(native.readAt(0, bytes.data(), bytes.size()) == Status::Ok);
    std::vector<uint8_t> reconstructed(bytes.size());
    status = Status::More;
    steps = 0;
    do
    {
        status = file.readAt(0, reconstructed.data(), reconstructed.size());
        CHECK(++steps < 10000);
    } while (status == Status::Busy || status == Status::More);
    CHECK(status == Status::Ok && reconstructed == bytes);
    do
    {
        result = package.open(file);
        CHECK(++steps < 10000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    GraphReader graph(package);
    IntegerRouteLabel labels[32];
    uint32_t queue[32], route[8];
    IntegerRouteCursor cursor;
    for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
    {
        do
        {
            result = beginIntegerRoute(graph, 0, 3, {{0, 1}, mode, 1000}, {labels, queue, 32}, cursor);
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok);
        do
        {
            result = integerRouteStep(graph, cursor, 1);
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        if (mode == TravelMode::Bicycle) CHECK(result == Result::NoPath);
        else
        {
            size_t count = 0;
            CHECK(result == Result::Ok && integerRouteEdges(cursor, route, 8, count) == Result::Ok && count == 3);
        }
    }
    package.close();
    file.detach();
    File binary_manifest;
    CHECK(native.open((std::string(argv[1]) + "/virtual-map.bin").c_str()));
    binary_manifest.bytes.resize(size_t(native.size()));
    CHECK(native.readAt(0, binary_manifest.bytes.data(), binary_manifest.bytes.size()) == Status::Ok);
    FileIdentity identity;
    identity.bytes = binary_manifest.size();
    auto digest_file = std::fopen((std::string(argv[1]) + "/virtual-map.sha256").c_str(), "r");
    CHECK(digest_file);
    char hex[65];
    CHECK(std::fscanf(digest_file, "%64s", hex) == 1);
    std::fclose(digest_file);
    for (size_t i = 0; i < 32; ++i)
    {
        unsigned byte;
        CHECK(std::sscanf(hex + i * 2, "%2x", &byte) == 1);
        identity.sha256[i] = uint8_t(byte);
    }
    VirtualSource slots[3];
    for (size_t i = 0; i < 3; ++i) slots[i] = {sources + i, sourceEpoch, sources + i, 0, 0};
    VerifiedVirtualFile verified;
    auto loader = std::make_unique<VirtualMapCursor>();
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size(), verified, *loader, UINT64_MAX,
                          sourceEpoch, &binary_manifest) == Result::More);
    CHECK(!verified.size());
    steps = 0;
    do
    {
        result = virtualMapStep(*loader, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && verified.size() == bytes.size());
    do
    {
        result = package.open(verified);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    GraphReader verified_graph(package);
    TerrainReader regional_terrain(package);
    ElevationSample regional_height{};
    do
    {
        result = regional_terrain.elevation({5000, 15000}, regional_height);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && regional_height.meters == 25);
    ContourCursor regional_contours;
    size_t regional_vertices = 0;
    do
    {
        result = beginContours(package, {0, 0, 30000, 10000}, 16, regional_contours);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    do
    {
        result = contourStep(package, regional_contours, 1, contourVertex, &regional_vertices);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && regional_vertices > 0);
    auto regional_admin_workspace = std::make_unique<AdministrativeWorkspace>();
    auto regional_admin = std::make_unique<AdministrativeCursor>();
    CHECK(beginAdministrativeQuery(package, {5000, 25000}, *regional_admin_workspace, *regional_admin) == Result::Ok);
    do
    {
        result = administrativeStep(package, *regional_admin, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && regional_admin->result.path && !std::strcmp(regional_admin->result.path, "country / east"));
    do
    {
        result = beginIntegerRoute(verified_graph, 0, 3, {}, {labels, queue, 32}, cursor);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    do
    {
        result = integerRouteStep(verified_graph, cursor, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok);
    ++sources[0].generation;
    CHECK(verified.readAt(0, output, 1) == Status::Cancelled && virtualMapStep(*loader, 1) == Result::Cancelled);
    package.close();
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size(), verified, *loader, 1) == Result::More);
    do
    {
        result = virtualMapStep(*loader, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::BudgetExceeded && !verified.size());
    sources[1].bytes.back() ^= 1;
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size(), verified, *loader, UINT64_MAX) == Result::More);
    steps = 0;
    do
    {
        result = virtualMapStep(*loader, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Incompatible && !verified.size());
    sources[1].bytes.back() ^= 1;
    binary_manifest.bytes.back() ^= 1;
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size(), verified, *loader, UINT64_MAX) == Result::More);
    steps = 0;
    do
    {
        result = virtualMapStep(*loader, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Incompatible && !verified.size());
    binary_manifest.bytes.back() ^= 1;
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size() - 1, verified, *loader, UINT64_MAX) == Result::More);
    steps = 0;
    do
    {
        result = virtualMapStep(*loader, 1);
        CHECK(++steps < 20000);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::BudgetExceeded && !verified.size());
    CHECK(beginVirtualMap(binary_manifest, identity, slots, 3, mapped.data(), mapped.size(), verified, *loader, UINT64_MAX) == Result::More);
    cancel(*loader);
    CHECK(virtualMapStep(*loader, 1) == Result::Cancelled && !verified.size());
    std::printf("PASS exact cross-source bytes, Busy progress without prefix replay, cancel/restart, invalid extents, split TMAP page/elevation; adapter=%zu extent=%zu\n", sizeof(CompositeFile), sizeof(FileExtent));
}
