#pragma once
#include "tmap/terrain.h"
#include <cmath>

namespace tmap
{
// Borrowed, fixed-capacity display columns. Statistics come from ProfileMetrics,
// never from these reduced samples. A column containing a gap is disconnected
// conservatively; the renderer must not draw through unknown terrain.
struct ProfileColumn
{
    double first_m = 0, last_m = 0, minimum_m = 0, maximum_m = 0;
    bool valid = false, gap = false, break_before = false;
};
class ProfilePlot
{
  public:
    Result begin(ProfileColumn* columns, size_t count, double distance_m, size_t maximum_samples)
    {
        columns_ = nullptr;
        terminal_ = Result::Invalid;
        if (!columns || !count || !maximum_samples || !std::isfinite(distance_m) || distance_m < 0)
            return Result::Invalid;
        for (size_t i = 0; i < count; ++i) columns[i] = ProfileColumn{};
        columns_ = columns;
        count_ = count;
        distance_m_ = distance_m;
        maximum_samples_ = maximum_samples;
        samples_ = 0;
        previous_distance_m_ = 0;
        terminal_ = Result::Ok;
        return Result::Ok;
    }
    bool append(const ProfileSample& sample)
    {
        if (!columns_ || terminal_ != Result::Ok) return false;
        if (samples_ == maximum_samples_)
        {
            terminal_ = Result::BudgetExceeded;
            return false;
        }
        if (!std::isfinite(sample.distance_m) || sample.distance_m < previous_distance_m_ ||
            sample.distance_m < 0 || sample.distance_m > distance_m_ ||
            (sample.valid && !std::isfinite(sample.elevation_m)))
        {
            terminal_ = Result::Invalid;
            return false;
        }
        const double fraction = distance_m_ > 0 ? sample.distance_m / distance_m_ : 0;
        const size_t index = fraction >= 1 ? count_ - 1 : size_t(fraction * double(count_ - 1));
        auto& column = columns_[index];
        column.break_before = column.break_before || sample.break_before;
        if (!sample.valid) column.gap = true;
        else
        {
            if (!column.valid)
            {
                column.first_m = column.minimum_m = column.maximum_m = sample.elevation_m;
                column.valid = true;
            }
            if (sample.elevation_m < column.minimum_m) column.minimum_m = sample.elevation_m;
            if (sample.elevation_m > column.maximum_m) column.maximum_m = sample.elevation_m;
            column.last_m = sample.elevation_m;
        }
        previous_distance_m_ = sample.distance_m;
        ++samples_;
        return true;
    }
    static bool visit(void* context, const ProfileSample& sample)
    {
        return context && static_cast<ProfilePlot*>(context)->append(sample);
    }
    Result result() const { return terminal_; }
    size_t samples() const { return samples_; }

  private:
    ProfileColumn* columns_ = nullptr;
    size_t count_ = 0, maximum_samples_ = 0, samples_ = 0;
    double distance_m_ = 0, previous_distance_m_ = 0;
    Result terminal_ = Result::Invalid;
};
} // namespace tmap
