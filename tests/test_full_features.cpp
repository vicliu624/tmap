#include "tmap/native_file.h"
#include "tmap/resource_budget.h"
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
struct File : RandomAccessFile
{
    NativeFile native;
    size_t attempts = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        if (++attempts % 2 == 0) return Status::Busy;
        return native.readAt(at, output, bytes);
    }
};
bool retry(Result r) { return r == Result::Busy || r == Result::More; }
int main(int argc, char** argv)
{
    CHECK(argc == 2 || (argc == 3 && !std::strcmp(argv[2], "center")));
    const bool center = argc == 3;
    auto storage = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*storage);
    File file;
    Result r;
    size_t steps = 0;
    for (const auto count : {37U, 64U, 65U})
    {
        if (center) break;
        const auto name = count == 37 ? "runtime-full-features.tmap" : count == 64 ? "directory-64.tmap"
                                                                                   : "directory-65-invalid.tmap";
        package.close();
        CHECK(file.native.open((std::string(argv[1]) + "/" + name).c_str()));
        do
        {
            r = package.open(file, 1);
            CHECK(++steps < 1000);
        } while (retry(r));
        const auto expected = count == 65 ? Result::Invalid : count > PackageWorkspace::kSections ? Result::BudgetExceeded
                                                                                                  : Result::Ok;
        CHECK(r == expected);
        CHECK(package.isOpen() == (r == Result::Ok));
        if (r != Result::Ok) CHECK(!package.sectionAt(0));
        else CHECK(package.sectionAt(count - 1) && !package.sectionAt(count));
    }
    if (PackageWorkspace::kSections < 37 && !center)
    {
        std::printf("FULL_FEATURES capacity_rejection=37,64 invalid=65 slots=%zu workspace=%zu\n", PackageWorkspace::kSections, sizeof(PackageWorkspace));
        return 0;
    }
    package.close();
    CHECK(file.native.open((std::string(argv[1]) + (center ? "/navigation.tmap" : "/runtime-full-features.tmap")).c_str()));
    do
    {
        r = package.open(file, 1);
    } while (retry(r));
    if (center && PackageWorkspace::kSections < 37)
    {
        CHECK(r == Result::BudgetExceeded && !package.isOpen());
        std::printf("FULL_FEATURES center_capacity_rejection=37 slots=%zu workspace=%zu\n", PackageWorkspace::kSections, sizeof(PackageWorkspace));
        return 0;
    }
    CHECK(r == Result::Ok && package.info().capabilities == 15 && package.sectionAt(36) && !package.sectionAt(37));
    auto admin_storage = std::make_unique<AdministrativeWorkspace>();
    auto admin = std::make_unique<AdministrativeCursor>();
    auto render = std::make_unique<ContourRenderCursor>();
    std::array<ContourSegment, 16> segments{};
    std::array<IntegerRouteLabel, 32> labels{};
    std::array<uint32_t, 32> queue{}, bounds{}, output{};
    IntegerLandmarkWorkspace landmarks;
    IntegerRouteCursor route;
    IntegerElevationCursor elevation;
    IntegerElevationPrefix height;
    CHECK(beginAdministrativeQuery(package, {5000, 5000}, *admin_storage, *admin) == Result::Ok);
    CHECK(beginContourRender(package, {{10000, 10000}, 16, 320, 240}, segments.data(), segments.size(), {}, *render) == Result::Ok);
    EdgeRuleReader roads(package, 0, 0);
    TurnContextReader turns(package, 0, 0);
    RuntimeRoutingRules binding(0, 0, &roads, &turns);
    GraphReader graph(package, binding);
    IntegerRouteOptions options;
    options.algorithm = IntegerRouteAlgorithm::Landmarks;
    do
    {
        r = beginIntegerRoute(graph, 0, 3, options, {labels.data(), queue.data(), labels.size(), bounds.data(), &landmarks}, route);
    } while (retry(r));
    CHECK(r == Result::Ok);
    uint32_t half_length = 0;
    {
        GraphEdge edge;
        do
        {
            r = graph.edge(0, edge);
        } while (retry(r));
        CHECK(r == Result::Ok);
        half_length = edge.length_cm / 2;
    }
    CHECK(beginIntegerElevation(graph, 0, half_length, elevation) == Result::Ok);
    Result a = Result::More, c = Result::More, e = Result::More, n = Result::More;
    steps = 0;
    // Serialized round-robin calls retain all service cursors and share one cache.
    while (retry(a) || retry(c) || retry(e) || retry(n))
    {
        if (retry(a)) a = administrativeStep(package, *admin, 1);
        if (retry(c)) c = contourRenderStep(package, *render, 1);
        if (retry(e)) e = integerElevationStep(graph, elevation, 1, height);
        if (retry(n)) n = integerRouteStep(graph, route, 1);
        CHECK(++steps < 10000);
    }
    std::printf("JOINT results admin=%s contour=%s elevation=%s route=%s path=%s depth=%u ascent=%u segments=%zu\n", resultName(a), resultName(c), resultName(e), resultName(n), admin->result.path ? admin->result.path : "", unsigned(admin->result.depth), height.ascent_cm, render->count);
    CHECK(a == Result::Ok && admin->result.depth == 2 && !std::strcmp(admin->result.path, "Test Country / Test District"));
    CHECK(c == Result::Ok && render->count == 3);
    CHECK(e == Result::Ok && height.ascent_cm == 500 && height.descent_cm == 0);
    CHECK(n == Result::Ok);
    size_t count = 0;
    CHECK(integerRouteEdges(route, output.data(), output.size(), count) == Result::Ok);
    CHECK(count == 3 && output[0] == 0 && output[1] == (center ? 2U : 1U) && output[2] == (center ? 3U : 2U));
    ResourceOptions resources;
    resources.route_states = labels.size();
    resources.route_legs = output.size();
    resources.integer_route = resources.integer_landmarks = resources.integer_elevation = true;
    resources.edge_rules = resources.turn_context = resources.runtime_binding = true;
    resources.administration = true;
    resources.contour_segments = segments.size();
    ResourceBudget budget;
    CHECK(resourceBudget(resources, budget) == Result::Ok);
    const size_t retained = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) + sizeof(roads) + sizeof(turns) + sizeof(binding) +
                            sizeof(AdministrativeWorkspace) + sizeof(AdministrativeCursor) + sizeof(ContourRenderCursor) + sizeof(segments) +
                            sizeof(labels) + sizeof(queue) + sizeof(bounds) + sizeof(output) + sizeof(landmarks) + sizeof(route) + sizeof(elevation) + sizeof(height);
    CHECK(budget.total_bytes == retained + sizeof(uint8_t) + sizeof(bool));
    std::printf("FULL_FEATURES sections=37 slots=%zu workspace=%zu payload=%zu rounds=%zu read_attempts=%zu route_edges=%zu contour_segments=%zu ascent_cm=%u admin_depth=%u\n",
                PackageWorkspace::kSections, sizeof(PackageWorkspace), budget.total_bytes, steps, file.attempts, count, render->count, height.ascent_cm, unsigned(admin->result.depth));
}
