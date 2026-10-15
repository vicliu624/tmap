#pragma once
#include "tmap/memory_object.h"
#include "tmap/profile_plot.h"

namespace tmap
{
struct ProfileJobOptions
{
    ProfileOptions sampling{};
    size_t columns = 96, maximum_samples = 8192, maximum_points = 1024;
    size_t maximum_bytes = SIZE_MAX;
};
// Cooperative DEM-to-display pipeline. File, provider and path outlive the job.
// Every retained large object and display column uses the provider; PSRAM is
// preferred and allocation failure never silently spills into internal RAM.
class ProfileJob
{
  public:
    explicit ProfileJob(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : workspace_(provider, policy), package_(provider, policy), terrain_(provider, policy),
          cursor_(provider, policy), columns_(provider, policy) {}
    Result begin(RandomAccessFile& file, const GeoPoint* points, size_t count, ProfileJobOptions options = {})
    {
        reset();
        if (!points || count < 2 || count > options.maximum_points || !options.columns ||
            options.columns > 4096 || options.maximum_samples < 2 ||
            !std::isfinite(options.sampling.spacing_m) || options.sampling.spacing_m <= 0 ||
            !std::isfinite(options.sampling.noise_threshold_m) || options.sampling.noise_threshold_m < 0)
            return terminal_ = Result::Invalid;
        const auto required = requiredBytes(options.columns);
        if (required > options.maximum_bytes) return terminal_ = Result::BudgetExceeded;
        double distance = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (points[i].latitude < -900000000 || points[i].latitude > 900000000 ||
                points[i].longitude < -1800000000 || points[i].longitude > 1800000000)
                return terminal_ = Result::Invalid;
            if (i)
            {
                if (std::abs(int64_t(points[i].longitude) - points[i - 1].longitude) > 1800000000)
                    return terminal_ = Result::Unsupported;
                distance += distanceMeters(points[i - 1], points[i]);
            }
        }
        auto result = cursor_.create();
        if (result == Result::Ok) result = columns_.reserve(options.columns);
        if (result == Result::Ok) result = plot_.begin(columns_.data(), options.columns, distance, options.maximum_samples);
        if (result != Result::Ok) return fail(result);
        points_ = points;
        count_ = count;
        options_ = options;
        file_ = &file;
        stage_ = 0;
        return terminal_ = Result::More;
    }
    Result step(size_t budget)
    {
        if (!budget) return Result::Invalid;
        if (terminal_ != Result::More && terminal_ != Result::Busy) return terminal_;
        if (stage_ < 5)
        {
            auto result = Result::Ok;
            switch (stage_)
            {
            case 0:
                result = workspace_.create();
                break;
            case 1:
                result = package_.create(*workspace_.get());
                break;
            case 2:
                result = package_.get()->open(*file_, 1);
                break;
            case 3:
                result = terrain_.create(*package_.get());
                break;
            case 4:
                result = terrain_.get()->initialize();
                break;
            }
            if (result == Result::Busy || result == Result::More) return terminal_ = result;
            if (result != Result::Ok) return fail(result);
            ++stage_;
            return terminal_ = Result::More;
        }
        if (stage_ == 5)
        {
            const auto result = beginProfile(*terrain_.get(), *package_.get(), points_, count_, options_.sampling, *cursor_.get());
            if (result == Result::Busy || result == Result::More) return terminal_ = result;
            if (result != Result::Ok) return fail(result);
            stage_ = 6;
            return terminal_ = Result::More;
        }
        const auto result = profileStep(*terrain_.get(), *package_.get(), *cursor_.get(), budget, ProfilePlot::visit, &plot_);
        if (plot_.result() != Result::Ok) return fail(plot_.result());
        return terminal_ = result;
    }
    void cancel()
    {
        reset();
        terminal_ = Result::Cancelled;
    }
    void reset()
    {
        terrain_.reset();
        package_.reset();
        workspace_.reset();
        cursor_.reset();
        columns_.reset();
        points_ = nullptr;
        file_ = nullptr;
        count_ = 0;
        terminal_ = Result::Invalid;
    }
    const ProfileMetrics* metrics() const { return cursor_.get() ? &cursor_.get()->metrics : nullptr; }
    const ProfileColumn* columns() const { return columns_.data(); }
    size_t columnCount() const { return columns_.count(); }
    size_t reservedBytes() const { return workspace_.bytes() + package_.bytes() + terrain_.bytes() + cursor_.bytes() + columns_.bytes(); }
    static size_t requiredBytes(size_t columns)
    {
        const size_t base = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(TerrainReader) + sizeof(ProfileCursor);
        return columns > (SIZE_MAX - base) / sizeof(ProfileColumn) ? SIZE_MAX : base + columns * sizeof(ProfileColumn);
    }

  private:
    Result fail(Result result)
    {
        reset();
        return terminal_ = result;
    }
    MemoryObject<PackageWorkspace> workspace_;
    MemoryObject<OutdoorPackage> package_;
    MemoryObject<TerrainReader> terrain_;
    MemoryObject<ProfileCursor> cursor_;
    MemoryBuffer<ProfileColumn> columns_;
    ProfilePlot plot_{};
    const GeoPoint* points_ = nullptr;
    RandomAccessFile* file_ = nullptr;
    size_t count_ = 0;
    ProfileJobOptions options_{};
    uint8_t stage_ = 0;
    Result terminal_ = Result::Invalid;
};
} // namespace tmap
