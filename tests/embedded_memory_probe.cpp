#include "tmap/contour_renderer.h"
#include "tmap/coordinate_route.h"
#include "tmap/memory.h"
// Compile-only probes instantiate the convenience layer on real MCU toolchains.
// Provider owns allocation; these wrappers contain only ownership metadata.
template <class T>
tmap::Result probe(tmap::MemoryProvider& provider, size_t count)
{
    tmap::MemoryBuffer<T> storage(provider);
    return storage.reserve(count);
}
template tmap::Result probe<tmap::Workspace>(tmap::MemoryProvider&, size_t);
template tmap::Result probe<tmap::PackageWorkspace>(tmap::MemoryProvider&, size_t);
template tmap::Result probe<tmap::CoordinateRouteCursor>(tmap::MemoryProvider&, size_t);
template tmap::Result probe<tmap::ContourRenderCursor>(tmap::MemoryProvider&, size_t);
template tmap::Result probe<tmap::RouteLabel>(tmap::MemoryProvider&, size_t);
