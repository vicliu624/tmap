// Compile-only probe: instantiate the optional convenience on actual MCU ABIs.
#include "tmap/memory_object.h"
#include "tmap/navigation.h"
using namespace tmap;
Result createWorkspace(MemoryObject<PackageWorkspace>& object) { return object.create(); }
Result createPackage(MemoryObject<OutdoorPackage>& object, PackageWorkspace& workspace) { return object.create(workspace); }
Result createTerrain(MemoryObject<TerrainReader>& object, OutdoorPackage& package) { return object.create(package); }
Result createGraph(MemoryObject<GraphReader>& object, OutdoorPackage& package) { return object.create(package); }
void resetWorkspace(MemoryObject<PackageWorkspace>& object) { object.reset(); }
void resetPackage(MemoryObject<OutdoorPackage>& object) { object.reset(); }
void resetTerrain(MemoryObject<TerrainReader>& object) { object.reset(); }
void resetGraph(MemoryObject<GraphReader>& object) { object.reset(); }
