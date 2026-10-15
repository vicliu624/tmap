#include "tmap/coordinate_route.h"
#include "tmap/edge_rules.h"
#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_route.h"
#include "tmap/native_file.h"
#include "tmap/navigation.h"
#include "tmap/package.h"
#include "tmap/road_rules.h"
#include "tmap/routing_rules.h"
#include "tmap/section_file.h"
#include "tmap/terrain.h"
#include "tmap/turn_context.h"
#include "tmap/turn_rules.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    NativeFile native;
    size_t calls = 0, every = 2;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        if (++calls % every == 0) return Status::Busy;
        return native.readAt(at, output, bytes);
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    auto storage = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*storage);
    File file;
    CHECK(file.native.open((std::string(argv[1]) + "/read-stream.tmap").c_str()));
    Result r;
    size_t attempts = 0;
    do
    {
        r = package.open(file, 1);
        CHECK(++attempts < 200);
    } while (retry(r));
    CHECK(r == Result::Ok && package.pageBytes() <= PackageWorkspace::kPageBytes);
    const auto* section = package.section(SectionType::ContourData);
    CHECK(section && section->bytes > package.payloadBytes() * 3);
    const size_t bytes = package.payloadBytes() * 3 + 17;
    std::vector<uint8_t> expected(bytes), output(bytes);
    ReadCursor c{};
    for (size_t at = 0; at < bytes;)
    {
        const auto count = std::min<size_t>(bytes - at, package.payloadBytes());
        CHECK(file.native.readAt(section->offset + (at / package.payloadBytes()) * package.pageBytes() + 32, expected.data() + at, count) == Status::Ok);
        at += count;
    }
    package.close();
    attempts = 0;
    do
    {
        r = package.open(file, 1);
        CHECK(++attempts < 200);
    } while (retry(r));
    CHECK(r == Result::Ok);
    CHECK(beginRead(package, SectionType::ContourData, 0, output.data(), bytes, c) == Result::Ok);
    attempts = 0;
    do
    {
        const auto pages = package.pageReads();
        r = readStep(package, c, 1);
        CHECK(package.pageReads() - pages <= 1 && ++attempts < 100);
    } while (retry(r));
    CHECK(r == Result::Ok && output == expected && c.completed == bytes);
    PackageSectionFile view(package, SectionType::ContourData);
    CHECK(view.size() == section->bytes);
    std::array<uint8_t, 24> record{};
    const auto record_offset = package.payloadBytes() - 8;
    Status status;
    attempts = 0;
    do
    {
        const auto reads = package.pageReads();
        status = view.readAt(record_offset, record.data(), record.size());
        CHECK(package.pageReads() - reads <= 1 && ++attempts < 100);
    } while (status == Status::Busy || status == Status::More);
    CHECK(status == Status::Ok);
    CHECK(std::equal(record.begin(), record.end(), expected.begin() + record_offset));
    CHECK(beginRead(package, SectionType::ContourData, 0, output.data(), bytes, c) == Result::Ok);
    cancelRead(c);
    CHECK(readStep(package, c, 1) == Result::Cancelled);
    CHECK(beginRead(package, SectionType::ContourData, 0, output.data(), bytes, c) == Result::Ok);
    package.close();
    CHECK(readStep(package, c, 1) == Result::Cancelled);
    CHECK(view.size() == 0 && view.readAt(0, record.data(), record.size()) == Status::Cancelled);
    CHECK(file.native.open((std::string(argv[1]) + "/dem-boundary.tmap").c_str()));
    do
    {
        r = package.open(file, 1);
    } while (retry(r));
    CHECK(r == Result::Ok);
    TerrainReader terrain(package);
    ElevationSample height{};
    attempts = 0;
    do
    {
        r = terrain.elevation({3150, 3150}, height);
        CHECK(++attempts < 100);
    } while (retry(r));
    CHECK(r == Result::Ok && height.meters == 94.5);
    ElevationCursor elevation{};
    CHECK(terrain.beginElevation({3150, 3150}, elevation) == Result::Ok);
    attempts = 0;
    do
    {
        const auto before = elevation.next;
        r = terrain.elevationStep(elevation, 1, height);
        CHECK(elevation.next - before <= 1 && ++attempts < 100);
    } while (retry(r));
    CHECK(r == Result::Ok && height.meters == 94.5);
    height.meters = 0;
    CHECK(terrain.elevationStep(elevation, 1, height) == Result::Ok && height.meters == 94.5);
    cancelElevation(elevation);
    CHECK(terrain.elevationStep(elevation, 1, height) == Result::Cancelled);
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-road-program.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    RoadProgramReadCursor program_read;
    std::array<RoadRuleRange, 7> programs{};
    uint8_t highway = 99;
    bool reversed = true;
    CHECK(beginRoadProgramRead(package, 0, program_read) == Result::Ok);
    attempts = 0;
    do
    {
        r = roadProgramReadStep(package, program_read, 1, programs, highway, reversed);
        CHECK(++attempts < 100);
        if (retry(r)) CHECK(highway == 99 && reversed);
    } while (retry(r));
    CHECK(r == Result::Ok && highway == 3 && !reversed && programs[2].count == 1);
    PackageSectionFile conditions(package, SectionType::RoadRuleRecords);
    RoadRuleCursor rules;
    RoadMasks masks{99, 99};
    CHECK(beginRoadRules(conditions, programs, 0, 0, highway, 3, rules, 4096, PackageSectionFile::epoch, &conditions) == Result::Ok);
    attempts = 0;
    do
    {
        r = roadRulesStep(rules, 1, masks);
        CHECK(++attempts < 100);
    } while (retry(r));
    CHECK(r == Result::Ok && masks.forward == 3 && masks.backward == 3);
    uint32_t dynamic_row = UINT32_MAX;
    std::array<uint8_t, 4> reference{};
    const auto* refs = package.section(SectionType::RoadRuleReferences);
    for (uint32_t row = 0; row < refs->count; ++row)
    {
        do
        {
            r = package.record(SectionType::RoadRuleReferences, row, 4, reference.data());
        } while (retry(r));
        CHECK(r == Result::Ok);
        if (reference[0] != 255)
        {
            dynamic_row = row;
            break;
        }
    }
    CHECK(dynamic_row != UINT32_MAX);
    EdgeRuleReader edge_rules(package, 345600, 0); // Monday; foot closed, bicycle allowed.
    uint8_t resolved_access = 99;
    CHECK(edge_rules.begin(dynamic_row, 3) == Result::More);
    attempts = 0;
    do
    {
        r = edge_rules.step(1, resolved_access);
        CHECK(++attempts < 100);
        if (retry(r)) CHECK(resolved_access == 99);
    } while (retry(r));
    CHECK(r == Result::Ok && resolved_access == 2);
    CHECK(edge_rules.begin(dynamic_row, 1) == Result::More);
    do
    {
        r = edge_rules.step(1, resolved_access);
    } while (retry(r));
    CHECK(r == Result::Ok && resolved_access == 0);
    CHECK(edge_rules.begin(dynamic_row, 3) == Result::More);
    edge_rules.cancel();
    CHECK(edge_rules.step(1, resolved_access) == Result::Cancelled);
    CHECK(edge_rules.begin(dynamic_row, 3) == Result::More);
    do
    {
        r = edge_rules.step(1, resolved_access);
    } while (retry(r));
    CHECK(r == Result::Ok && resolved_access == 2);
    std::printf("EDGE rule_reader=%zu\n", sizeof(edge_rules));
    CHECK(beginRoadProgramRead(package, 0, program_read) == Result::Ok);
    cancel(program_read);
    CHECK(roadProgramReadStep(package, program_read, 1, programs, highway, reversed) == Result::Cancelled);
    CHECK(beginRoadProgramRead(package, 0, program_read) == Result::Ok);
    package.close();
    CHECK(roadProgramReadStep(package, program_read, 1, programs, highway, reversed) == Result::Cancelled);
    std::printf("PROGRAM read_cursor=%zu ranges=%zu\n", sizeof(program_read), sizeof(programs));
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-road-route.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    GraphReader unsupported(package);
    do
    {
        r = unsupported.initialize();
    } while (retry(r));
    CHECK(r == Result::Unsupported);
    EdgeRuleReader monday(package, 345600, 0), thursday(package, 0, 0);
    GraphReader closed(package, &monday), opened(package, &thursday);
    GraphEdge edge;
    edge.access = 99;
    attempts = 0;
    do
    {
        r = closed.edge(0, edge);
        CHECK(++attempts < 100);
        if (retry(r)) CHECK(edge.access == 99);
    } while (retry(r));
    CHECK(r == Result::Ok && edge.access == 2);
    std::array<RouteLabel, 16> labels{};
    std::array<uint32_t, 16> heap{};
    RouteCursor route;
    RouteOptions options;
    options.mode = TravelMode::Foot;
    for (auto* graph : {&closed, &opened})
    {
        do
        {
            r = beginRoute(*graph, 0, 2, options, {labels.data(), heap.data(), labels.size()}, route);
        } while (retry(r));
        CHECK(r == Result::Ok);
        attempts = 0;
        do
        {
            r = routeStep(*graph, route, 1);
            CHECK(++attempts < 500);
        } while (retry(r));
        CHECK(r == (graph == &closed ? Result::NoPath : Result::Ok));
    }
    std::array<IntegerRouteLabel, 16> integer_labels{};
    std::array<uint32_t, 16> lower_bounds{};
    IntegerLandmarkWorkspace landmark;
    IntegerRouteCursor integer_route;
    size_t dynamic_routes = 0, dynamic_snaps = 0;
    for (auto* graph : {&closed, &opened})
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
        {
            const auto expected_result = graph == &closed && mode == TravelMode::Foot ? Result::NoPath : Result::Ok;
            for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
            {
                options.mode = mode;
                options.algorithm = algorithm;
                do
                {
                    r = beginRoute(*graph, 0, 2, options, {labels.data(), heap.data(), labels.size()}, route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = routeStep(*graph, route, 1);
                    CHECK(++attempts < 1000);
                } while (retry(r));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 2> rows{};
                    size_t count = 99;
                    CHECK(routeEdges(route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 2 && rows[0] == 0 && rows[1] == 1);
                }
                ++dynamic_routes;
            }
            for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
            {
                IntegerRouteOptions integer_options;
                integer_options.mode = mode;
                integer_options.algorithm = algorithm;
                do
                {
                    r = beginIntegerRoute(*graph, 0, 2, integer_options, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = integerRouteStep(*graph, integer_route, 1);
                    CHECK(++attempts < 1000);
                } while (retry(r));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 2> rows{};
                    size_t count = 99;
                    CHECK(integerRouteEdges(integer_route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 2 && rows[0] == 0 && rows[1] == 1);
                }
                ++dynamic_routes;
            }
            SnapCursor snap;
            do
            {
                r = beginSnap(*graph, {0, 5000}, mode, 2, snap);
            } while (retry(r));
            CHECK(r == Result::Ok);
            attempts = 0;
            do
            {
                r = snapStep(*graph, snap, 1);
                CHECK(++attempts < 500);
            } while (retry(r));
            CHECK(r == (expected_result == Result::Ok ? Result::Ok : Result::Missing));
            if (r == Result::Ok) CHECK(snap.best.edge == 0);
            ++dynamic_snaps;
        }
    CHECK(dynamic_routes == 32 && dynamic_snaps == 4);
    std::printf("DYNAMIC routes=%zu snaps=%zu\n", dynamic_routes, dynamic_snaps);
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-road-coordinate.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    EdgeRuleReader both_rules(package, 345600, 0), forward_rules(package, 0, 0);
    GraphReader both_directions(package, &both_rules), forward_only(package, &forward_rules);
    std::array<RouteLeg, 8> float_legs{};
    std::array<IntegerRouteLeg, 8> integer_legs{};
    CoordinateRouteCursor coordinate_route;
    IntegerCoordinateRouteCursor integer_coordinate;
    size_t coordinate_cases = 0;
    for (auto* graph : {&both_directions, &forward_only})
        for (bool reverse : {false, true})
            for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
            {
                const GeoPoint start{0, reverse ? 17500 : 2500}, end{0, reverse ? 2500 : 17500};
                const auto expected_result = graph == &forward_only && reverse ? Result::NoPath : Result::Ok;
                for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
                {
                    options.mode = mode;
                    options.algorithm = algorithm;
                    do
                    {
                        r = beginCoordinateRoute(*graph, start, end, options, 2, {labels.data(), heap.data(), labels.size()}, float_legs.data(), float_legs.size(), coordinate_route);
                    } while (retry(r));
                    CHECK(r == Result::Ok);
                    attempts = 0;
                    do
                    {
                        r = coordinateRouteStep(*graph, coordinate_route, 1);
                        CHECK(++attempts < 3000);
                    } while (retry(r));
                    CHECK(r == expected_result);
                    if (r == Result::Ok)
                    {
                        CHECK(coordinate_route.count == 2);
                        CHECK(float_legs[0].edge == (reverse ? 3u : 0u) && float_legs[1].edge == (reverse ? 1u : 2u));
                        CHECK(float_legs[0].first_fraction > .24 && float_legs[0].first_fraction < .26);
                        CHECK(float_legs[1].last_fraction > .74 && float_legs[1].last_fraction < .76);
                    }
                    ++coordinate_cases;
                }
                for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
                {
                    IntegerRouteOptions integer_options;
                    integer_options.mode = mode;
                    integer_options.algorithm = algorithm;
                    do
                    {
                        r = beginIntegerCoordinateRoute(*graph, start, end, integer_options, 2, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_legs.data(), integer_legs.size(), integer_coordinate);
                    } while (retry(r));
                    CHECK(r == Result::Ok);
                    attempts = 0;
                    do
                    {
                        r = integerCoordinateRouteStep(*graph, integer_coordinate, 1);
                        CHECK(++attempts < 3000);
                    } while (retry(r));
                    CHECK(r == expected_result);
                    if (r == Result::Ok)
                    {
                        const IntegerRouteLeg* result_legs = nullptr;
                        size_t count = 99;
                        uint64_t cost = 0;
                        CHECK(integerCoordinateRouteResult(integer_coordinate, result_legs, count, cost) == Result::Ok);
                        CHECK(count == 2 && result_legs[0].edge == (reverse ? 3u : 0u) && result_legs[1].edge == (reverse ? 1u : 2u));
                        CHECK(result_legs[0].first_cm > 0 && result_legs[0].last_cm > result_legs[0].first_cm);
                        CHECK(result_legs[1].last_cm > result_legs[1].first_cm && cost > 0);
                    }
                    ++coordinate_cases;
                }
            }
    CHECK(coordinate_cases == 64);
    std::printf("COORDINATE cases=%zu\n", coordinate_cases);
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-barrier-route.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t barrier_cases = 0;
    for (auto utc : {uint64_t(345600), uint64_t(432000), uint64_t(0)})
    {
        EdgeRuleReader barrier_rules(package, utc, 0);
        GraphReader graph(package, &barrier_rules);
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
        {
            const auto expected_result = (utc == 345600 && mode == TravelMode::Foot) ||
                                                 (utc == 432000 && mode == TravelMode::Bicycle)
                                             ? Result::NoPath
                                             : Result::Ok;
            for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
            {
                options.mode = mode;
                options.algorithm = algorithm;
                do
                {
                    r = beginRoute(graph, 0, 2, options, {labels.data(), heap.data(), labels.size()}, route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = routeStep(graph, route, 1);
                    CHECK(++attempts < 2000);
                } while (retry(r));
                CHECK(r == expected_result);
                ++barrier_cases;
            }
            for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
            {
                IntegerRouteOptions integer_options;
                integer_options.mode = mode;
                integer_options.algorithm = algorithm;
                do
                {
                    r = beginIntegerRoute(graph, 0, 2, integer_options, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = integerRouteStep(graph, integer_route, 1);
                    CHECK(++attempts < 2000);
                } while (retry(r));
                CHECK(r == expected_result);
                ++barrier_cases;
            }
        }
        CHECK(barrier_rules.begin(0, 3) == Result::More);
        r = barrier_rules.step(1, resolved_access);
        CHECK(retry(r));
        barrier_rules.cancel();
        CHECK(barrier_rules.step(1, resolved_access) == Result::Cancelled);
        CHECK(barrier_rules.begin(0, 3) == Result::More);
        attempts = 0;
        do
        {
            r = barrier_rules.step(1, resolved_access);
            CHECK(++attempts < 200);
        } while (retry(r));
        CHECK(r == Result::Ok && resolved_access == (utc == 345600 ? 2 : utc == 432000 ? 1
                                                                                       : 3));
    }
    CHECK(barrier_cases == 48);
    std::printf("BARRIER cases=%zu reader=%zu\n", barrier_cases, sizeof(EdgeRuleReader));
    size_t invalid_barriers = 0;
    for (const auto* name : {"direction", "reversed", "inactive-action", "reference", "range"})
    {
        package.close();
        CHECK(file.native.open((std::string(argv[1]) + "/runtime-barrier-invalid-" + name + ".tmap").c_str()));
        do
        {
            r = package.open(file);
        } while (retry(r));
        CHECK(r == Result::Ok);
        EdgeRuleReader invalid_rules(package, 345600, 0);
        GraphReader invalid_graph(package, &invalid_rules);
        GraphEdge output_edge;
        output_edge.access = 99;
        attempts = 0;
        do
        {
            r = invalid_graph.edge(0, output_edge);
            CHECK(++attempts < 500);
            CHECK(output_edge.access == 99);
        } while (retry(r));
        CHECK(r == Result::Invalid);
        const auto calls = file.calls;
        CHECK(invalid_graph.edge(0, output_edge) == Result::Invalid);
        CHECK(file.calls == calls && output_edge.access == 99);
        ++invalid_barriers;
    }
    CHECK(invalid_barriers == 5);
    std::printf("BARRIER invalid=%zu\n", invalid_barriers);
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-turn-program.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    TurnProgramReadCursor turn_read;
    std::array<RoadRuleRange, 2> turn_ranges{};
    uint8_t turn_mode = 99;
    for (auto row : {24u, 101u})
    {
        CHECK(beginTurnProgramRead(package, row, turn_read) == Result::Ok);
        attempts = 0;
        turn_mode = 99;
        do
        {
            const auto reads = package.pageReads();
            r = turnProgramReadStep(package, turn_read, 1, turn_ranges, turn_mode);
            CHECK(++attempts < 100 && package.pageReads() - reads <= 1);
            if (retry(r)) CHECK(turn_mode == 99);
        } while (retry(r));
        CHECK(r == Result::Ok && turn_mode == 2 && turn_ranges[1].fallback == 11);
    }
    PackageSectionFile turn_data(package, SectionType::TurnRuleRecords);
    TurnRuleCursor turn_rules;
    uint8_t turn_value = 99;
    CHECK(beginTurnRules(turn_data, turn_ranges, 0, 0, turn_rules, 4096, PackageSectionFile::epoch, &turn_data) == Result::Ok);
    attempts = 0;
    do
    {
        r = turnRulesStep(turn_rules, 1, turn_value);
        CHECK(++attempts < 100);
    } while (retry(r));
    CHECK(r == Result::Ok && turn_value == 0);
    CHECK(beginTurnProgramRead(package, 0, turn_read) == Result::Ok);
    cancel(turn_read);
    CHECK(turnProgramReadStep(package, turn_read, 1, turn_ranges, turn_mode) == Result::Cancelled);
    CHECK(beginTurnProgramRead(package, 0, turn_read) == Result::Ok);
    package.close();
    CHECK(turnProgramReadStep(package, turn_read, 1, turn_ranges, turn_mode) == Result::Cancelled);
    std::printf("TURN_PAGED cursor=%zu ranges=%zu\n", sizeof(turn_read), sizeof(turn_ranges));
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-turn-context.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t context_cases = 0;
    for (auto utc : {uint64_t(0), uint64_t(345600)})
    {
        TurnContextReader context_reader(package, utc, 0);
        CHECK(context_reader.validFor(package, utc, 0));
        CHECK(!context_reader.validFor(package, utc + 1, 0));
        CHECK(!context_reader.validFor(package, utc, 3600));
        for (uint32_t context = 0; context < 3; ++context)
            for (uint32_t outgoing = 0; outgoing < 4; ++outgoing)
                for (uint8_t mode : {uint8_t(1), uint8_t(2)})
                {
                    const bool expected = mode == 2 || context == 0 || (utc == 0 ? outgoing == context : context != 2 || outgoing != 2);
                    bool allowed = !expected;
                    CHECK(context_reader.begin(context, outgoing, mode, 4) == Result::More);
                    attempts = 0;
                    do
                    {
                        r = context_reader.step(1, allowed);
                        CHECK(++attempts < 500);
                        if (retry(r)) CHECK(allowed == !expected);
                    } while (retry(r));
                    CHECK(r == Result::Ok && allowed == expected);
                    const auto calls = file.calls;
                    allowed = !expected;
                    CHECK(context_reader.read(context, outgoing, mode, 4, allowed) == Result::Ok && allowed == expected);
                    CHECK(file.calls == calls);
                    ++context_cases;
                }
        bool allowed = true;
        CHECK(context_reader.begin(1, 1, 1, 4, 0) == Result::More);
        do
        {
            r = context_reader.step(1, allowed);
        } while (retry(r));
        CHECK(r == Result::BudgetExceeded && allowed);
        CHECK(context_reader.begin(1, 1, 1, 4) == Result::More);
        context_reader.cancel();
        CHECK(context_reader.step(1, allowed) == Result::Cancelled && allowed);
        CHECK(context_reader.read(1, 1, 1, 4, allowed) == Result::Cancelled && allowed);
        CHECK(context_reader.begin(1, 1, 1, 4) == Result::More);
        attempts = 0;
        do
        {
            r = context_reader.read(1, 1, 1, 4, allowed);
            CHECK(++attempts < 500);
        } while (retry(r));
        CHECK(r == Result::Ok);
    }
    CHECK(context_cases == 48);
    std::printf("TURN_CONTEXT cases=%zu reader=%zu\n", context_cases, sizeof(TurnContextReader));
    size_t turn_routes = 0;
    GraphReader unbound_turns(package);
    do
    {
        r = unbound_turns.initialize();
    } while (retry(r));
    CHECK(r == Result::Unsupported);
    for (auto utc : {uint64_t(0), uint64_t(345600)})
    {
        TurnContextReader route_turns(package, utc, 0);
        RuntimeRoutingRules binding(utc, 0, nullptr, &route_turns);
        GraphReader graph(package, binding);
        RuntimeRoutingRules wrong_binding(utc + 1, 0, nullptr, &route_turns);
        GraphReader wrong_graph(package, wrong_binding);
        do
        {
            r = wrong_graph.initialize();
        } while (retry(r));
        CHECK(r == Result::Incompatible);
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
        {
            const auto expected_result = utc == 345600 && mode == TravelMode::Foot ? Result::NoPath : Result::Ok;
            for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
            {
                options.mode = mode;
                options.algorithm = algorithm;
                do
                {
                    r = beginRoute(graph, 0, 3, options, {labels.data(), heap.data(), labels.size()}, route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = routeStep(graph, route, 1);
                    CHECK(++attempts < 2000);
                } while (retry(r));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 3> rows{};
                    size_t count = 99;
                    CHECK(routeEdges(route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == 2);
                }
                ++turn_routes;
            }
            for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
            {
                IntegerRouteOptions integer_options;
                integer_options.mode = mode;
                integer_options.algorithm = algorithm;
                do
                {
                    r = beginIntegerRoute(graph, 0, 3, integer_options, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = integerRouteStep(graph, integer_route, 1);
                    CHECK(++attempts < 2000);
                } while (retry(r));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 3> rows{};
                    size_t count = 99;
                    CHECK(integerRouteEdges(integer_route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == 2);
                }
                ++turn_routes;
            }
        }
    }
    CHECK(turn_routes == 32);
    std::printf("TURN_ROUTES cases=%zu binding=%zu\n", turn_routes, sizeof(RuntimeRoutingRules));
    size_t turn_clipped = 0;
    for (auto utc : {uint64_t(0), uint64_t(345600)})
    {
        TurnContextReader turn_reader(package, utc, 0);
        RuntimeRoutingRules binding(utc, 0, nullptr, &turn_reader);
        GraphReader graph(package, binding);
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
            for (unsigned scenario = 0; scenario < 3; ++scenario)
            {
                const GeoPoint start{0, scenario == 1 ? 12500 : 2500}, end{0, scenario == 2 ? 15000 : 25000};
                const auto expected_result = utc == 345600 && mode == TravelMode::Foot && scenario == 0 ? Result::NoPath : Result::Ok;
                const auto first_edge = scenario == 1 ? 1u : 0u, last_edge = scenario == 2 ? 1u : 2u;
                const auto expected_count = scenario == 0 ? 3u : 2u;
                for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
                {
                    options.mode = mode;
                    options.algorithm = algorithm;
                    do
                    {
                        r = beginCoordinateRoute(graph, start, end, options, 2, {labels.data(), heap.data(), labels.size()}, float_legs.data(), float_legs.size(), coordinate_route);
                    } while (retry(r));
                    CHECK(r == Result::Ok);
                    attempts = 0;
                    do
                    {
                        r = coordinateRouteStep(graph, coordinate_route, 1);
                        CHECK(++attempts < 4000);
                    } while (retry(r));
                    CHECK(r == expected_result);
                    if (r == Result::Ok)
                    {
                        CHECK(coordinate_route.count == expected_count && float_legs[0].edge == first_edge && float_legs[expected_count - 1].edge == last_edge);
                        CHECK(float_legs[0].first_fraction > .24 && float_legs[0].first_fraction < .26);
                        CHECK(float_legs[expected_count - 1].last_fraction > .49 && float_legs[expected_count - 1].last_fraction < .51);
                    }
                    ++turn_clipped;
                }
                for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
                {
                    IntegerRouteOptions integer_options;
                    integer_options.mode = mode;
                    integer_options.algorithm = algorithm;
                    do
                    {
                        r = beginIntegerCoordinateRoute(graph, start, end, integer_options, 2, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_legs.data(), integer_legs.size(), integer_coordinate);
                    } while (retry(r));
                    CHECK(r == Result::Ok);
                    attempts = 0;
                    do
                    {
                        r = integerCoordinateRouteStep(graph, integer_coordinate, 1);
                        CHECK(++attempts < 4000);
                    } while (retry(r));
                    CHECK(r == expected_result);
                    if (r == Result::Ok)
                    {
                        const IntegerRouteLeg* result_legs = nullptr;
                        size_t count = 99;
                        uint64_t cost = 0;
                        CHECK(integerCoordinateRouteResult(integer_coordinate, result_legs, count, cost) == Result::Ok);
                        CHECK(count == expected_count && result_legs[0].edge == first_edge && result_legs[count - 1].edge == last_edge);
                        CHECK(result_legs[0].first_cm > 0 && result_legs[count - 1].last_cm > 0 && cost > 0);
                    }
                    ++turn_clipped;
                }
            }
    }
    CHECK(turn_clipped == 96);
    std::printf("TURN_CLIPPED cases=%zu\n", turn_clipped);
    size_t grouped_cases = 0, invalid_turns = 0;
    size_t grouped_routes = 0;
    for (const auto* name : {"union", "intersection"})
    {
        package.close();
        CHECK(file.native.open((std::string(argv[1]) + "/runtime-turn-" + name + ".tmap").c_str()));
        do
        {
            r = package.open(file);
        } while (retry(r));
        CHECK(r == Result::Ok);
        const bool intersection = std::string(name) == "intersection";
        for (auto utc : {uint64_t(0), uint64_t(345600)})
        {
            TurnContextReader turns(package, utc, 0);
            RuntimeRoutingRules binding(utc, 0, nullptr, &turns);
            GraphReader graph(package, binding);
            for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
                for (uint32_t destination : {3u, 4u})
                {
                    const auto expected_result = mode == TravelMode::Foot && (utc == 345600 || (intersection && destination == 3)) ? Result::NoPath : Result::Ok;
                    for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
                    {
                        options.mode = mode;
                        options.algorithm = algorithm;
                        do
                        {
                            r = beginRoute(graph, 0, destination, options, {labels.data(), heap.data(), labels.size()}, route);
                        } while (retry(r));
                        CHECK(r == Result::Ok);
                        attempts = 0;
                        do
                        {
                            r = routeStep(graph, route, 1);
                            CHECK(++attempts < 2000);
                        } while (retry(r));
                        CHECK(r == expected_result);
                        if (r == Result::Ok)
                        {
                            std::array<uint32_t, 3> rows{};
                            size_t count = 99;
                            CHECK(routeEdges(route, rows.data(), rows.size(), count) == Result::Ok);
                            CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == destination - 1);
                        }
                        ++grouped_routes;
                    }
                    for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
                    {
                        IntegerRouteOptions integer_options;
                        integer_options.mode = mode;
                        integer_options.algorithm = algorithm;
                        do
                        {
                            r = beginIntegerRoute(graph, 0, destination, integer_options, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_route);
                        } while (retry(r));
                        CHECK(r == Result::Ok);
                        attempts = 0;
                        do
                        {
                            r = integerRouteStep(graph, integer_route, 1);
                            CHECK(++attempts < 2000);
                        } while (retry(r));
                        CHECK(r == expected_result);
                        if (r == Result::Ok)
                        {
                            std::array<uint32_t, 3> rows{};
                            size_t count = 99;
                            CHECK(integerRouteEdges(integer_route, rows.data(), rows.size(), count) == Result::Ok);
                            CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == destination - 1);
                        }
                        ++grouped_routes;
                    }
                }
        }
        for (auto utc : {uint64_t(0), uint64_t(345600)})
        {
            TurnContextReader grouped(package, utc, 0);
            for (uint32_t context = 0; context < 3; ++context)
                for (uint32_t outgoing = 0; outgoing < 4; ++outgoing)
                {
                    const bool expected = context == 0 || (utc == 0 ? (context == 1 ? outgoing == 1 : (intersection ? outgoing == 3 : outgoing >= 2)) : (context != 2 || outgoing < 2));
                    bool allowed = !expected;
                    CHECK(grouped.begin(context, outgoing, 1, 4) == Result::More);
                    attempts = 0;
                    do
                    {
                        r = grouped.step(1, allowed);
                        CHECK(++attempts < 500);
                        if (retry(r)) CHECK(allowed == !expected);
                    } while (retry(r));
                    CHECK(r == Result::Ok && allowed == expected);
                    ++grouped_cases;
                }
        }
    }
    for (const auto* name : {"index-range", "index-reserved", "match-reference", "match-flags", "match-complete", "match-next", "relation-range", "program-domain", "inactive-action"})
    {
        package.close();
        CHECK(file.native.open((std::string(argv[1]) + "/runtime-turn-invalid-" + name + ".tmap").c_str()));
        do
        {
            r = package.open(file);
        } while (retry(r));
        CHECK(r == Result::Ok);
        TurnContextReader invalid(package, 0, 0);
        CHECK(invalid.begin(2, 0, 1, 4) == Result::More);
        bool allowed = true;
        attempts = 0;
        do
        {
            r = invalid.step(1, allowed);
            CHECK(++attempts < 500 && allowed);
        } while (retry(r));
        CHECK(r == Result::Invalid);
        const auto calls = file.calls;
        CHECK(invalid.step(1, allowed) == Result::Invalid && allowed && file.calls == calls);
        ++invalid_turns;
    }
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-turn-overlap.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    for (auto utc : {uint64_t(0), uint64_t(345600)})
    {
        TurnContextReader overlap(package, utc, 0);
        for (uint32_t context = 0; context < 3; ++context)
            for (uint32_t outgoing = 0; outgoing < 2; ++outgoing)
            {
                const bool expected = utc != 0 || context == 0 || (context == 1 && outgoing == 0);
                bool allowed = !expected;
                CHECK(overlap.begin(context, outgoing, 1, 2) == Result::More);
                attempts = 0;
                do
                {
                    r = overlap.step(1, allowed);
                    CHECK(++attempts < 500);
                    if (retry(r)) CHECK(allowed == !expected);
                } while (retry(r));
                CHECK(r == Result::Ok && allowed == expected);
                ++grouped_cases;
            }
    }
    TurnContextReader phase_cancel(package, 0, 0);
    for (size_t phase_steps = 1; phase_steps <= 30; ++phase_steps)
    {
        bool allowed = true;
        CHECK(phase_cancel.begin(2, 0, 1, 2) == Result::More);
        for (size_t step = 0; step < phase_steps; ++step)
        {
            r = phase_cancel.step(1, allowed);
            CHECK(r == Result::Ok || retry(r));
        }
        phase_cancel.cancel();
        CHECK(phase_cancel.step(1, allowed) == Result::Cancelled);
        CHECK(phase_cancel.begin(2, 0, 1, 2) == Result::More);
        allowed = true;
        attempts = 0;
        do
        {
            r = phase_cancel.step(1, allowed);
            CHECK(++attempts < 500);
            if (retry(r)) CHECK(allowed);
        } while (retry(r));
        CHECK(r == Result::Ok && !allowed);
    }
    CHECK(grouped_cases == 60 && invalid_turns == 9);
    CHECK(grouped_routes == 128);
    std::printf("TURN_GROUPED_ROUTES cases=%zu\n", grouped_routes);
    std::printf("TURN_GROUPED cases=%zu invalid=%zu\n", grouped_cases, invalid_turns);
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + "/runtime-combined-rules.tmap").c_str()));
    do
    {
        r = package.open(file);
    } while (retry(r));
    CHECK(r == Result::Ok);
    size_t combined_cases = 0;
    for (auto utc : {uint64_t(0), uint64_t(345600), uint64_t(432000), uint64_t(518400)})
    {
        EdgeRuleReader roads(package, utc, 0);
        TurnContextReader turns(package, utc, 0);
        RuntimeRoutingRules binding(utc, 0, &roads, &turns);
        GraphReader graph(package, binding);
        RuntimeRoutingRules wrong_roads(utc + 1, 0, &roads, &turns);
        CHECK(!wrong_roads.validFor(package));
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
        {
            const auto expected_result = (mode == TravelMode::Foot && (utc == 345600 || utc == 432000)) ||
                                                 (mode == TravelMode::Bicycle && utc == 518400)
                                             ? Result::NoPath
                                             : Result::Ok;
            for (auto algorithm : {RouteAlgorithm::Auto, RouteAlgorithm::Dijkstra, RouteAlgorithm::Landmarks, RouteAlgorithm::QueueRelaxation})
            {
                options.mode = mode;
                options.algorithm = algorithm;
                do
                {
                    r = beginRoute(graph, 0, 3, options, {labels.data(), heap.data(), labels.size()}, route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = routeStep(graph, route, 1);
                    CHECK(++attempts < 3000);
                } while (retry(r));
                if (r != expected_result) std::fprintf(stderr, "combined utc=%llu mode=%u algorithm=%u result=%s expected=%s\n", static_cast<unsigned long long>(utc), unsigned(mode), unsigned(algorithm), resultName(r), resultName(expected_result));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 3> rows{};
                    size_t count = 99;
                    CHECK(routeEdges(route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == 2);
                }
                ++combined_cases;
                const auto clipped_expected = mode == TravelMode::Foot && utc == 432000 ? Result::Missing : expected_result;
                do
                {
                    r = beginCoordinateRoute(graph, {0, 2500}, {0, 25000}, options, 2, {labels.data(), heap.data(), labels.size()}, float_legs.data(), float_legs.size(), coordinate_route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = coordinateRouteStep(graph, coordinate_route, 1);
                    CHECK(++attempts < 6000);
                } while (retry(r));
                CHECK(r == clipped_expected);
                if (r == Result::Ok) CHECK(coordinate_route.count == 3 && float_legs[0].edge == 0 && float_legs[1].edge == 1 && float_legs[2].edge == 2);
                ++combined_cases;
            }
            for (auto algorithm : {IntegerRouteAlgorithm::QueueRelaxation, IntegerRouteAlgorithm::Dijkstra, IntegerRouteAlgorithm::HeapDijkstra, IntegerRouteAlgorithm::Landmarks})
            {
                IntegerRouteOptions integer_options;
                integer_options.mode = mode;
                integer_options.algorithm = algorithm;
                do
                {
                    r = beginIntegerRoute(graph, 0, 3, integer_options, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_route);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = integerRouteStep(graph, integer_route, 1);
                    CHECK(++attempts < 3000);
                } while (retry(r));
                CHECK(r == expected_result);
                if (r == Result::Ok)
                {
                    std::array<uint32_t, 3> rows{};
                    size_t count = 99;
                    CHECK(integerRouteEdges(integer_route, rows.data(), rows.size(), count) == Result::Ok);
                    CHECK(count == 3 && rows[0] == 0 && rows[1] == 1 && rows[2] == 2);
                }
                ++combined_cases;
                const auto clipped_expected = mode == TravelMode::Foot && utc == 432000 ? Result::Missing : expected_result;
                do
                {
                    r = beginIntegerCoordinateRoute(graph, {0, 2500}, {0, 25000}, integer_options, 2, {integer_labels.data(), heap.data(), integer_labels.size(), lower_bounds.data(), &landmark}, integer_legs.data(), integer_legs.size(), integer_coordinate);
                } while (retry(r));
                CHECK(r == Result::Ok);
                attempts = 0;
                do
                {
                    r = integerCoordinateRouteStep(graph, integer_coordinate, 1);
                    CHECK(++attempts < 6000);
                } while (retry(r));
                CHECK(r == clipped_expected);
                if (r == Result::Ok)
                {
                    const IntegerRouteLeg* result_legs = nullptr;
                    size_t count = 99;
                    uint64_t cost = 0;
                    CHECK(integerCoordinateRouteResult(integer_coordinate, result_legs, count, cost) == Result::Ok);
                    CHECK(count == 3 && result_legs[0].edge == 0 && result_legs[1].edge == 1 && result_legs[2].edge == 2);
                }
                ++combined_cases;
            }
        }
    }
    CHECK(combined_cases == 128);
    std::printf("COMBINED_RULES cases=%zu\n", combined_cases);
    size_t label_scan_cases = 0;
    for (auto utc : {uint64_t(0), uint64_t(345600), uint64_t(432000), uint64_t(518400)})
    {
        EdgeRuleReader roads(package, utc, 0);
        TurnContextReader turns(package, utc, 0);
        RuntimeRoutingRules binding(utc, 0, &roads, &turns);
        GraphReader graph(package, binding);
        for (auto mode : {TravelMode::Foot, TravelMode::Bicycle})
        {
            IntegerRouteOptions selected;
            selected.mode = mode;
            selected.algorithm = IntegerRouteAlgorithm::LabelScanDijkstra;
            const auto expected = (mode == TravelMode::Foot && (utc == 345600 || utc == 432000)) ||
                                          (mode == TravelMode::Bicycle && utc == 518400)
                                      ? Result::NoPath
                                      : Result::Ok;
            IntegerRouteWorkspace queue_free{integer_labels.data(), nullptr, integer_labels.size()};
            do
            {
                r = beginIntegerRoute(graph, 0, 3, selected, queue_free, integer_route);
            } while (retry(r));
            CHECK(r == Result::Ok && !integer_route.workspace.queue);
            attempts = 0;
            do
            {
                r = integerRouteStep(graph, integer_route, 1);
                CHECK(++attempts < 10000);
            } while (retry(r));
            CHECK(r == expected);
            ++label_scan_cases;
            do
            {
                r = beginIntegerCoordinateRoute(graph, {0, 2500}, {0, 25000}, selected, 2, queue_free,
                                                integer_legs.data(), integer_legs.size(), integer_coordinate);
            } while (retry(r));
            CHECK(r == Result::Ok);
            attempts = 0;
            do
            {
                r = integerCoordinateRouteStep(graph, integer_coordinate, 1);
                CHECK(++attempts < 15000);
            } while (retry(r));
            CHECK(r == (mode == TravelMode::Foot && utc == 432000 ? Result::Missing : expected));
            if (r == Result::Ok)
            {
                const IntegerRouteLeg* legs = nullptr;
                size_t count = 0;
                uint64_t cost = 0;
                CHECK(integerCoordinateRouteResult(integer_coordinate, legs, count, cost) == Result::Ok);
                CHECK(count == 3 && legs[0].edge == 0 && legs[1].edge == 1 && legs[2].edge == 2);
            }
            ++label_scan_cases;
        }
    }
    CHECK(label_scan_cases == 16);
    std::printf("LABEL_SCAN_COMBINED cases=%zu\n", label_scan_cases);
    std::printf("GRAPH reader=%zu info=%zu\n", sizeof(GraphReader), sizeof(GraphInfo));
    std::printf("PASS bounded open, chunked-directory CRC, multi-page read, Busy, cancellation; workspace=%zu package=%zu read_cursor=%zu\n", sizeof(PackageWorkspace), sizeof(OutdoorPackage), sizeof(ReadCursor));
}
