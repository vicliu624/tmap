# Real Center outdoor package and shared-cache evidence

The Center CLI produced `artifacts/navigation/liechtenstein-real-outdoor-v1`:
19,218,432 bytes, 28 sections, 20,432 nodes and 51,736 directed edges.
This is the real Geofabrik Liechtenstein 2026-10-13 PBF graph with a real
617 × 844 elevation grid and original OSM administrative relation geometry.
The 14 selected relations represent country, two districts and eleven
municipalities; their multipolygons contain 33 components and 14,374 vertices.
The importer validates topology and selected-parent containment; it does not
repair invalid rings silently. Selection is explicit, not a worldwide automatic
administrative hierarchy discovery claim.

The HGT source is [Mapzen Skadi N47E009](https://s3.amazonaws.com/elevation-tiles-prod/skadi/N47/N47E009.hgt.gz).
The provider documents WGS84/EGM96 metre elevations in its
[formats](https://github.com/tilezen/joerd/blob/master/docs/formats.md) and
[source attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md).
The application assigns datum ID 1 to this declared reference. Grid spacing is
not surveyed vertical accuracy. Europe coverage combines provider datasets;
this report does not identify the origin of each pixel. OSM attribution and
source hashes are retained in the generated documents and manifest.

## Joint workload

Each cold process queries one representative interior point in each of the
eleven municipalities at zoom 7, 12, 14, 16 and 18: 55 cases per configuration.
Navigation, point elevation, partial-edge elevation, administrative lookup and
contour rendering advance serially in round-robin calls. Every other file-read
attempt returns Busy. Two independent graph/rule readers retain the state of
the two in-flight graph operations. These are cooperative operations, not a
thread-safety claim. Route statistics are then computed while storage remains
allocated. All 55 cases in each configuration returned successful results.

| Configuration | Package workspace | Configured library storage payload |
|---|---:|---:|
| Default host ABI | 9,504 B | 21,992 B |
| Compact host ABI | 3,360 B | 15,848 B |

Payload includes the configured controls, two readers/bindings, 128 route
states, queue and landmark bounds, 128 route-output entries, 128 contour
segments, administration storage and elevation/statistics cursors. It is
`resourceBudget` storage accounting on the tested 32-bit-pointer host ABI,
not measured process RSS or final MCU RAM peak. Allocator metadata, application
objects, file buffers, display framebuffer and task call-chain stack are extra.
The desktop probe uses desktop allocations; it does not test physical PSRAM.
Use the external-first allocator helpers to place eligible objects in PSRAM;
their allocation-failure policy is separately tested, without silent internal
RAM fallback. A few-KiB RAM device cannot infer full-feature support here.

GEOS independently checks administrative membership/hierarchy, elevation is
compared with the source grid, and prefix/route totals with packed arithmetic.
All package pages are CRC checked. Zoom 7 produces zero contour segments;
higher levels respect the 128-segment cap and may retry with a coarser interval.
The fixed route here has four edges and zero climb. It tests joint progress,
not whole-country route capacity; the partial-edge query has nonzero climb and
descent. Earlier real route suites remain necessary for longer-route coverage.

Machine-readable cases, I/O counts, hashes and limits:
[default](real-outdoor-joint-native-evidence.json),
[compact](real-outdoor-joint-small-evidence.json).

[Additional administrative boundary checks](REAL_ADMINISTRATIVE_BOUNDARIES.md)
cover all 33 components with interior/vertex/one-E7-neighbour queries: 198 cases
per profile, including 33 boundary observations and 19 expected outside results.

A later decode-window experiment passed correctness checks but increased
joint media traffic by approximately 3.6%, with unchanged RAM. It was reverted;
see [the measured comparison](CONTOUR_WINDOW_EXPERIMENT.md). The retained
terrain source hash again matches the baseline reports above.

## Defect exposed by real data

Cross-page reads originally restarted after Busy. When other services evicted
their first page, some queries never progressed. Persistent fragment cursors
now retain progress for terrain, contours, administrative geometry, graph
edges/profiles, snapping and landmark rows. Compacting tree-frame child counts
to the format's actual 16-bit width offsets cursor storage: contour cursor
496 → 488 B, administrative cursor 544 → 488 B on this ABI. Elevation cursor
56 B, terrain reader 128 B, graph reader 296 B and rule reader 584 B remain
unchanged. `shared_cache_progress` exercises forced eviction, cross-page reads,
eight landmarks and snapping. Progress still requires the provider eventually
to satisfy a retried read.

The final GCC stack-report gate passes at 240 B: new-core maximum single frame
160 B for ESP32-S3 and 240 B for ESP32-C3. These are cross-compiler static
reports, not task stack watermarks or hardware execution. Physical RAM/PSRAM
peaks, final firmware flash, SD latency and power remain unmeasured.

Reproduce with `tools/import_osm_administration.py`,
`tools/prepare_real_liechtenstein_outdoor.py`, the Center `TmapTool navigation`
command and `tools/benchmark_real_outdoor_joint.py --help`. Import and oracle
use optional host Shapely/GEOS; the MCU core has no geometry-engine dependency.
