#pragma once
#include "tmap/memory.h"

namespace tmap
{
// Explicit allocation followed by bounded construction slices. Provider calls
// themselves are platform operations; their latency is not bounded by this API.
template <class T>
class StagedMemoryBuffer
{
    static_assert(std::is_trivially_destructible<T>::value, "Partial buffers require trivial destruction");
    static_assert(std::is_nothrow_default_constructible<T>::value, "Initialization must not throw");

  public:
    explicit StagedMemoryBuffer(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : provider_(provider), policy_(policy) {}
    ~StagedMemoryBuffer() { reset(); }
    StagedMemoryBuffer(const StagedMemoryBuffer&) = delete;
    StagedMemoryBuffer& operator=(const StagedMemoryBuffer&) = delete;
    Result beginReserve(size_t count)
    {
        if (pointer_) return Result::Invalid;
        terminal_ = Result::Invalid;
        if (!count || count > std::numeric_limits<size_t>::max() / sizeof(T)) return terminal_;
        const bool external = provider_.hasExternalMemory();
        if (policy_ == MemoryPolicy::RequireExternal && !external) return terminal_ = Result::Missing;
        const auto region = policy_ != MemoryPolicy::InternalOnly && external ? MemoryRegion::External : MemoryRegion::Internal;
        void* raw = provider_.allocate(count * sizeof(T), alignof(T), region);
        if (!raw) return terminal_ = Result::BudgetExceeded;
        if (reinterpret_cast<uintptr_t>(raw) % alignof(T))
        {
            provider_.release(raw, count * sizeof(T), alignof(T), region);
            return terminal_;
        }
        pointer_ = static_cast<T*>(raw);
        count_ = count;
        initialized_ = 0;
        region_ = region;
        return terminal_ = Result::More;
    }
    Result initializeStep(size_t element_budget)
    {
        if (terminal_ != Result::More) return terminal_;
        while (element_budget && initialized_ < count_)
        {
            ::new (static_cast<void*>(pointer_ + initialized_)) T{};
            ++initialized_;
            --element_budget;
        }
        return terminal_ = initialized_ == count_ ? Result::Ok : Result::More;
    }
    void reset()
    {
        if (pointer_) provider_.release(pointer_, count_ * sizeof(T), alignof(T), region_);
        pointer_ = nullptr;
        count_ = initialized_ = 0;
        terminal_ = Result::Invalid;
    }
    void cancel()
    {
        reset();
        terminal_ = Result::Cancelled;
    }
    // A partially constructed reservation is deliberately not a usable array.
    T* data() const { return terminal_ == Result::Ok ? pointer_ : nullptr; }
    size_t count() const { return count_; }
    size_t initialized() const { return initialized_; }
    size_t bytes() const { return count_ * sizeof(T); }
    MemoryRegion region() const { return region_; }

  private:
    MemoryProvider& provider_;
    MemoryPolicy policy_;
    MemoryRegion region_ = MemoryRegion::Internal;
    Result terminal_ = Result::Invalid;
    T* pointer_ = nullptr;
    size_t count_ = 0, initialized_ = 0;
};
} // namespace tmap
