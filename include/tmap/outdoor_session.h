#pragma once
#include "tmap/memory_object.h"
#include "tmap/navigation.h"
namespace tmap
{
struct SessionOptions
{
    bool require_graph = false, require_terrain = false;
    size_t max_object_bytes = SIZE_MAX;
};
// Optional explicit owner. The provider and externally opened file outlive the
// session. Core algorithms remain usable independently with borrowed storage.
class OutdoorSession
{
  public:
    explicit OutdoorSession(MemoryProvider& provider, MemoryPolicy policy = MemoryPolicy::PreferExternal)
        : workspace_(provider, policy), package_(provider, policy), graph_(provider, policy), terrain_(provider, policy) {}
    ~OutdoorSession() { reset(); }
    OutdoorSession(const OutdoorSession&) = delete;
    OutdoorSession& operator=(const OutdoorSession&) = delete;
    Result begin(RandomAccessFile& file, SessionOptions options = {});
    Result initializeStep(size_t work_budget);
    void reset();
    void cancel();
    OutdoorPackage* package() const { return terminal_ == Result::Ok ? package_.get() : nullptr; }
    GraphReader* graph() const { return terminal_ == Result::Ok ? graph_.get() : nullptr; }
    TerrainReader* terrain() const { return terminal_ == Result::Ok ? terrain_.get() : nullptr; }
    size_t reservedBytes() const { return workspace_.bytes() + package_.bytes() + graph_.bytes() + terrain_.bytes(); }
    static size_t requiredBytes(SessionOptions options)
    {
        return sizeof(PackageWorkspace) + sizeof(OutdoorPackage) +
               (options.require_graph ? sizeof(GraphReader) : 0) + (options.require_terrain ? sizeof(TerrainReader) : 0);
    }

  private:
    MemoryObject<PackageWorkspace> workspace_;
    MemoryObject<OutdoorPackage> package_;
    MemoryObject<GraphReader> graph_;
    MemoryObject<TerrainReader> terrain_;
    RandomAccessFile* file_ = nullptr;
    SessionOptions options_{};
    uint8_t stage_ = 0;
    Result terminal_ = Result::Invalid;
};
} // namespace tmap
