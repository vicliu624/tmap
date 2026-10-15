#include "tmap/route_storage.h"
using namespace tmap;
Result initializeCoordinateStorage(CoordinateRouteStorage& storage, size_t budget) { return storage.initializeStep(budget); }
Result initializeIntegerStorage(IntegerRouteStorage& storage, size_t budget) { return storage.initializeStep(budget); }
Result reserveIntegerLabelScan(IntegerRouteStorage& storage, size_t states, size_t outputs, size_t bytes) { return storage.beginLabelScan(states, outputs, bytes); }
void cancelCoordinateStorage(CoordinateRouteStorage& storage) { storage.cancel(); }
void cancelIntegerStorage(IntegerRouteStorage& storage) { storage.cancel(); }
Result initializeIntegerLandmarks(IntegerLandmarkStorage& storage, size_t budget) { return storage.initializeStep(budget); }
void cancelIntegerLandmarks(IntegerLandmarkStorage& storage) { storage.cancel(); }
