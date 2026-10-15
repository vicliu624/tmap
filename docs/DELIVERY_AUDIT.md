# Delivery requirements and evidence audit

This audit reconciles the requested unpublished independent-library work with
the current implementation and available validation. It does not certify an
integrated Trail Mate firmware or physical-board performance.

| Requested work | Current implementation | Evidence inspected |
|---|---|---|
| Independent portable library | C++17 core, optional desktop adapter, CMake exported/installed target and host tools, PlatformIO manifest | Installed/subdirectory consumers, core-only build, Xtensa S3 and RV32IMC C3 builds; README |
| Resource-first routing/maze assessment | Sparse A*, directed ALT, FIFO relaxation, linear/heap and queue-free integer Dijkstra | ROUTING_RESEARCH.md, LABEL_SCAN_DIJKSTRA.md; 24 independent Bellman reference cases; actual Monaco capacity/work/I/O reports |
| Live contours and terrain | Runtime streamed geometry, z12–18 LOD, fixed output/density retry; DEM queries, gap policy, route/partial-edge climb | Terrain/render/reference tests; current real 55-case joint workloads; mountain coordinate statistics |
| Coordinate administrative hierarchy | Unchanged v1 reader plus bounded approximate adapter; streamed E7 polygon/hole/common-boundary query | Administration tests; 198 real queries/profile across all 33 components; REAL_ADMINISTRATIVE_BOUNDARIES.md |
| PSRAM conveniences | Explicit provider-backed buffer/object/staged/session/route/ALT owners; external-first with no silent fallback | Allocation/misalignment/failure tests; 51 route/ALT and 26 session cancellation boundaries per profile; installed consumer failure checks |
| Configurable cache and resource budget | 1/2/4 KiB pages, bounded cache/directory, ABI mismatch guard; sizeof-based buffers/controls/output plus explicit application/allocator allowances | Capacity/ABI/overflow tests; extra independent read/validation/identity/elevation counts; delivery-full-feature-evidence.json |
| Bounded retry and unified cancellation | More/Busy continuation; retained cross-page fragments; cancel/restart/publication revocation, including independent full validation | shared_cache_progress, package/cancellation/owner tests; real joint Busy cases; API.md |
| Acceleration and cross-file routing | Directed float/integer ALT; source identity union before segmentation; verified immutable source/patch virtual view | Raw source seam, disconnected control, mixed-source identity, virtual-region tests and reports; RAW_REGION_SEAMS.md |
| Cross-file outdoor data | Compatible DEM mosaics or explicit bounded resampling; retained/stiched/noded contours; optional polygon union | virtual_regions, regional_geometry six tests with SDK seam queries; REGIONAL_OUTDOOR_COMPOSITION.md |
| PBF and complex restrictions | Standard-library snapshot decoder, optional bounded codecs; mode/one-way/barrier/node/via-way permissions, supported calendar snapshots/live evaluation | PBF 11/11 with codecs; XML/PBF parity, calendar/reference/malformed/history tests; real 144 permission and 400 route checks/profile |
| Low-floating-point navigation | Integer costs/ALT/search, clipped coordinates and exact packed-model statistics; incremental maneuvers | Current Everest 40 cases/profile across five algorithms, both directions, climb penalties and Busy; INTEGER_COORDINATE_ROUTE.md |
| Actual Center production | Core/CLI invokes explicit SDK tools, source leases/hashes, regional inputs and outdoor documents; optional installed SDK tools | Center TMAP tests 21/21; real country/mountain artifacts; installed-tool byte identity and 20 navigation queries |
| Reproducible resource evidence and limits | Source/package/probe hashes, failure outcomes, typed payload, compiler frames/dependencies, README tables | CURRENT_REAL_REVALIDATION.md, REAL_OUTDOOR_JOINT.md, current compiler JSON and this audit |

Available local gates pass: default/compact optional-host suites **36/36**;
freshly rebuilt 37-slot and 64-slot suites **35/35**; geometry suite separately
**6/6 per profile**; PBF **11/11 with no skips** when dependencies supplied.
The 35-entry standard-library configuration can skip optional codec cases;
counts alone must not be used to imply optional dependencies executed.

Latest whole-real-map reports are `delivery-real-outdoor-native-evidence.json`
and `delivery-real-outdoor-small-evidence.json`: 55 cases each, all successful,
typed configured payload **21,992 B / 15,848 B**. Latest complete synthetic
combination is `delivery-full-feature-evidence.json`: 37-slot payload **9,176 B**,
64-slot payload **10,256 B**, with explicit 32-slot rejection. Different
capacities/workloads cannot be compared as if they were the same RAM demand.

## Actual package storage increment

The 1 KiB-page Liechtenstein packages have the same 20,432-node/51,736-edge
navigation graph/profile. Shared section stored sizes are unchanged. The
navigation-only file is 8,091,648 B; the outdoor file is 19,218,432 B, an increase
of **11,126,784 B**. Stored sections, including their page overhead:

| Outdoor package component | Stored bytes |
|---|---:|
| Routing and rules | 8,089,600 |
| DEM metadata/index/samples | 1,094,656 |
| Detailed/overview vector contours | 5,225,472 |
| Per-edge elevation profiles/policy | 4,680,704 |
| Administrative geometry/index/paths | 124,928 |
| Container/header/directory alignment | 3,072 |
| Total | 19,218,432 |

These are actual Center `sections.json` sums, not estimated navigation library
Flash growth or a promise for other regions. The joint benchmark validates
package/manifest identity and all data-page CRCs. Terrain grid spacing does not
establish vertical accuracy; OSM containment does not establish survey accuracy.

## Explicit implementation and evidence boundaries

Supported input grammar and bounded capacities are part of the APIs: unsupported
noncalendar/weather/vehicle expressions and ambiguous OSM restrictions fail
explicitly. There is no vertical-datum conversion, antimeridian path support,
automatic map download, GPS/display/audio integration or Center GUI navigation
export. Integer search does not remove geographic floating point from snapping,
rendering or interpolation. Fixed capacity does not guarantee arbitrary-region
routes: real 32-slot country testing retains **132 BudgetExceeded**, **50 NoPath**
and **218 successful** outcomes per profile. Complete independently sourced
neighbouring countries and worldwide polygon topology are not claimed tested.

Existing v1 reader/search .cpp source hashes match Trail Mate exactly; real
world/China outputs/page counts agree, including the 21.6 GB tile offset. New
workspace profiles stay within the old workspace ceiling. This establishes
the tested compatibility/storage properties, not improved physical speed.

Current S3/C3 GCC archives pass forbidden allocator/exception/RTTI dependency
checks and new single-frame limits (160 B / 240 B). **No physical-board RAM or
PSRAM peaks, task-stack watermarks, final linked firmware Flash delta, SD
latency, power or full application resource improvement have been measured.**
External-memory provider tests validate allocation requests and rollback, not
the physical allocator. Application/provider metadata, filesystem/display
buffers and call-chain stack are additional. Host producer/GEOS peaks are also
unmeasured; input/work caps do not imply bounded native GEOS peak memory.

This is an unreleased implementation. No publication, firmware upload, Trail
Mate replacement or hardware acceptance is implied by the implementation and
local evidence gates.
