#include "tmap/navigation.h"
#include "tmap/staged_memory.h"
using namespace tmap;
Result reserveLabels(StagedMemoryBuffer<RouteLabel>& buffer, size_t count) { return buffer.beginReserve(count); }
Result initializeLabels(StagedMemoryBuffer<RouteLabel>& buffer, size_t budget) { return buffer.initializeStep(budget); }
void cancelLabels(StagedMemoryBuffer<RouteLabel>& buffer) { buffer.cancel(); }
