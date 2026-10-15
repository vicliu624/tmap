#pragma once
#include "tmap/package.h"
#include <limits>
#include <new>
#include <type_traits>

namespace tmap
{
// Platform adapters map External to PSRAM (or their external RAM equivalent).
enum class MemoryRegion : uint8_t
{
    Internal,
    External
};
enum class MemoryPolicy : uint8_t
{
    PreferExternal, // External present: never silently fall back after failure.
    RequireExternal,
    InternalOnly
};
class MemoryProvider
{
  public:
    virtual ~MemoryProvider() = default;
    virtual bool hasExternalMemory() const = 0;
    virtual void* allocate(size_t bytes, size_t alignment, MemoryRegion region) = 0;
    virtual void release(void* pointer, size_t bytes, size_t alignment, MemoryRegion region) = 0;
};
// Optional explicit allocation convenience. Core readers still accept borrowed
// buffers and never invoke this helper themselves. Provider outlives the buffer.
template <class T>
class MemoryBuffer
{
    static_assert(std::is_trivially_destructible<T>::value, "Buffers require trivial destruction");

  public:
    explicit MemoryBuffer(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : provider_(provider), policy_(policy) {}
    ~MemoryBuffer() { reset(); }
    MemoryBuffer(const MemoryBuffer&) = delete;
    MemoryBuffer& operator=(const MemoryBuffer&) = delete;
    Result reserve(size_t count)
    {
        if (data_ || !count || count > std::numeric_limits<size_t>::max() / sizeof(T)) return Result::Invalid;
        const bool external = provider_.hasExternalMemory();
        if (policy_ == MemoryPolicy::RequireExternal && !external) return Result::Missing;
        const auto region = policy_ != MemoryPolicy::InternalOnly && external ? MemoryRegion::External : MemoryRegion::Internal;
        const auto bytes = count * sizeof(T);
        void* pointer = provider_.allocate(bytes, alignof(T), region);
        if (!pointer) return Result::BudgetExceeded;
        if (reinterpret_cast<uintptr_t>(pointer) % alignof(T))
        {
            provider_.release(pointer, bytes, alignof(T), region);
            return Result::Invalid;
        }
        data_ = static_cast<T*>(pointer);
        count_ = count;
        region_ = region;
        for (size_t i = 0; i < count; ++i) ::new (static_cast<void*>(data_ + i)) T{};
        return Result::Ok;
    }
    void reset()
    {
        if (data_) provider_.release(data_, count_ * sizeof(T), alignof(T), region_);
        data_ = nullptr;
        count_ = 0;
    }
    T* data() const { return data_; }
    size_t count() const { return count_; }
    size_t bytes() const { return count_ * sizeof(T); }
    MemoryRegion region() const { return region_; }

  private:
    MemoryProvider& provider_;
    MemoryPolicy policy_;
    MemoryRegion region_ = MemoryRegion::Internal;
    T* data_ = nullptr;
    size_t count_ = 0;
};
} // namespace tmap
