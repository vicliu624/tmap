#include "tmap/integer_coordinate_route.h"
#include "tmap/memory_object.h"
#include "tmap/native_file.h"
#include "tmap/native_route_file.h"
#include "tmap/route_archive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
using namespace tmap;
static bool guard = false;
static size_t allocations = 0;
void* operator new(size_t n)
{
    if (guard) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#define CHECK(x)                                                   \
    do                                                             \
    {                                                              \
        if (!(x))                                                  \
        {                                                          \
            std::fprintf(stderr, "archive:%d %s\n", __LINE__, #x); \
            std::exit(1);                                          \
        }                                                          \
    } while (false)
bool retry(Result r)
{
    return r == Result::More || r == Result::Busy;
}
class File final : public WritableRouteFile
{
  public:
    std::vector<uint8_t> data;
    uint64_t read_bytes = 0, written_bytes = 0;
    size_t calls = 0, every = 0;
    bool fail = false;
    File() { data.reserve(2 * 1024 * 1024); }
    uint64_t size() const override { return data.size(); }
    Status readAt(uint64_t offset, uint8_t* out, size_t bytes) override
    {
        if (fail) return Status::IoError;
        if (every && ++calls % every == 0) return Status::Busy;
        if (offset > size() || bytes > size() - offset) return Status::IoError;
        std::memcpy(out, data.data() + offset, bytes);
        read_bytes += bytes;
        return Status::Ok;
    }
    Status writeAt(uint64_t offset, const uint8_t* in, size_t bytes) override
    {
        if (fail) return Status::IoError;
        if (every && ++calls % every == 0) return Status::Busy;
        if (offset + bytes > data.capacity()) return Status::IoError;
        if (offset + bytes > size()) data.resize(size_t(offset + bytes));
        std::memcpy(data.data() + offset, in, bytes);
        written_bytes += bytes;
        return Status::Ok;
    }
};
struct Output
{
    size_t count = 0;
    uint64_t checksum = 0;
};
class LateBusyFile final : public WritableRouteFile
{
  public:
    File backing;
    bool late = true;
    uint64_t size() const override { return backing.size(); }
    Status readAt(uint64_t at, uint8_t* out, size_t count) override { return backing.readAt(at, out, count); }
    Status writeAt(uint64_t at, const uint8_t* in, size_t count) override
    {
        const auto r = backing.writeAt(at, in, count);
        if (r == Status::Ok && late)
        {
            late = false;
            return Status::Busy;
        }
        return r;
    }
};
void referenceCrc(File& file, size_t offset, size_t bytes, size_t crc_offset)
{
    auto* p = file.data.data() + offset;
    uint32_t expected = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        expected |= uint32_t(p[crc_offset + i]) << (8 * i);
        p[crc_offset + i] = 0;
    }
    const auto actual = Reader::crc32c(p, bytes); // Independent 256-entry implementation.
    for (unsigned i = 0; i < 4; ++i) p[crc_offset + i] = uint8_t(expected >> (8 * i));
    CHECK(actual == expected);
}
bool visit(void* context, const RouteArchiveSegment& s)
{
    auto& out = *static_cast<Output*>(context);
    ++out.count;
    out.checksum += s.first.distance_cm + s.last.distance_cm;
    return true;
}
bool reject(void*, const RouteArchiveSegment&) { return false; }
Result run(RouteArchiveReader& reader, RouteArchiveQueryCursor& c, Output& out)
{
    Result r;
    size_t steps = 0;
    do
    {
        r = reader.queryStep(c, 1, visit, &out);
        CHECK(++steps < 2000000);
    } while (retry(r));
    return r;
}
void synthetic()
{
    auto memory = std::make_unique<RouteArchiveWorkspace>();
    auto c = std::make_unique<RouteArchiveQueryCursor>();
    auto second = std::make_unique<RouteArchiveQueryCursor>();
    auto validation = std::make_unique<RouteArchiveValidationCursor>();
    File file;
    file.every = 3;
    RouteArchiveWriter writer(*memory);
    OutdoorInfo map;
    map.id[0] = 1;
    map.build[0] = 2;
    map.revision = 42;
    guard = true;
    Result r;
    do
    {
        r = writer.begin(file, map, 1, 999, 2 * 1024 * 1024);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = writer.writeLeg(0, {12, 123, 456});
    } while (retry(r));
    CHECK(r == Result::Ok);
    for (uint32_t i = 0; i <= 5000; ++i)
    {
        RouteArchivePoint p{{int32_t(i % 13) * 1000, int32_t(i) * 10000}, uint64_t(i) * 100, 0, i == 2400};
        do
        {
            r = writer.append(p);
        } while (retry(r));
        CHECK(r == Result::Ok);
    }
    do
    {
        r = writer.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && writer.info().points == 5001 && writer.info().chunks > 100);
    referenceCrc(file, 0, 192, 136);
    referenceCrc(file, 192, 16, 12);
    referenceCrc(file, 1024, 1024, 4);
    CHECK(writer.append({}) == Result::Invalid);
    RouteArchiveReader reader(*memory);
    do
    {
        r = reader.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok && reader.matches(map));
    CHECK(reader.beginValidation(*validation) == Result::Ok);
    do
    {
        r = reader.validationStep(*validation, 1);
    } while (retry(r));
    CHECK(r == Result::Ok);
    CHECK(reader.info().cost_units == 999 && reader.info().length_cm == 500000);
    IntegerRouteLeg leg;
    do
    {
        r = reader.readLeg(0, leg, map);
    } while (retry(r));
    CHECK(r == Result::Ok && leg.edge == 12 && leg.first_cm == 123 && leg.last_cm == 456);
    ++map.revision;
    CHECK(reader.readLeg(0, leg, map) == Result::Incompatible);
    --map.revision;
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    Output all;
    CHECK(run(reader, *c, all) == Result::Ok && all.count == 4999);
    const auto full_bytes = file.read_bytes;
    RouteArchiveQuery query;
    query.viewport = {24990000, -100, 25010000, 13000};
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    Output local;
    const auto before = file.read_bytes;
    CHECK(run(reader, *c, local) == Result::Ok && local.count == 4);
    CHECK(file.read_bytes - before < file.size() / 8 && full_bytes > file.size());
    query.viewport = {-1800000000, -900000000, 1800000000, 900000000};
    query.first_cm = 249900;
    query.last_cm = 250100;
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    Output distance;
    CHECK(run(reader, *c, distance) == Result::Ok && distance.count == local.count && distance.checksum == local.checksum);
    query.max_nodes = 0;
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    CHECK(reader.queryStep(*c, 1, visit, &distance) == Result::BudgetExceeded);
    query.max_nodes = UINT64_MAX;
    query.max_segments = 0;
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    CHECK(run(reader, *c, distance) == Result::BudgetExceeded);
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    do
    {
        r = reader.queryStep(*c, 1, reject, nullptr);
    } while (retry(r));
    CHECK(r == Result::Cancelled);
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    reader.cancel(*c);
    CHECK(reader.queryStep(*c, 1, visit, &all) == Result::Cancelled);
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    CHECK(reader.beginQuery({}, *second) == Result::Ok);
    Output one, two;
    while (c->terminal == Result::More || second->terminal == Result::More)
    {
        if (c->terminal == Result::More) CHECK(retry(reader.queryStep(*c, 1, visit, &one)) || c->terminal == Result::Ok);
        if (second->terminal == Result::More) CHECK(retry(reader.queryStep(*second, 3, visit, &two)) || second->terminal == Result::Ok);
    }
    CHECK(one.count == all.count && two.checksum == all.checksum);
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    reader.close();
    CHECK(reader.queryStep(*c, 1, visit, &all) == Result::Cancelled);
    file.every = 0;
    CHECK(reader.open(file) == Result::Ok);
    file.data[192] ^= 1;
    CHECK(reader.readLeg(0, leg, map) == Result::Invalid);
    file.data[192] ^= 1;
    file.data.back() ^= 1;
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    CHECK(run(reader, *c, all) == Result::Invalid);
    file.data.back() ^= 1;
    file.data[1024 + 80] ^= 1;
    CHECK(reader.open(file) == Result::Ok && reader.beginQuery({}, *c) == Result::Ok);
    CHECK(run(reader, *c, all) == Result::Invalid);
    CHECK(reader.beginValidation(*validation) == Result::Ok);
    do
    {
        r = reader.validationStep(*validation, 1);
    } while (retry(r));
    CHECK(r == Result::Invalid);
    file.data[1024 + 80] ^= 1;
    file.data.pop_back();
    CHECK(reader.open(file) == Result::Invalid);
    guard = false;
    CHECK(allocations == 0);
    File pending;
    RouteArchiveWriter incomplete(*memory);
    CHECK(incomplete.begin(pending, map, 0, 0, 100) == Result::BudgetExceeded && !pending.size());
    CHECK(incomplete.begin(pending, map, 0, 0, 100000) == Result::Ok);
    CHECK(reader.open(pending) == Result::Incomplete);
    incomplete.cancel();
    CHECK(incomplete.finishStep(1) == Result::Cancelled);
    pending.data.clear();
    RouteArchiveWriter tiny(*memory);
    CHECK(tiny.begin(pending, map, 0, 0, 2112) == Result::Ok);
    for (uint32_t i = 0; i < 80; ++i)
    {
        r = tiny.append({{0, int32_t(i)}, i, 0, false});
        if (r != Result::Ok) break;
    }
    CHECK(r == Result::BudgetExceeded && reader.open(pending) == Result::Incomplete);
    pending.data.clear();
    RouteArchiveWriter exact(*memory);
    CHECK(exact.begin(pending, map, 0, 0, 3264) == Result::Ok);
    for (uint32_t i = 0; i <= 40; ++i) CHECK(exact.append({{0, int32_t(i)}, i, 0, false}) == Result::Ok);
    do
    {
        r = exact.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && pending.size() == 3264);
    pending.data.clear();
    RouteArchiveWriter date(*memory);
    CHECK(date.begin(pending, map, 0, 0, 100000) == Result::Ok);
    CHECK(date.append({{0, 1799000000}, 0, 0, false}) == Result::Ok);
    CHECK(date.append({{0, -1799000000}, 100, 0, false}) == Result::Ok);
    do
    {
        r = date.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && reader.open(pending) == Result::Ok);
    query = {};
    query.viewport = {1799500000, -1, -1799500000, 1};
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    Output wrapped;
    CHECK(run(reader, *c, wrapped) == Result::Ok && wrapped.count == 1);
    query.viewport = {-1, -1, 1, 1};
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    Output greenwich;
    CHECK(run(reader, *c, greenwich) == Result::Ok && greenwich.count == 0);
    LateBusyFile late;
    RouteArchiveWriter opening(*memory);
    CHECK(opening.begin(late, map, 0, 0, 100000) == Result::Busy && late.size() == 192);
    CHECK(opening.finishStep(1) == Result::Invalid);
    CHECK(opening.begin(late, map, 0, 1, 100000) == Result::Invalid);
    CHECK(opening.begin(late, map, 0, 0, 100000) == Result::Ok);
    CHECK(opening.append({{0, 0}, 0, 0, false}) == Result::Ok);
    do
    {
        r = opening.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && reader.open(late) == Result::Ok);
    Bounds view;
    CHECK(routeArchiveViewport({{0, 1799999990}, 18, 320, 240}, view) == Result::Ok && view.west > view.east);
    CHECK(view.south < 0 && view.north > 0);
    CHECK(routeArchiveViewport({{0, 0}, 0, 320, 240}, view) == Result::Ok && view.west == -1800000000 && view.east == 1800000000);
    CHECK(routeArchiveViewport({{0, 0}, 30, 320, 240}, view) == Result::Invalid);
    CHECK(routeArchiveViewport({{0, 0}, 18, 0, 240}, view) == Result::Invalid);
    pending.data.clear();
    RouteArchiveWriter loops(*memory);
    CHECK(loops.begin(pending, map, 0, 0, 100000) == Result::Ok);
    const GeoPoint vertices[4] = {{-100, -100}, {100, 100}, {-100, 100}, {100, -100}};
    for (uint32_t i = 0; i <= 160; ++i) CHECK(loops.append({vertices[i % 4], i * 100, 0, false}) == Result::Ok);
    do
    {
        r = loops.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && reader.open(pending) == Result::Ok);
    query.viewport = {-1, -1, 1, 1};
    CHECK(reader.beginQuery(query, *c) == Result::Ok);
    Output crossing;
    CHECK(run(reader, *c, crossing) == Result::Ok && crossing.count == 80);
    CHECK(reader.beginValidation(*validation) == Result::Ok);
    reader.cancel(*validation);
    CHECK(reader.validationStep(*validation, 1) == Result::Cancelled);
    CHECK(reader.beginValidation(*validation) == Result::Ok);
    pending.fail = true;
    CHECK(reader.validationStep(*validation, 1) == Result::More);
    CHECK(reader.validationStep(*validation, 1) == Result::IoError);
    pending.fail = false;
}
void planner(const std::string& directory)
{
    NativeFile file;
    CHECK(file.open((directory + "/chain.tmap").c_str()));
    auto storage = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*storage);
    CHECK(package.open(file) == Result::Ok);
    GraphReader graph(package);
    auto labels = std::make_unique<IntegerRouteLabel[]>(128);
    auto queue = std::make_unique<uint32_t[]>(128);
    auto cursor = std::make_unique<IntegerCoordinateRouteCursor>();
    File temporary, archive, cancelled;
    temporary.every = 2;
    archive.every = 3;
    RouteLegSpool spool(temporary, 128);
    auto memory = std::make_unique<RouteArchiveWorkspace>();
    auto output = std::make_unique<RouteArchiveExportCursor>();
    auto validation = std::make_unique<RouteArchiveValidationCursor>();
    IntegerRouteLeg array[128];
    IntegerRouteOptions options{{0, 1}, TravelMode::Foot, 10000, IntegerRouteAlgorithm::HeapDijkstra};
    // Match the existing coordinate fixture's in-road endpoints.
    const GeoPoint start{0, 2500}, end{0, 17500};
    Result r;
    do
    {
        r = beginIntegerCoordinateRoute(graph, start, end, options, 1000,
                                        {labels.get(), queue.get(), 128}, array, 128, *cursor);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = integerCoordinateRouteStep(graph, *cursor, 1);
    } while (retry(r));
    CHECK(r == Result::Ok);
    const IntegerRouteLeg* ready = nullptr;
    size_t count = 0;
    uint64_t cost = 0;
    CHECK(integerCoordinateRouteResult(*cursor, ready, count, cost) == Result::Ok && count > 0);
    guard = true;
    do
    {
        r = beginIntegerCoordinateRouteToSink(graph, start, end, options, 1000,
                                              {labels.get(), queue.get(), 128}, spool, 128, *cursor);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t steps = 0;
    do
    {
        r = integerCoordinateRouteStep(graph, *cursor, 1);
        CHECK(++steps < 20000);
    } while (retry(r));
    CHECK(r == Result::Ok && cursor->scratch.searching.count == count && cursor->scratch.searching.cost_units == cost);
    CHECK(integerCoordinateRouteSummary(*cursor, count, cost) == Result::Ok);
    CHECK(integerCoordinateRouteResult(*cursor, ready, count, cost) == Result::Unsupported && !ready);
    count = cursor->scratch.searching.count;
    cost = cursor->scratch.searching.cost_units;
    for (uint32_t i = 0; i < count; ++i)
    {
        IntegerRouteLeg leg;
        do
        {
            r = spool.readLeg(i, leg);
        } while (retry(r));
        CHECK(r == Result::Ok && leg.edge == array[i].edge && leg.first_cm == array[i].first_cm && leg.last_cm == array[i].last_cm);
    }
    labels.reset();
    queue.reset();
    cursor.reset(); // Export does not retain search storage.
    RouteArchiveWriter writer(*memory);
    do
    {
        r = writer.begin(archive, package.info(), uint32_t(count), cost, 100000);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = beginRouteArchiveExport(graph, spool, uint32_t(count), writer, *output);
    } while (retry(r));
    CHECK(r == Result::Ok);
    do
    {
        r = routeArchiveExportStep(graph, spool, writer, *output, 1);
    } while (retry(r));
    CHECK(r == Result::Ok);
    cancelRouteArchiveExport(writer, *output);
    CHECK(output->terminal == Result::Ok && writer.result() == Result::Ok);
    RouteArchiveWriter cancelled_writer(*memory);
    CHECK(cancelled_writer.begin(cancelled, package.info(), uint32_t(count), cost, 100000) == Result::Ok);
    CHECK(beginRouteArchiveExport(graph, spool, uint32_t(count), cancelled_writer, *output) == Result::Ok);
    cancelRouteArchiveExport(cancelled_writer, *output);
    CHECK(routeArchiveExportStep(graph, spool, cancelled_writer, *output, 1) == Result::Cancelled);
    CHECK(cancelled_writer.finishStep(1) == Result::Cancelled);
    cancelled.data.clear();
    RouteArchiveWriter generation_writer(*memory);
    CHECK(generation_writer.begin(cancelled, package.info(), uint32_t(count), cost, 100000) == Result::Ok);
    CHECK(beginRouteArchiveExport(graph, spool, uint32_t(count), generation_writer, *output) == Result::Ok);
    package.close();
    CHECK(routeArchiveExportStep(graph, spool, generation_writer, *output, 1) == Result::Cancelled);
    RouteArchiveReader incomplete(*memory);
    CHECK(incomplete.open(cancelled) == Result::Incomplete);
    storage.reset();
    output.reset();
    RouteArchiveReader reader(*memory);
    do
    {
        r = reader.open(archive);
    } while (retry(r));
    CHECK(r == Result::Ok && reader.info().legs == count && reader.info().cost_units == cost);
    CHECK(reader.beginValidation(*validation) == Result::Ok);
    do
    {
        r = reader.validationStep(*validation, 1);
    } while (retry(r));
    CHECK(r == Result::Ok);
    RouteArchiveQueryCursor query;
    CHECK(reader.beginQuery({}, query) == Result::Ok);
    Output out;
    CHECK(run(reader, query, out) == Result::Ok && out.count > 0);
    guard = false;
    CHECK(allocations == 0);
}
class ExternalProvider final : public MemoryProvider
{
  public:
    size_t internal = 0, external = 0, live = 0, peak = 0;
    bool fail = false;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t, MemoryRegion region) override
    {
        if (region == MemoryRegion::Internal)
        {
            ++internal;
            return nullptr;
        }
        ++external;
        if (fail) return nullptr;
        auto* out = std::malloc(bytes);
        if (out)
        {
            live += bytes;
            peak = std::max(live, peak);
        }
        return out;
    }
    void release(void* p, size_t bytes, size_t, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External);
        live -= bytes;
        std::free(p);
    }
};
void externalOwnership()
{
    ExternalProvider provider;
    {
        MemoryObject<RouteArchiveWorkspace> workspace(provider);
        MemoryObject<RouteArchiveReader> reader(provider);
        MemoryObject<RouteArchiveQueryCursor> query(provider);
        CHECK(workspace.create() == Result::Ok);
        CHECK(reader.create(*workspace.get()) == Result::Ok);
        CHECK(query.create() == Result::Ok);
        CHECK(provider.live == sizeof(RouteArchiveWorkspace) + sizeof(RouteArchiveReader) + sizeof(RouteArchiveQueryCursor));
        provider.fail = true;
        MemoryObject<RouteArchiveExportCursor> refused(provider);
        CHECK(refused.create() == Result::BudgetExceeded);
        CHECK(provider.internal == 0 && provider.external == 4);
    }
    CHECK(provider.live == 0);
}
void nativeRoundtrip(const std::string& directory)
{
    const auto path = directory + "/archive-native-test.tmroute";
    std::remove(path.c_str()); // This test's own generated artifact, never a map.
    NativeRouteFile file, collision;
    CHECK(file.openNew(path.c_str()) && !collision.openNew(path.c_str()));
    auto memory = std::make_unique<RouteArchiveWorkspace>();
    RouteArchiveWriter writer(*memory);
    OutdoorInfo map;
    CHECK(writer.begin(file, map, 0, 0, 100000) == Result::Ok);
    CHECK(writer.append({{0, 0}, 0, 0, false}) == Result::Ok);
    CHECK(writer.append({{0, 100}, 100, 0, false}) == Result::Ok);
    Result r;
    do
    {
        r = writer.finishStep(1);
    } while (retry(r));
    CHECK(r == Result::Ok && file.flush());
    file.close();
    NativeFile read;
    CHECK(read.open(path.c_str()));
    RouteArchiveReader reader(*memory);
    CHECK(reader.open(read) == Result::Ok);
    auto c = std::make_unique<RouteArchiveQueryCursor>();
    CHECK(reader.beginQuery({}, *c) == Result::Ok);
    Output out;
    CHECK(run(reader, *c, out) == Result::Ok && out.count == 1);
    read.close();
    CHECK(std::remove(path.c_str()) == 0);
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    synthetic();
    planner(argv[1]);
    externalOwnership();
    nativeRoundtrip(argv[1]);
    return 0;
}
