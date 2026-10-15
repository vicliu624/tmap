#include "tmap/route_storage.h"
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
    size_t calls = 0, releases = 0, fail_at = 0, live = 0;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t alignment, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External && alignment <= alignof(std::max_align_t));
        if (++calls == fail_at) return nullptr;
        void* pointer = std::malloc(bytes);
        CHECK(pointer);
        ++live;
        return pointer;
    }
    void release(void* pointer, size_t, size_t, MemoryRegion region) override
    {
        CHECK(region == MemoryRegion::External && live);
        --live;
        ++releases;
        std::free(pointer);
    }
};
int main()
{
    size_t cancellation_points = 0;
    // Cancel after every possible one-work-unit initialization boundary,
    // including a fully initialized owner, then reuse the same owner.
    for (const bool queue_free : {false, true})
    {
        bool reached_complete = false;
        for (size_t cut = 0; cut < 64 && !reached_complete; ++cut)
        {
            Provider sliced_provider;
            IntegerRouteStorage sliced(sliced_provider);
            const auto begin = [&]() { return queue_free ? sliced.beginLabelScan(8, 4) : sliced.begin(8, 4); };
            CHECK(begin() == Result::More);
            auto state = Result::More;
            for (size_t step = 0; step < cut && state == Result::More; ++step)
                state = sliced.initializeStep(1);
            CHECK(state == Result::More || state == Result::Ok);
            reached_complete = state == Result::Ok;
            if (!reached_complete) CHECK(!sliced.labels() && !sliced.output() && !sliced.queue());
            sliced.cancel();
            CHECK(!sliced_provider.live && sliced_provider.calls == sliced_provider.releases);
            CHECK(!sliced.states() && !sliced.outputs() && !sliced.requiredBytes() && !sliced.reservedBytes());
            CHECK(!sliced.labels() && !sliced.output() && !sliced.queue());
            CHECK(sliced.initializeStep(1) == Result::Cancelled);
            CHECK(begin() == Result::More && sliced.initializeStep(100) == Result::Ok);
            CHECK(sliced.labels() && sliced.output() && (bool(sliced.queue()) != queue_free));
            sliced.reset();
            CHECK(!sliced_provider.live && sliced_provider.calls == sliced_provider.releases);
            ++cancellation_points;
        }
        CHECK(reached_complete);
    }
    bool alt_complete = false;
    for (size_t cut = 0; cut < 32 && !alt_complete; ++cut)
    {
        Provider sliced_provider;
        IntegerLandmarkStorage sliced(sliced_provider);
        CHECK(sliced.begin(8) == Result::More);
        auto state = Result::More;
        for (size_t step = 0; step < cut && state == Result::More; ++step)
            state = sliced.initializeStep(1);
        CHECK(state == Result::More || state == Result::Ok);
        alt_complete = state == Result::Ok;
        if (!alt_complete) CHECK(!sliced.workspace() && !sliced.lowerBounds());
        sliced.cancel();
        CHECK(!sliced_provider.live && sliced_provider.calls == sliced_provider.releases);
        CHECK(!sliced.reservedBytes() && !sliced.workspace() && !sliced.lowerBounds());
        CHECK(sliced.initializeStep(1) == Result::Cancelled);
        CHECK(sliced.begin(8) == Result::More && sliced.initializeStep(100) == Result::Ok);
        sliced.reset();
        CHECK(!sliced_provider.live && sliced_provider.calls == sliced_provider.releases);
        ++cancellation_points;
    }
    CHECK(alt_complete);
    std::printf("CANCEL_SLICES checked=%zu queued=1 label_scan=1 landmarks=1 retry_each=1 external_only=1\n", cancellation_points);
    for (size_t fail = 0; fail <= 2; ++fail)
    {
        Provider label_provider;
        label_provider.fail_at = fail;
        IntegerRouteStorage labels_only(label_provider);
        const size_t payload = 8 * sizeof(IntegerRouteLabel) + 4 * sizeof(uint32_t);
        CHECK(labels_only.beginLabelScan(8, 4, payload - 1) == Result::BudgetExceeded && !label_provider.calls);
        CHECK(labels_only.beginLabelScan(8, 4, payload) == Result::More);
        Result result;
        do
        {
            result = labels_only.initializeStep(1);
        } while (result == Result::More);
        CHECK(result == (fail ? Result::BudgetExceeded : Result::Ok));
        if (!fail)
        {
            CHECK(label_provider.calls == 2 && label_provider.live == 2 && !labels_only.queue());
            CHECK(labels_only.labels()[7].queue_position == UINT32_MAX && labels_only.output()[3] == 0);
            CHECK(labels_only.reservedBytes() == payload && labels_only.requiredBytes() == payload);
        }
        else CHECK(!label_provider.live && label_provider.releases == fail - 1 && !labels_only.reservedBytes());
        labels_only.cancel();
        CHECK(!label_provider.live);
    }
    for (size_t fail = 1; fail <= 2; ++fail)
    {
        Provider alt_provider;
        alt_provider.fail_at = fail;
        IntegerLandmarkStorage alt(alt_provider);
        CHECK(alt.begin(8, 1) == Result::BudgetExceeded && alt_provider.calls == 0);
        CHECK(alt.begin(8) == Result::More);
        Result result;
        do
        {
            result = alt.initializeStep(1);
            if (result == Result::More) CHECK(!alt.workspace() && !alt.lowerBounds());
        } while (result == Result::More);
        CHECK(result == Result::BudgetExceeded && alt_provider.live == 0);
        alt_provider.fail_at = 0;
        CHECK(alt.begin(8) == Result::More && alt.initializeStep(100) == Result::Ok);
        CHECK(alt.workspace()->stage == 0 && alt.lowerBounds()[7] == 0);
        CHECK(alt.reservedBytes() == 32 + sizeof(IntegerLandmarkWorkspace));
        alt.cancel();
        CHECK(alt_provider.live == 0 && alt.initializeStep(1) == Result::Cancelled);
    }
    for (size_t fail = 1; fail <= 3; ++fail)
    {
        Provider provider;
        provider.fail_at = fail;
        IntegerRouteStorage storage(provider);
        CHECK(storage.begin(8, 4) == Result::More && !storage.reservedBytes());
        Result result = Result::More;
        size_t steps = 0;
        while (result == Result::More)
        {
            result = storage.initializeStep(1);
            CHECK(++steps < 100);
        }
        CHECK(result == Result::BudgetExceeded && provider.live == 0 && provider.releases == fail - 1);
        CHECK(!storage.labels() && !storage.output() && !storage.reservedBytes());
        CHECK(storage.initializeStep(100) == Result::BudgetExceeded);
        provider.fail_at = 0;
        CHECK(storage.begin(8, 4) == Result::More);
        CHECK(storage.initializeStep(100) == Result::Ok);
        CHECK(storage.labels()[7].key == 0 && storage.queue()[7] == 0 && storage.output()[3] == 0);
        CHECK(storage.requiredBytes() == 8 * (sizeof(IntegerRouteLabel) + 4) + 16);
        CHECK(storage.begin(2, 1) == Result::Invalid && storage.labels());
        storage.cancel();
        storage.cancel();
        CHECK(provider.live == 0 && storage.initializeStep(1) == Result::Cancelled);
    }
    Provider provider;
    CoordinateRouteStorage storage(provider);
    CHECK(storage.begin(8, 4, 1) == Result::BudgetExceeded && provider.calls == 0);
    CHECK(storage.begin(SIZE_MAX, 1) == Result::Invalid && provider.calls == 0);
    CHECK(storage.begin(8, 4) == Result::More);
    CHECK(storage.initializeStep(0) == Result::More && provider.calls == 0);
    CHECK(storage.initializeStep(1) == Result::More && provider.calls == 1 && !storage.labels());
    storage.cancel();
    CHECK(provider.live == 0 && provider.releases == 1);
    CHECK(storage.begin(8, 4) == Result::More && storage.initializeStep(100) == Result::Ok);
    CHECK(storage.output()[3].edge == UINT32_MAX);
    storage.reset();
    storage.reset();
    CHECK(provider.live == 0);
    std::printf("LABEL_SCAN_STORAGE owner=%zu queue_free_bytes=%zu queued_bytes=%zu external_allocations=2 rollback_points=2\n", sizeof(IntegerRouteStorage), 8 * sizeof(IntegerRouteLabel) + 4 * sizeof(uint32_t), 8 * (sizeof(IntegerRouteLabel) + sizeof(uint32_t)) + 4 * sizeof(uint32_t));
    std::printf("PASS all three allocation failure rollback points, retry, shared slice budget, hidden partial arrays, preflight limit, cancel/reset\n");
}
