#pragma once
#include "tmap/memory.h"
#include <utility>

namespace tmap
{
// Explicit owner for readers with constructor dependencies. Declare dependencies
// first so destruction releases dependents before their borrowed storage/provider.
template <class T>
class MemoryObject
{
    static_assert(std::is_nothrow_destructible<T>::value, "Owned destructors must not throw");

  public:
    explicit MemoryObject(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : provider_(provider), policy_(policy) {}
    ~MemoryObject() { reset(); }
    MemoryObject(const MemoryObject&) = delete;
    MemoryObject& operator=(const MemoryObject&) = delete;
    template <class... Args>
    Result create(Args&&... args)
    {
        if (pointer_) return Result::Invalid;
        const bool external = provider_.hasExternalMemory();
        if (policy_ == MemoryPolicy::RequireExternal && !external) return Result::Missing;
        const auto region = policy_ != MemoryPolicy::InternalOnly && external ? MemoryRegion::External : MemoryRegion::Internal;
        void* raw = provider_.allocate(sizeof(T), alignof(T), region);
        if (!raw) return Result::BudgetExceeded;
        Guard guard{provider_, raw, region};
        if (reinterpret_cast<uintptr_t>(raw) % alignof(T)) return Result::Invalid;
        // Placement construction: no temporary T on the task stack. Guard also
        // releases on constructor exceptions in exception-enabled host consumers.
        T* constructed = ::new (raw) T(std::forward<Args>(args)...);
        pointer_ = constructed;
        region_ = region;
        guard.raw = nullptr;
        return Result::Ok;
    }
    void reset()
    {
        if (!pointer_) return;
        pointer_->~T();
        provider_.release(pointer_, sizeof(T), alignof(T), region_);
        pointer_ = nullptr;
    }
    T* get() const { return pointer_; }
    size_t bytes() const { return pointer_ ? sizeof(T) : 0; }
    MemoryRegion region() const { return region_; }

  private:
    struct Guard
    {
        MemoryProvider& provider;
        void* raw;
        MemoryRegion region;
        ~Guard()
        {
            if (raw) provider.release(raw, sizeof(T), alignof(T), region);
        }
    };
    MemoryProvider& provider_;
    MemoryPolicy policy_;
    MemoryRegion region_ = MemoryRegion::Internal;
    T* pointer_ = nullptr;
};
} // namespace tmap
