#include "tmap/native_file.h"
#include "tmap/profile_job.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x)                                                 \
    do                                                           \
    {                                                            \
        if (!(x))                                                \
        {                                                        \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
            std::exit(1);                                        \
        }                                                        \
    } while (false)
class Provider : public tmap::MemoryProvider
{
  public:
    size_t calls = 0, fail_at = 0, bytes = 0;
    bool external = true;
    bool hasExternalMemory() const override { return external; }
    void* allocate(size_t count, size_t alignment, tmap::MemoryRegion region) override
    {
        CHECK(region == (external ? tmap::MemoryRegion::External : tmap::MemoryRegion::Internal));
        CHECK(alignment <= alignof(std::max_align_t));
        if (++calls == fail_at) return nullptr;
        auto* result = std::malloc(count);
        CHECK(result);
        bytes += count;
        return result;
    }
    void release(void* pointer, size_t count, size_t, tmap::MemoryRegion) override
    {
        CHECK(bytes >= count);
        bytes -= count;
        std::free(pointer);
    }
};
class File : public tmap::RandomAccessFile
{
  public:
    tmap::NativeFile native;
    size_t calls = 0;
    uint64_t size() const override { return native.size(); }
    tmap::Status readAt(uint64_t at, uint8_t* output, size_t bytes) override
    {
        return ++calls % 2 == 0 ? tmap::Status::Busy : native.readAt(at, output, bytes);
    }
};
static tmap::Result finish(tmap::ProfileJob& job)
{
    tmap::Result result;
    size_t iterations = 0;
    do
    {
        result = job.step(1);
        CHECK(++iterations < 100000);
    } while (result == tmap::Result::Busy || result == tmap::Result::More);
    return result;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    File file;
    CHECK(file.native.open((std::string(argv[1]) + "/outdoor.tmap").c_str()));
    const tmap::GeoPoint points[] = {{0, 0}, {20000, 20000}};
    tmap::ProfileJobOptions options;
    options.sampling = {30, 0};
    options.columns = 16;
    Provider provider;
    tmap::ProfileJob job(provider);
    CHECK(job.begin(file, points, 2, options) == tmap::Result::More);
    CHECK(finish(job) == tmap::Result::Ok);
    CHECK(job.metrics() && job.metrics()->ascent_m > 79.99 && job.metrics()->ascent_m < 80.01);
    CHECK(job.columns()[0].valid && job.columns()[15].valid);
    CHECK(job.reservedBytes() == provider.bytes && provider.bytes == tmap::ProfileJob::requiredBytes(16));
    CHECK(job.step(1) == tmap::Result::Ok);
    std::printf("PROFILE_JOB bytes=%zu columns=16 busy=1 ascent=%.2f samples=%zu\n", provider.bytes, job.metrics()->ascent_m, job.metrics()->samples);
    job.cancel();
    CHECK(provider.bytes == 0 && !job.columns() && !job.metrics());
    CHECK(job.step(1) == tmap::Result::Cancelled);
    for (size_t fail = 1; fail <= 5; fail++)
    {
        provider.calls = 0;
        provider.fail_at = fail;
        auto result = job.begin(file, points, 2, options);
        if (result == tmap::Result::More) result = finish(job);
        CHECK(result == tmap::Result::BudgetExceeded && !provider.bytes);
    }
    provider.fail_at = 0;
    options.maximum_bytes = 1;
    CHECK(job.begin(file, points, 2, options) == tmap::Result::BudgetExceeded && !provider.bytes);
    options.maximum_bytes = SIZE_MAX;
    options.maximum_samples = 2;
    CHECK(job.begin(file, points, 2, options) == tmap::Result::More);
    CHECK(finish(job) == tmap::Result::BudgetExceeded && !provider.bytes);
    options.maximum_samples = 8192;
    for (size_t cut = 0; cut < 20; ++cut)
    {
        CHECK(job.begin(file, points, 2, options) == tmap::Result::More);
        for (size_t i = 0; i < cut; i++) job.step(1);
        job.cancel();
        CHECK(!provider.bytes);
    }
    provider.external = false;
    CHECK(job.begin(file, points, 2, options) == tmap::Result::More && finish(job) == tmap::Result::Ok);
    job.reset();
    CHECK(!provider.bytes);
    const tmap::GeoPoint there_and_back[] = {{0, 0}, {20000, 20000}, {0, 0}};
    CHECK(job.begin(file, there_and_back, 3, options) == tmap::Result::More && finish(job) == tmap::Result::Ok);
    CHECK(std::abs(job.metrics()->ascent_m - 80) < 1e-8 && std::abs(job.metrics()->descent_m - 80) < 1e-8);
    job.reset();
    CHECK(file.native.open((std::string(argv[1]) + "/missing.tmap").c_str()));
    CHECK(job.begin(file, points, 2, options) == tmap::Result::More && finish(job) == tmap::Result::Incomplete);
    CHECK(job.metrics()->gaps && job.step(1) == tmap::Result::Incomplete);
    job.reset();
    CHECK(!provider.bytes);
}
