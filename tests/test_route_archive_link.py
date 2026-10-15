"""A route-only reader must not pull map/search/export implementations into its binary."""
import subprocess
import sys

symbols = subprocess.run([sys.argv[1], "-C", "--defined-only", sys.argv[2]],
                         capture_output=True, text=True, check=True).stdout
if "tmap::RouteArchiveReader::queryStep" not in symbols:
    raise AssertionError("Reader code was not retained; linkage evidence is inconclusive")
for name in ("tmap::GraphReader", "tmap::OutdoorPackage", "tmap::Reader::",
             "tmap::beginInteger", "tmap::integerRouteStep", "tmap::RouteArchiveWriter",
             "tmap::routeArchiveExport", "tmap::beginRouteArchiveExport",
             "tmap::routeArchiveViewport", "tmap::distanceMeters"):
    if name in symbols:
        raise AssertionError(f"Route-only reader retained an unrelated implementation: {name}")
print("Route-only linkage: no raster, graph, search, export or projection implementation retained")
