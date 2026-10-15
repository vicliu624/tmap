#include "tmap/native_file.h"
#include "tmap/profile_job.h"
#include <cstdio>
#include <cstdlib>

class Provider final : public tmap::MemoryProvider
{
  public:
    size_t live = 0, peak = 0, allocations = 0;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t, tmap::MemoryRegion region) override
    {
        if (region != tmap::MemoryRegion::External) std::abort();
        void* result = std::malloc(bytes);
        if (result)
        {
            live += bytes;
            if (live > peak) peak = live;
            ++allocations;
        }
        return result;
    }
    void release(void* pointer, size_t bytes, size_t, tmap::MemoryRegion region) override
    {
        if (region != tmap::MemoryRegion::External || live < bytes) std::abort();
        live -= bytes;
        std::free(pointer);
    }
};
class File final : public tmap::RandomAccessFile
{
  public:
    tmap::NativeFile native;
    size_t attempts = 0, reads = 0, bytes = 0, busy_every = 0;
    uint64_t size() const override { return native.size(); }
    tmap::Status readAt(uint64_t at, uint8_t* out, size_t count) override
    {
        ++attempts;
        if (busy_every && attempts % busy_every == 0) return tmap::Status::Busy;
        ++reads;
        bytes += count;
        return native.readAt(at, out, count);
    }
};
int main(int argc, char** argv)
{
    if (argc != 10) return 2;
    File file;
    if (!file.native.open(argv[1])) return 3;
    tmap::GeoPoint points[2]{{int32_t(std::strtol(argv[2], nullptr, 10)), int32_t(std::strtol(argv[3], nullptr, 10))},
                             {int32_t(std::strtol(argv[4], nullptr, 10)), int32_t(std::strtol(argv[5], nullptr, 10))}};
    tmap::ProfileJobOptions options;
    options.sampling.spacing_m = std::strtod(argv[6], nullptr);
    options.columns = size_t(std::strtoul(argv[7], nullptr, 10));
    options.maximum_samples = size_t(std::strtoul(argv[8], nullptr, 10));
    file.busy_every = size_t(std::strtoul(argv[9], nullptr, 10));
    Provider provider;
    tmap::ProfileJob job(provider);
    auto result = job.begin(file, points, 2, options);
    size_t steps = 0;
    while ((result == tmap::Result::More || result == tmap::Result::Busy) && ++steps < 10000000) result = job.step(1);
    const auto* metrics = job.metrics();
    std::printf("{\"result\":%u,\"distance_m\":%.9f,\"ascent_m\":%.9f,\"descent_m\":%.9f,"
                "\"minimum_m\":%.9f,\"maximum_m\":%.9f,\"valid_distance_m\":%.9f,\"samples\":%zu,\"gaps\":%zu,"
                "\"owner_bytes\":%zu,\"provider_peak_bytes\":%zu,\"allocations\":%zu,\"steps\":%zu,"
                "\"read_attempts\":%zu,\"physical_reads\":%zu,\"physical_bytes\":%zu,\"display_columns\":%zu",
                unsigned(result), metrics ? metrics->distance_m : 0, metrics ? metrics->ascent_m : 0,
                metrics ? metrics->descent_m : 0, metrics ? metrics->minimum_m : 0, metrics ? metrics->maximum_m : 0,
                metrics ? metrics->valid_distance_m : 0, metrics ? metrics->samples : 0, metrics ? metrics->gaps : 0,
                sizeof(job), provider.peak, provider.allocations, steps, file.attempts, file.reads, file.bytes, options.columns);
    job.reset();
    std::printf(",\"live_after_reset\":%zu}\n", provider.live);
    return result == tmap::Result::Ok || result == tmap::Result::Incomplete || result == tmap::Result::BudgetExceeded ? 0 : 1;
}
