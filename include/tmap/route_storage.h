#pragma once
#include "tmap/coordinate_route.h"
#include "tmap/integer_prepared_route.h"
#include "tmap/integer_route.h"
#include "tmap/staged_memory.h"
#include <type_traits>
namespace tmap
{
// Optional explicit owner for the three caller arrays. One shared element/work
// budget covers reservations and construction; any failure rolls all arrays back.
template <class Label, class Output>
class RouteStorage
{
  public:
    explicit RouteStorage(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : labels_(provider, policy), queue_(provider, policy), output_(provider, policy) {}
    RouteStorage(const RouteStorage&) = delete;
    RouteStorage& operator=(const RouteStorage&) = delete;
    Result begin(size_t states, size_t outputs, size_t max_payload_bytes = SIZE_MAX)
    {
        return beginArrays(states, outputs, max_payload_bytes, true);
    }
    Result beginLabelScan(size_t states, size_t outputs, size_t max_payload_bytes = SIZE_MAX)
    {
        static_assert(std::is_same<Label, IntegerRouteLabel>::value, "Queue-free ownership is for integer label-scan routes");
        return beginArrays(states, outputs, max_payload_bytes, false);
    }

  private:
    Result beginArrays(size_t states, size_t outputs, size_t max_payload_bytes, bool queue_required)
    {
        if (terminal_ == Result::More || terminal_ == Result::Ok) return Result::Invalid;
        const size_t width = sizeof(Label) + (queue_required ? sizeof(uint32_t) : 0);
        if (states < 2 || states > UINT32_MAX || !outputs || outputs > UINT32_MAX || states > SIZE_MAX / width)
            return terminal_ = Result::Invalid;
        const auto bytes = states * width;
        if (outputs > (SIZE_MAX - bytes) / sizeof(Output)) return terminal_ = Result::Invalid;
        required_ = bytes + outputs * sizeof(Output);
        if (required_ > max_payload_bytes) return terminal_ = Result::BudgetExceeded;
        states_ = states;
        outputs_ = outputs;
        queue_required_ = queue_required;
        stage_ = 0;
        return terminal_ = Result::More;
    }

  public:
    Result initializeStep(size_t work_budget)
    {
        if (terminal_ != Result::More) return terminal_;
        while (work_budget && stage_ < 6)
        {
            if (stage_ == 2 && !queue_required_)
            {
                stage_ = 4;
                continue;
            }
            Result result = Result::Invalid;
            if (stage_ == 0) result = labels_.beginReserve(states_);
            else if (stage_ == 1) result = labels_.initializeStep(1);
            else if (stage_ == 2) result = queue_.beginReserve(states_);
            else if (stage_ == 3) result = queue_.initializeStep(1);
            else if (stage_ == 4) result = output_.beginReserve(outputs_);
            else result = output_.initializeStep(1);
            --work_budget;
            if (result != Result::More && result != Result::Ok)
            {
                reset();
                return terminal_ = result;
            }
            if (!(stage_ & 1) || result == Result::Ok) ++stage_;
        }
        if (stage_ == 6) terminal_ = Result::Ok;
        return terminal_;
    }
    void reset()
    {
        output_.reset();
        queue_.reset();
        labels_.reset();
        states_ = outputs_ = required_ = 0;
        stage_ = 0;
        queue_required_ = true;
        terminal_ = Result::Invalid;
    }
    void cancel()
    {
        reset();
        terminal_ = Result::Cancelled;
    }
    Label* labels() const { return terminal_ == Result::Ok ? labels_.data() : nullptr; }
    uint32_t* queue() const { return terminal_ == Result::Ok ? queue_.data() : nullptr; }
    Output* output() const { return terminal_ == Result::Ok ? output_.data() : nullptr; }
    size_t states() const { return states_; }
    size_t outputs() const { return outputs_; }
    size_t requiredBytes() const { return required_; }
    size_t reservedBytes() const { return labels_.bytes() + queue_.bytes() + output_.bytes(); }

  private:
    StagedMemoryBuffer<Label> labels_;
    StagedMemoryBuffer<uint32_t> queue_;
    StagedMemoryBuffer<Output> output_;
    size_t states_ = 0, outputs_ = 0, required_ = 0;
    uint8_t stage_ = 0;
    bool queue_required_ = true;
    Result terminal_ = Result::Invalid;
};
using CoordinateRouteStorage = RouteStorage<RouteLabel, RouteLeg>;
using IntegerRouteStorage = RouteStorage<IntegerRouteLabel, uint32_t>;
using IntegerPreparedRouteStorage = RouteStorage<IntegerRouteLabel, IntegerRouteLeg>;
// Optional ALT owner; absent unless explicitly constructed/reserved. Stop all
// borrowers before reset/cancel. Provider policy prefers PSRAM without fallback.
class IntegerLandmarkStorage
{
  public:
    explicit IntegerLandmarkStorage(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : bounds_(provider, policy), workspace_(provider, policy) {}
    Result begin(size_t states, size_t max_payload_bytes = SIZE_MAX)
    {
        if (terminal_ == Result::Ok || terminal_ == Result::More) return Result::Invalid;
        if (states < 2 || states > UINT32_MAX || states > (SIZE_MAX - sizeof(IntegerLandmarkWorkspace)) / sizeof(uint32_t))
            return terminal_ = Result::Invalid;
        required_ = states * sizeof(uint32_t) + sizeof(IntegerLandmarkWorkspace);
        if (required_ > max_payload_bytes) return terminal_ = Result::BudgetExceeded;
        states_ = states;
        stage_ = 0;
        return terminal_ = Result::More;
    }
    Result initializeStep(size_t budget)
    {
        if (terminal_ != Result::More) return terminal_;
        while (budget && stage_ < 4)
        {
            const auto result = stage_ == 0 ? bounds_.beginReserve(states_) : stage_ == 1 ? bounds_.initializeStep(1)
                                                                          : stage_ == 2   ? workspace_.beginReserve(1)
                                                                                          : workspace_.initializeStep(1);
            --budget;
            if (result != Result::Ok && result != Result::More)
            {
                reset();
                return terminal_ = result;
            }
            if (!(stage_ & 1) || result == Result::Ok) ++stage_;
        }
        if (stage_ == 4) terminal_ = Result::Ok;
        return terminal_;
    }
    void reset()
    {
        workspace_.reset();
        bounds_.reset();
        states_ = required_ = 0;
        stage_ = 0;
        terminal_ = Result::Invalid;
    }
    void cancel()
    {
        reset();
        terminal_ = Result::Cancelled;
    }
    uint32_t* lowerBounds() const { return terminal_ == Result::Ok ? bounds_.data() : nullptr; }
    IntegerLandmarkWorkspace* workspace() const { return terminal_ == Result::Ok ? workspace_.data() : nullptr; }
    size_t requiredBytes() const { return required_; }
    size_t reservedBytes() const { return bounds_.bytes() + workspace_.bytes(); }

  private:
    StagedMemoryBuffer<uint32_t> bounds_;
    StagedMemoryBuffer<IntegerLandmarkWorkspace> workspace_;
    size_t states_ = 0, required_ = 0;
    uint8_t stage_ = 0;
    Result terminal_ = Result::Invalid;
};
} // namespace tmap
