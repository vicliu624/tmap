#include "tmap/cancellation.h"
#include "tmap/native_file.h"
#include "tmap/outdoor_session.h"
#include "tmap/route_storage.h"
#include <cstdio>
#include <cstdlib>
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
class Provider : public MemoryProvider
{
  public:
    size_t calls = 0, fail_at = 0, live = 0, live_bytes = 0;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t alignment, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External && alignment <= alignof(std::max_align_t));
        if (++calls == fail_at) return nullptr;
        void* pointer = std::malloc(bytes);
        CHECK(pointer);
        ++live;
        live_bytes += bytes;
        return pointer;
    }
    void release(void* pointer, size_t bytes, size_t, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External && live && live_bytes >= bytes);
        --live;
        live_bytes -= bytes;
        std::free(pointer);
    }
};
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        return ++calls % 2 == 0 ? Status::Busy : native.readAt(at, output, bytes);
    }
};
Result initialize(OutdoorSession& session)
{
    Result result = Result::More;
    for (size_t step = 0; step < 10000 && (result == Result::More || result == Result::Busy); ++step)
        result = session.initializeStep(1);
    return result;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string directory = argv[1];
    File file;
    CHECK(file.native.open((directory + "/chain.tmap").c_str()));
    const SessionOptions all{true, true, SIZE_MAX};
    size_t cancellation_points = 0;
    bool completed = false;
    for (size_t cut = 0; cut < 10000 && !completed; ++cut)
    {
        Provider sliced_provider;
        OutdoorSession sliced(sliced_provider);
        file.calls = 0; // Keep the same Busy schedule for each prefix.
        CHECK(sliced.begin(file, all) == Result::More);
        auto state = Result::More;
        for (size_t step = 0; step < cut && (state == Result::More || state == Result::Busy); ++step)
            state = sliced.initializeStep(1);
        CHECK(state == Result::More || state == Result::Busy || state == Result::Ok);
        completed = state == Result::Ok;
        if (!completed) CHECK(!sliced.package() && !sliced.graph() && !sliced.terrain());
        sliced.cancel();
        CHECK(!sliced_provider.live && !sliced_provider.live_bytes && !sliced.reservedBytes());
        CHECK(!sliced.package() && !sliced.graph() && !sliced.terrain());
        CHECK(sliced.initializeStep(1) == Result::Cancelled);
        CHECK(sliced.begin(file, all) == Result::More && initialize(sliced) == Result::Ok);
        CHECK(sliced.reservedBytes() == sliced_provider.live_bytes);
        sliced.reset();
        CHECK(!sliced_provider.live && !sliced_provider.live_bytes);
        ++cancellation_points;
    }
    CHECK(completed);
    std::printf("SESSION_CANCEL_SLICES checked=%zu busy=1 retry_each=1 external_only=1 zero_live_bytes_each=1\n", cancellation_points);
    for (size_t fail = 1; fail <= 4; ++fail)
    {
        Provider provider;
        provider.fail_at = fail;
        OutdoorSession session(provider);
        CHECK(session.begin(file, all) == Result::More);
        CHECK(session.initializeStep(0) == Result::More && provider.calls == 0);
        CHECK(initialize(session) == Result::BudgetExceeded && provider.live == 0 && !session.reservedBytes());
        CHECK(!session.package() && !session.graph() && !session.terrain());
        provider.fail_at = 0;
        CHECK(session.begin(file, all) == Result::More);
        CHECK(initialize(session) == Result::Ok && provider.live == 4);
        CHECK(session.reservedBytes() == OutdoorSession::requiredBytes(all) && provider.live_bytes == session.reservedBytes());
        CHECK(session.package()->isOpen() && session.graph()->info().nodes > 0);
        ElevationSample sample;
        Result result;
        do
        {
            result = session.terrain()->elevation({0, 0}, sample);
        } while (result == Result::Busy);
        CHECK(result == Result::Ok && sample.meters == 100);
        CHECK(session.begin(file, all) == Result::Invalid && session.package());
        session.cancel();
        session.cancel();
        CHECK(session.initializeStep(1) == Result::Cancelled && provider.live == 0 && provider.live_bytes == 0);
    }
    Provider provider;
    OutdoorSession session(provider);
    CHECK(session.begin(file, {true, true, 1}) == Result::BudgetExceeded && provider.calls == 0);
    CHECK(session.begin(file, all) == Result::More);
    CHECK(session.initializeStep(1) == Result::More && provider.live == 1 && !session.package());
    session.cancel();
    CHECK(provider.live == 0);
    CHECK(file.native.open((directory + "/dense-contours.tmap").c_str()));
    CHECK(session.begin(file, all) == Result::More);
    CHECK(initialize(session) == Result::Missing && provider.live == 0);
    CHECK(session.begin(file) == Result::More && initialize(session) == Result::Ok);
    CHECK(session.package() && !session.graph() && !session.terrain());
    session.reset();
    session.reset();
    CHECK(provider.live == 0);
    CHECK(file.native.open((directory + "/bad-directory.tmap").c_str()));
    CHECK(session.begin(file) == Result::More);
    CHECK(initialize(session) == Result::Invalid && provider.live == 0);
    CHECK(file.native.open((directory + "/chain.tmap").c_str()));
    CHECK(session.begin(file, all) == Result::More && initialize(session) == Result::Ok);
    {
        CoordinateRouteStorage routes(provider);
        CHECK(routes.begin(128, 64) == Result::More);
        Result result = Result::More;
        size_t steps = 0;
        while (result == Result::More)
        {
            result = routes.initializeStep(7);
            CHECK(++steps < 10000);
        }
        CHECK(result == Result::Ok);
        MemoryObject<CoordinateRouteCursor> route(provider);
        CHECK(route.create() == Result::Ok);
        const RouteWorkspace workspace{routes.labels(), routes.queue(), routes.states()};
        do
        {
            result = beginCoordinateRoute(*session.graph(), {0, 2500}, {0, 17500}, {}, 20,
                                          workspace, routes.output(), routes.outputs(), *route.get());
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok);
        do
        {
            result = coordinateRouteStep(*session.graph(), *route.get(), 7);
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok && route.get()->count == 2 && route.get()->cost > 0);
        MemoryBuffer<GeoPoint> points(provider);
        CHECK(points.reserve(64) == Result::Ok);
        struct PointSink
        {
            GeoPoint* data;
            size_t count = 0;
        } sink{points.data()};
        MemoryObject<LegGeometryCursor> geometry(provider);
        CHECK(geometry.create() == Result::Ok);
        CHECK(beginLegGeometry(*session.graph(), routes.output(), route.get()->count, *geometry.get()) == Result::Ok);
        auto retain_point = [](void* context, GeoPoint point)
        {
            auto& sink = *static_cast<PointSink*>(context);
            if (sink.count == 64) return false;
            sink.data[sink.count++] = point;
            return true;
        };
        do
        {
            result = legGeometryStep(*session.graph(), *geometry.get(), 7, retain_point, &sink);
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok && sink.count >= 2);
        MemoryObject<Navigator> navigation(provider);
        MemoryObject<NavigationCursor> matching(provider);
        CHECK(navigation.create() == Result::Ok && matching.create() == Result::Ok);
        CHECK(navigation.get()->begin(points.data(), sink.count) == Result::Ok);
        CHECK(navigation.get()->beginUpdate(points.data()[sink.count - 1], 1, *matching.get()) == Result::Ok);
        NavigationState state;
        do
        {
            result = navigation.get()->updateStep(*matching.get(), 1, state);
            CHECK(++steps < 10000);
        } while (result == Result::More);
        CHECK(result == Result::Ok && state.arrived);
        StagedMemoryBuffer<ContourSegment> segments(provider);
        CHECK(segments.beginReserve(512) == Result::More);
        do
        {
            result = segments.initializeStep(7);
            CHECK(++steps < 10000);
        } while (result == Result::More);
        CHECK(result == Result::Ok);
        MemoryObject<ContourRenderCursor> render(provider);
        CHECK(render.create() == Result::Ok);
        CHECK(beginContourRender(*session.package(), {{10000, 10000}, 16, 320, 240}, segments.data(), 512, {}, *render.get()) == Result::Ok);
        do
        {
            result = contourRenderStep(*session.package(), *render.get(), 7);
            CHECK(++steps < 10000);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok && render.get()->count > 0);
        CHECK(provider.live_bytes == session.reservedBytes() + routes.reservedBytes() + route.bytes() + segments.bytes() + render.bytes() +
                                         points.bytes() + geometry.bytes() + navigation.bytes() + matching.bytes());
        cancel(*matching.get());
        cancel(*geometry.get());
        matching.reset();
        navigation.reset();
        geometry.reset();
        points.reset();
        cancel(*render.get());
        cancel(*route.get());
        CHECK(render.get()->count == 0 && route.get()->count == 0);
        render.reset();
        segments.reset();
        route.reset();
        routes.reset();
    }
    CHECK(provider.live == 4 && provider.live_bytes == session.reservedBytes());
    session.reset();
    CHECK(provider.live == 0 && provider.live_bytes == 0);
    std::printf("PASS Busy/open/features, four allocation rollback points, cancel/restart, preflight, missing feature, corrupt package; owner=%zu payload=%zu\n", sizeof(OutdoorSession), OutdoorSession::requiredBytes(all));
}
