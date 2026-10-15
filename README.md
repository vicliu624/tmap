# TMAP

Portable C++17 library for embedded offline maps, vector contours, elevation
analysis and foot/bicycle routing. Located independently of Trail Mate; no
Trail Mate sources or dependencies are required for a normal build.

**0.1.1 is an experimental pre-release.** The portable core and host producers
are tested on real map files and cross-compiled for ESP32-S3/C3. Physical MCU
RAM/PSRAM peaks, task-stack watermarks, SD latency and power remain unmeasured.
Full navigation does not fit every few-KiB MCU. Choose explicit storage budgets
and inspect the [resource evidence](docs/RESOURCE_ACCEPTANCE.md) before integration.

- **Display and search:** v1 raster blocks, POIs, names and administrative paths.
- **Outdoor analysis:** v2 DEM sampling, elevation profiles, ascent/descent,
  height-tagged vector contours and administrative polygon queries.
- **Routing:** hiking/cycling graphs, integer costs, turn/access rules, optional
  ALT heuristics and bounded coordinate-to-coordinate routes.
- **Embedded ownership:** no implicit allocation in the algorithm core;
  optional explicit PSRAM-first owners, cancellation and budget errors.

## Install and integrate

Download a source archive from [Releases](https://github.com/vicliu624/tmap/releases),
or clone this repository. A desktop build needs CMake 3.20+, a C++17 compiler and
Python 3 for producer/tests. The embedded core has no Python runtime dependency.

```sh
git clone https://github.com/vicliu624/tmap.git
cmake -S tmap -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix install
```

Use `find_package(tmap CONFIG REQUIRED)` and link `tmap::tmap`, or add the source
with `add_subdirectory`. Embedded consumers should disable desktop tools/tests
as shown below. `library.json` provides a PlatformIO source-library manifest;
application file, allocation, scheduler and display adapters remain explicit.
Library version 0.1.1 and on-disk format versions v1/v2 are separate concepts.

Start with the [format](docs/FORMAT.md), [bounded elevation profiles](docs/ELEVATION_PROFILES.md)
and examples. The [delivery audit](docs/DELIVERY_AUDIT.md) maps implemented features
to evidence and remaining boundaries. Historical reports retain the exact
source/configuration they measured; they are not all current build gates.

## Real-map evidence

Trail Mate Center now produces real navigation packages through the SDK tools.
The [current-reader revalidation](docs/CURRENT_REAL_REVALIDATION.md) repeats
real mountain/conditional-country queries and legacy comparisons after the
latest continuation fixes, retaining explicit route-capacity failures.
See [real navigation resource evidence](docs/REAL_NAVIGATION.md) for the actual
Monaco PBF → Center → TMAP → C++ reader path, capacity failures and raw reports.
See [real conditional-road evidence](docs/REAL_RUNTIME_NAVIGATION.md) for the
original Liechtenstein PBF, Center production, time-dependent road access,
detours and explicit 32-state capacity failures. Compact-profile navigation
still needs 5,909 bytes of measured typed payload in this configuration, before
application/file/allocator/task-stack storage; a few-KiB MCU cannot assume it fits.
Original regional XML/PBF sources can now be united before graph segmentation,
including cross-source turn relations. Center supports explicit additional
sources. [Raw seam evidence](docs/RAW_REGION_SEAMS.md) records independently
Center-exported real-way fragments, the disconnected naive control, and verified
multi-file navigation with measured source/patch storage and retained controls.

The reader/algorithm core performs **no implicit heap allocation**, uses caller-owned workspaces, accepts
64-bit exact random reads, and supports bounded continuations for contour,
profile, snapping and routing work. Busy output must not be consumed. A storage
worker owns each package and its readers; media replacement requires closing
and reopening the package, which invalidates outstanding cursors.

## Current implementation

- `tmap::Reader`: unchanged v1 raster, POI, annotation and indexed search reader,
  including POI-row administrative paths and search-result administrative paths.
- Optional `MemoryBuffer<T>` convenience layer: explicit reservations through
  a platform-provided allocator, external RAM/PSRAM first by default, no silent
  internal-RAM fallback when external memory exists but allocation fails.
- Coordinate administrative queries: bounded v1 nearby-POI common hierarchy,
  explicit approximate/limited results; v2 streamed integer E7 containment,
  holes and shared-boundary common paths without loading whole polygons.
  [Real boundary checks](docs/REAL_ADMINISTRATIVE_BOUNDARIES.md) cover all 33
  components in the Center sample, including boundary and outside queries.
- `OutdoorPackage`: independently versioned 2.0 outdoor container, integrity
  pages and streaming validation. v2 is not readable by existing Trail Mate.
- `TerrainReader`: bilinear elevation queries, explicit resolution/datum,
  missing samples; bounded line/route profiles and noise-filtered ascent/descent.
  [Bounded profile job and display reducer](docs/ELEVATION_PROFILES.md) add explicit
  sample/storage limits, PSRAM-first ownership and real Center/C++ comparisons.
  The real-file 96-column profile uses 13,872 B default or 7728 B compact provider
  payload on the measured host ABI, plus a 160 B owner and borrowed inputs.
  Forty-eight default/compact/Busy/coverage/budget cases are recorded; these are
  configured library payloads, not whole-device RAM or PSRAM peaks.
- Vector contours: packed spatial tree, delta/varint geometry, streamed
  vertices, per-vertex LOD, spatial cells, height-mask pruning, zoom policy,
  screen projection and clipping. Fixed-capacity drawing supports bounded
  density fallback and reports the actual interval. Geometry is produced
  offline and drawn by the application, never a pre-rendered contour bitmap.
- `GraphReader`: shared bidirectional shapes, names, access, node-via turn rules.
- Sparse Dijkstra / optional directed landmark A* routing: caller-selected state budget; arrival-edge state
  preserves turn restrictions; ascent-weighted costs when supplied by producer.
- Optional queue-free integer label-scan Dijkstra saves four bytes per search
  slot at the cost of more scan work. [Actual map tradeoffs](docs/LABEL_SCAN_DIJKSTRA.md)
  show why ALT still has the smaller all-query configuration in the Monaco test.
  PSRAM ownership can explicitly omit queue storage.
- Road snapping: spatial-tree query and streamed edge-segment projection.
- Coordinate endpoint routing: both legal directions, clipped road legs,
  departure/arrival turn rules and sampled partial-road ascent costs.
- Route geometry, elementary maneuvers, progress, deviation hysteresis, arrival.
- Python standard-library tools: writer, marching-squares contours, WGS84 ASCII
  and HGT DEM importers, conservative OSM XML/PBF import and compatible region merge.

See [implementation audit](docs/IMPLEMENTATION.md) and missing capabilities
below. This experimental release is not hardware-validated.
Coordinate routing is implemented in `coordinate_route.h`; source and
destination are snapped to directed road interiors. The application supplies
fixed search arrays and a `RouteLeg` output buffer. Route geometry is streamed
without allocating or copying an entire route inside the library.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests use generated small synthetic fixtures. They verify exact expected
terrain values, missing-data gaps, streamed long contours, Busy retries, access
and turn rules, no-path and budget outcomes, snapping, deviation, CRC failures,
legacy opening and zero implicit core allocations. Release tests use explicit
checks, not assertions disabled by `NDEBUG`.

The dependency-free configuration has 37 CTest entries. For the complete
optional host geometry gate, install `tools/requirements-geometry.txt` and
configure `-DTMAP_TEST_GEOMETRY=ON`; this adds `regional_geometry`, whose six
checks include actual SDK queries across merged internal administrative seams.
Default and compact profiles pass 38/38 with geometry and optional codecs
available. CI selects this configuration. Installing scripts also includes
their requirements files under `share/tmap/tools`.

For a core-only build without stdio, exceptions or RTTI:

```sh
cmake -S . -B build-core -DBUILD_TESTING=OFF -DTMAP_BUILD_EXAMPLES=OFF \
  -DTMAP_BUILD_TESTS=OFF -DTMAP_NATIVE_FILE=OFF -DTMAP_EMBEDDED_CHECKS=ON
cmake --build build-core
```

GCC emits `.su` reports with `TMAP_EMBEDDED_CHECKS=ON`. The stack checker rejects
new core frames over 256 bytes and unbounded dynamic frames. Reports are for
the compiler/ABI used, not the cumulative stack of an ESP task. Actual S3/C3
cross-compilations are recorded below; runtime stack-watermark measurements
remain necessary before integration.

## Writer and inspection

```sh
python tools/tmap_pack.py examples/outdoor.json build/demo.tmap --report build/size.json
build/tmap_inspect build/demo.tmap
build/tmap_inspect build/demo.tmap elevation 0.0005 0.0005
build/tmap_inspect build/demo.tmap contours-svg 0.001 0.001 16 > build/contours.svg
build/tmap_inspect build/demo.tmap route 0 2
```

Example coordinates and terrain are synthetic. They do not describe a real
place. Writer JSON uses `[longitude, latitude]`, positive angular DEM steps,
and rows ordered south to north. See [format](docs/FORMAT.md).

Import geographic ASCII terrain and combine it with a complete OSM XML or PBF extract:

```sh
python tools/import_dem.py terrain.asc terrain.json --wgs84 --resolution-m 30 \
  --source "dataset/version" --vertical-datum 1
python tools/import_osm.py region.osm outdoor.json --terrain-document terrain.json
python tools/tmap_pack.py outdoor.json region.tmap --report region-size.json
```

ASCII input must already use WGS84 longitude/latitude degrees; GeoTIFF and
projected input need external conversion/warping. `import_hgt.py` accepts HGT
and HGT.gz with explicit crop, provenance and datum. Datum IDs are supplied by
the application; preserve the source's declared reference and use consistent IDs
across composed datasets. The real Skadi experiment assigns ID 1 to the
provider-declared WGS84/EGM96 heights. The core does not transform datums.
OSM imports preserve attribution obligations;
no external datasets are bundled. Supported conditional access and ordered
via-way rules are compiled explicitly; unsupported expressions and ambiguous
chains are rejected rather than ignored. PBF ingestion runs on the host producer and
adds no MCU dependency or workspace. `.pbf` selects the decoder automatically;
`--format xml|pbf` overrides extension detection. Raw, zlib, LZMA and bzip2 blobs
use Python's standard library; LZ4/ZSTD require optional host packages. Snapshot
history, unsupported required features, malformed lengths and decompression
size mismatches are rejected. Complete referenced nodes are required.
The standard-library LZMA decoder also has a separate 64 MiB decoder-memory
limit, covering dictionary/state independently of the 32 MiB raw-output cap.
Streams requiring more are explicitly rejected even when their declared output
is tiny. This is a host decoder limit, not a bound on the entire graph producer's
RSS; retained entities, parser objects and other codecs have separate costs.
Optional LZ4 always receives a positive explicit output capacity, including
declared-empty blobs, so a zero capacity cannot enable embedded-size allocation.
ZSTD validates its frame's declared content/window sizes, limits windows to
32 MiB and reads at most raw_size+1 bytes through a streaming decoder. A separate
single-frame delimiter rejects concatenation and trailing bytes, including empty
frames. Optional-codec tests run explicitly with lz4 4.4.5/zstandard 0.25.0;
without those host packages the two codec tests report skips. CI installs them.

The real Monaco 2026-10-13 extract (692,392 B) was independently checked against
pyosmium 4.3.1: all 41,708 nodes, 6,249 ways and 348 relations match in IDs, E7
coordinates, tags, ordered references and members. Source URL and SHA-256 are
in [PBF oracle evidence](docs/pbf-oracle-report.json). The decoder streams bounded
blobs; the producer still retains regional graph data in host memory. This is
decoder evidence, not MCU RAM measurement or proof of all OSM rule semantics.
The stricter default `both` travel-mode validation rejects this Monaco extract:
relation 7361260 lacks a from member. Earlier producer output skipped this
relation and is not evidence of complete rule import. Repair input data explicitly
rather than treating a malformed restriction as unrestricted travel.

An explicitly selected `foot` profile excludes vehicle-only restrictions and
retains applicable foot rules. Center successfully produced this real extract
with that profile; see [real navigation evidence](docs/REAL_NAVIGATION.md).

Node-via `no_entry` with multiple from ways and `no_exit` with multiple to ways
are expanded into ordinary forbidden edge pairs by the host producer, with no
additional MCU state format. Duplicate members, invalid cardinalities/types,
disconnected via nodes and unknown/dynamic restriction names are rejected.
The producer accepts
`--condition-at 2026-10-12T09:00:00+02:00` for an explicit local-time snapshot:
weekday lists/ranges and clock intervals (including overnight) can resolve access, one-way and
node-via restriction conditions before graph construction. Inactive clauses are
also validated, and unsupported expressions fail. This is a static snapshot,
not a runtime calendar evaluator; see [conditional snapshot limits](docs/CONDITIONAL_RULES.md).
Alternatively, `--runtime-conditions` preserves supported calendar road,
endpoint-barrier and node/via-way turn conditions for runtime evaluation.
The application supplies UTC and the applicable local offset; timezone and
daylight-saving inference are not built in. Noncalendar expressions remain
unsupported. See [runtime rules](docs/REAL_RUNTIME_NAVIGATION.md) and
[ordered turn history](docs/ROUTE_HISTORY.md).

Regional navigation graphs can be combined on the producer with
`tools/merge_regions.py`. Inputs must declare the same `topology_epoch`; shared
stable node IDs must agree in E7 coordinates. The merged paged graph traverses
region seams without a device-side global node map. Graph-only virtual packages
can route through source/patch files after trusted binary-manifest and full-file
identity verification. Original-source seam reconstruction and compatible
cross-region terrain/contour/admin composition are implemented; see
[source seams](docs/RAW_REGION_SEAMS.md) and
[outdoor composition](docs/REGIONAL_OUTDOOR_COMPOSITION.md).
Arbitrary unrelated packages still require compatible source identity/topology
and explicit production. Boundary nodes return OutsideCoverage
when a route cannot finish, and compatibility checks reject undeclared/mismatched
epochs. A successful route exposes conservative coverage warnings.

## Embedded memory contract

Current host sizes (32-bit pointers, 64-bit file offsets, MinGW GCC 9.2):

| Storage | Bytes | Ownership |
|---|---:|---|
| Legacy v1 Workspace | 14,024 | caller, unchanged baseline |
| Outdoor PackageWorkspace | 9,504 | caller, two 4 KiB pages |
| ContourCursor | 488 | caller per active query |
| RouteLabel + heap slot | 36 | caller per search capacity slot |
| CoordinateRouteCursor | 1,376 | caller, including snapping/search and landmark/queue continuation |
| ContourRenderCursor | 560 | caller, including contour continuation |
| ContourSegment | 24 | caller per retained draw segment |
| Reader / OutdoorPackage | 120 / 200 | control objects, not workspaces |
| TerrainReader / GraphReader | 128 / 296 | control objects; terrain retains Busy progress; graph caches routing profile |
| ProfileCursor / Navigator | 200 / 88 | continuation/control objects |
| MemoryBuffer<T> | 16 | optional ownership wrapper; excludes allocator overhead |
| AdministrativeWorkspace / AdministrativeCursor | 1,026 / 488 | exact query storage |
| LegacyAdministrativeWorkspace / Cursor | 1,056 / 544 | approximate query storage |
| LegacyAdministrativeCandidate | 540 | caller per retained candidate, default 4 |
| ReadCursor / ElevationCursor | 40 / 56 | bounded read / corner sampling state |

For 128 routing slots the two search arrays require 4,608 bytes; 512 slots use
18,432 bytes. These figures exclude the small reader/cursor control objects,
retained route geometry, application draw cache and filesystem buffers. Reuse
storage between v1 and v2 sessions rather than allocating both unnecessarily.
Core APIs never choose PSRAM or fall back to internal RAM: the caller controls
placement and budgets in the borrowed-buffer APIs. The optional memory wrapper
enforces external-first allocation requests as described below.
No `std::vector`, `std::deque`, `std::string`, RTTI or
exceptions are required in the portable core.

Budgets count algorithm operations, not wall-clock deadlines. One CRC page or
storage read can take longer than a nominal task slice. Yield on More/Busy and
cancel by closing/reopening the package. Route points and caller arrays must
remain alive until completion. A full search table returns BudgetExceeded;
it never allocates a larger table. Navigator currently requires a segment
budget covering the retained route when using the synchronous convenience API.
`beginUpdate`/`updateStep` match long routes incrementally without hidden buffers;
a newer fix invalidates an older unfinished match. Maneuvers also have a
caller-owned cursor and bounded steps, preserving callbacks across Busy retries.

The producer stores detailed contour geometry once, with per-vertex display
levels. An optional compact z12–14 overview avoids loading dense geometry at
lower zooms; it is a display cache, not seven duplicated zoom datasets. Disable
it with `contour_overview:false` when package size is more important than I/O.
`ContourRenderCursor` uses a fixed segment buffer, coarsens only a bounded number
of times on overflow, and reports the resulting interval. BudgetExceeded clears
the incomplete draw snapshot; it never silently publishes truncated contours.

The real mountain [resource report](docs/mountain-resource-report.json) records
source digest, I/O and draw budgets. At the tested z14 view, bytes read fell from
709,184 to 103,104 after layout/overview improvements, with the same two cache
pages. The full sample package grew from 278,528 to 475,136 bytes; this is an
explicit storage/I/O tradeoff. These measurements establish this sample only.

## Integration and installation

```sh
cmake --install build --prefix install
```

Consumers use `find_package(tmap CONFIG REQUIRED)` and link `tmap::tmap`.
Top-level desktop builds also install the Python producer/verification scripts
under `share/tmap/tools`; use that directory as Center's `ToolsDirectory`.
`TMAP_INSTALL_TOOLS=OFF` omits host tooling. Generic embedded configurations
default it off; it adds no MCU code or runtime dependency. A host Python
interpreter remains explicit. The installed-consumer test runs importer,
packer and regional-union help from the installation to check module discovery.
[Actual Center production from installed tools](docs/INSTALLED_CENTER_PRODUCTION.md)
also matches source-tool output byte-for-byte and passes 20 navigation queries.
See [API ownership and task-loop guide](docs/API.md) for bounded operations.
For embedded CMake builds set `TMAP_EMBEDDED_CHECKS=ON`,
`TMAP_NATIVE_FILE=OFF`, `TMAP_BUILD_EXAMPLES=OFF`, and `TMAP_BUILD_TESTS=OFF`.
Enable `--gc-sections` in the application's final linker flags so unused
functions and data are discarded. The static archive is not the final flash
footprint; math/runtime dependencies and application linker settings matter.
PlatformIO can consume `library.json`; desktop examples/Python tools are outside
`src`. Embedded builds can exclude `native_file.cpp` and implement
`RandomAccessFile` on their own filesystem. Exact reads must only return Ok when
all requested output is valid; Busy means retry without consuming partial data.

## Verification scope

### Platform independence and actual MCU limits

Core sources contain no ESP-IDF, Arduino, FreeRTOS, LVGL, operating-system task,
GPS, display-driver or filesystem implementation dependencies. The adapter
boundary is `RandomAccessFile`; application code supplies scheduling, storage,
graphics and memory placement. Parsing uses bytewise little-endian decoding,
not packed host-structure casts. Desktop `NativeFile` is optional and excluded
by the PlatformIO manifest. Python producer tooling runs on the host, not MCU.

Required: C++17 compiler, fixed-width integers/64-bit offset arithmetic, standard
memory/string functions and math support for terrain/navigation. Float/double
runtime helpers are needed on soft-float MCUs. No FPU, PSRAM or RTOS is required
by the API, but their absence affects available functionality and CPU cost.
Only MinGW host, Xtensa S3 and RV32IMC C3 builds have local evidence. Cortex-M,
AVR, big-endian targets, MSVC and actual board execution are not yet validated.
Configured Linux GCC/Clang CI is not evidence of a remotely completed run.

Do not equate portability with fitting any tiny MCU. Fixed 4 KiB pages/two-page
caches are the default; compact outdoor profiles are described below. The following are **buffer subtotals**,
not complete firmware RAM requirements, using the measured host ABI:

| Selected workload | Calculation | Buffer subtotal |
|---|---|---:|
| v1 indexed search | workspace + SearchCursor | 14,880 B |
| streaming contours | outdoor workspace + ContourCursor | 9,992 B |
| retained contours, 64 segments | workspace + render cursor + 64*24 | 11,600 B |
| retained contours, 512 segments | workspace + render cursor + 512*24 | 22,352 B |
| coordinate search, 128 slots, 64 output legs | workspace + planner + 128*36 + 64*20 | 16,768 B |
| coordinate search, 512 slots, 256 output legs | workspace + planner + 512*36 + 256*20 | 34,432 B |
| exact administrative query | outdoor workspace + query workspace/cursor | 11,018 B |
| v1 approximate query, 4 candidates | v1 workspace + query workspace/cursor + 4*540 | 17,784 B |

Add control objects, memory-provider bookkeeping, task/call-chain stack, input
and result geometry, filesystem cache, framebuffer and other application RAM.
Concurrent v1/v2 sessions need both workspaces; reuse storage only after the old
session and its consumers finish. A few-KiB-RAM device cannot run these current
legacy/full-feature readers. Compact outdoor storage and integer coordinate
navigation are implemented; rendering and DEM interpolation still use floating
point. Dijkstra capacity can exhaust on real
large regions even when a short route exists; no fixed RAM budget guarantees
successful arbitrary-region routing.

### Configurable outdoor storage

Compile configuration: TMAP_CACHE_PAGE_BYTES=1024/2048/4096,
TMAP_PACKAGE_CACHE_PAGES=2..4, TMAP_PACKAGE_SECTION_SLOTS=4..64 (default 32). Workspace sizes
larger than the unchanged v1 baseline are rejected; 4 KiB/four pages is excluded.
At least two pages support cross-page record progress. CMake exports definitions
to consumers; ABI tags reject mismatched header/library configurations at link.
The joint DEM/contour/admin/runtime-road/barrier/turn/ALT profile currently uses
37 sections. A 32-slot application rejects it with BudgetExceeded. Select 37
slots for this profile rather than reserving 64 unnecessarily: with two 1 KiB
cache pages the measured workspace is 3,560 B, only 200 B above 32 slots.
See [joint service evidence](docs/FULL_FEATURE_RESOURCES.md) for Center production,
interleaved services, total configured payload and the limitations of this test.
Manual/PlatformIO builds must use the same macros throughout the program.

```sh
cmake -S . -B build-small -DTMAP_CACHE_PAGE_BYTES=1024 -DTMAP_EMBEDDED_CHECKS=ON
cmake --build build-small
ctest --test-dir build-small --output-on-failure
python tools/tmap_pack.py input.json output.tmap --page-bytes 1024
```

| Two-page profile | Workspace | Exact-admin buffer subtotal |
|---|---:|---:|
| default 4 KiB, 32 slots | 9,504 B | 11,074 B |
| 2 KiB, 32 slots | 5,408 B | 6,978 B |
| 1 KiB, 32 slots | 3,360 B | 4,930 B |
| 1 KiB, 4 slots (only fitting packages) | 2,240 B | 3,810 B |

Lower capacity returns BudgetExceeded for larger physical pages/directory counts,
never reallocates. Default 4 KiB capacity reads smaller pages too. Legacy v1
remains fixed and needs 14,024 B. Add controllers, stack, wrappers, filesystem
and UI storage. open(file, section_budget) retains streamed directory chunks and
CRC across More/Busy even when all 32 entries exceed a compact cache page.

Default/compact builds pass 35 tests including file identity verification, streaming SHA-256, virtual-region production/routing, composite-file continuations, session/route-storage rollback, integer routing/cost arithmetic, explicit cancellation, staged initialization, object ownership, resource budgets, PBF import, installed consumers and ABI
rejection. C3 RV32IMC compact core also passes the 240 B new-frame gate. Reports:
evidence-small.json and evidence-esp32c3-small.json. Real coarse country data
shrinks 24,576->7,168 B, inside-query reads 20,992->6,656 B, calls 7->8, with
240 polygon operations unchanged (administration-small-resource-report.json).
Memory/I/O savings do not establish board latency or energy consumption.

beginRead/readStep/cancelRead retain completed pages in persistent caller buffers;
only Ok makes the entire output valid. TerrainReader::beginElevation/elevationStep
and cancelElevation retain successful corners at DEM boundaries. The existing
elevation() API uses one internal 56 B cursor; serialize convenience queries.
Explicit independent cursors may be stepped serially. The cross-block Busy
regression proves forward progress without growing cache page count.

### PSRAM-first convenience API

`memory.h` adds an explicit, optional allocation layer; algorithms themselves
never allocate or grow buffers. Implement `MemoryProvider` once on the platform:
`hasExternalMemory()` reports actual PSRAM availability, and `allocate/release`
must honor the requested region and alignment. External allocations must never
internally fall back to internal RAM. On ESP, an adapter uses external-only
allocation capabilities for `External`, internal-only capabilities for `Internal`.
The platform adapter is deliberately outside the portable library.

Preflight the selected workload on the **target ABI** before reserving buffers:

```cpp
tmap::ResourceOptions selected;
selected.integer_route = true;
selected.integer_label_scan = true; // Explicit queue-free algorithm/storage.
selected.route_states = 128;
selected.route_legs = 64;
selected.application_bytes = application_storage_bytes;
selected.allocator_overhead_bytes = provider_overhead_bytes;
tmap::ResourceBudget budget;
auto status = tmap::resourceBudget(selected, budget);
if (status != tmap::Result::Ok) return status;
if (budget.total_bytes > available_storage_budget) return tmap::Result::BudgetExceeded;
```

This counts configured retained objects and arrays, not route feasibility or
physical memory placement. Include task-stack storage and filesystem/display
buffers in the application allowance. Reader counts such as `graph_reader`,
`edge_rules` and `runtime_binding` must match independently in-flight queries.
The queue-free option must also be selected in the actual route options and
`IntegerRouteStorage::beginLabelScan`; ordinary ALT needs its queue and optional
landmark storage. Budgeting does not allocate or validate available PSRAM.

```cpp
// provider must outlive every buffer. Keep these wrappers as owner members.
tmap::MemoryBuffer<tmap::PackageWorkspace> pages(provider);
tmap::MemoryBuffer<tmap::CoordinateRouteCursor> planner(provider);
tmap::MemoryBuffer<tmap::RouteLabel> labels(provider);
tmap::MemoryBuffer<uint32_t> heap(provider);
// Explicit initialization, before the navigation hot path; check every result.
auto result = pages.reserve(1);
if (result != tmap::Result::Ok) return result;
// Independently reserve planner(1), labels(N), heap(N), legs(M), draw segments(K).
// Pass pages.data(), labels.data(), etc. to the existing borrowed-buffer APIs.
```

Default `PreferExternal`: external memory present -> request it exclusively;
failure -> BudgetExceeded, **no internal retry**. External memory absent ->
request internal memory. `RequireExternal` returns Missing without allocating
when PSRAM is absent. `InternalOnly` is an explicit caller override. Buffers are
noncopyable, fixed after reservation, initialized directly in allocated storage
and released through their original provider/region; no large initializer is
placed on the task stack. The provider must satisfy its region/lifetime contract;
portable code cannot identify physical PSRAM from arbitrary borrowed pointers.
Thus use this wrapper consistently for all large buffers to enforce the policy;
raw caller-provided buffers retain the caller's explicit placement decision.

### Reproducible experiments, 2026-10-14

Local reports: [host](docs/evidence-host.json),
[S3](docs/evidence-esp32s3.json), [C3](docs/evidence-esp32c3.json).
The reports include configuration, stack checks, target archive dependencies and
object sizes. These are recorded results, not estimated device performance.

| Experiment | Result / evidence | What it establishes |
|---|---|---|
| Release CTest | 38/38 entries pass in default and compact profiles with optional geometry/codecs installed | includes bounded profile ownership/display reduction, shared-cache progress, raw topology, administrative import, file identity/SHA-256, virtual-region production/routing, owner rollback, integer routing, cancellation, ABI mismatch and DEM Busy regression |
| Allocation interception | 0 implicit core allocations on exercised terrain/routing/rendering paths | no hidden growth in these paths; not total application allocation |
| Owned service composition | external-region requests, Busy, coordinate route, clipped geometry, GPS arrival, contours; exact live payload accounting and zero allocations after teardown | default/1 KiB host integration test, not physical PSRAM peak evidence |
| Large graph | 2,048 nodes, 2,047 edges/legs, exact cost, capacity/no-path/cancellation tests | fixed arrays, correct completion/failure |
| Independent reference | 24 generated graph cases vs Bellman-Ford, both modes and no/only turns | routing correctness for covered cases |
| ALT resource comparison | same-cost comb query: 1,469->63 expansions; 3,309,568->794,624 read bytes; 256 slots succeeds where baseline exhausts | lower usable search RAM in this query, not a universal guarantee |
| Integer representation comparison | equal-capacity arrays shrink 73,728->57,344 B; same FIFO states/I/O; 256 slots fails, 1,494 succeeds | smaller labels do not establish lower total resources than ALT; reports integer-resource-report.json and integer-small-resource-report.json |
| Storage contention | every second physical call Busy; stress completes, 163 read attempts, 328,448 successful bytes | resumable progress in covered paths |
| Per-step graph I/O | at most 2 physical read attempts in tested 7-operation slices | fixture-specific I/O bound, not a time deadline |
| Coordinate endpoints | partial-road geometry, legal direction, initial/final turns, ascent profiles | starts/ends at snapped positions rather than whole road endpoints |
| Long-route navigation | 10,000 points, <=13 matched segments/step | work per step stays bounded independently of route length |
| Contour overflow | bounded coarsening; terminal failure clears incomplete snapshot | fixed draw capacity, explicit actual density |
| Projection | double reference, z12/14/16/18, latitudes ±84/±45/0, longitudes ±170/0, tested error <0.01 px | local float precision and conservative viewport bounds |
| Damaged data | CRC, truncation, malformed adjacency/shape and incompatible epoch rejected | covered integrity and semantic failures |
| Producer tests | planar DEM, saddle, NoData, LOD endpoints, HGT orientation, access/one-way/unsupported rules | prepared format and import behavior |
| Consumer tests | installed find_package and add_subdirectory parent | independence from Trail Mate and opt-in desktop utilities |
| Memory placement | external-first, no silent fallback, absent PSRAM, alignment/overflow, release ownership | portable convenience policy via instrumented provider, not real PSRAM hardware |
| Real v1 maps | 5 comparisons each, outputs identical; world 217 / China 183 checkpoint page reads | compatibility, not ESP latency |
| Real mountain crop | 145*109 DEM samples, reader 7225.91 m vs input 7225.913557 m at sample | interpolation agrees with input; not independent ground truth |

The [integer coordinate navigation entry point](docs/INTEGER_COORDINATE_ROUTE.md)
accepts E7 latitude/longitude and runs bounded snapping, endpoint preparation,
integer search and clipped leg publication. Its scratch union reuses storage
across phases; the current native cursor is 936 bytes, including endpoint sets.
The [real Center coordinate report](docs/real-everest-integer-coordinate-report.json)
passes 32 forward/reverse, zero/climb-weighted, four-algorithm and Busy cases.
At fixed 128 state and leg capacities with elevation statistics retained, native
library payload is 16,392 bytes for FIFO/linear/heap and 17,036 bytes for ALT.
These small-graph capacities do not predict success on a larger region, and
snapping still uses floating point. Application/allocator costs and physical
device peaks are outside these host payload figures.
The [small-cache coordinate report](docs/real-everest-integer-coordinate-small-report.json)
passes the same 32 cases with 10,248 bytes for FIFO/linear/heap or 10,892 bytes
for ALT. This includes the 128-state search, 128-leg output and concurrent
statistics, but does not include a rendered polyline or Navigator state.

Clipped integer routes now have [streamed maneuver hints](docs/INTEGER_MANEUVERS.md)
with a 280-byte host cursor, caller callbacks and checked packed-length distances.
Updated real Center reports include endpoint/direction/name/distance oracles and
the maneuver cursor's concurrent resource cost; callback-retained event arrays
are additional application storage. Tangent classification is geometric and
uses floating point; lane-aware instructions are not provided.
With this control retained alongside 128-state/128-leg coordinate navigation and
elevation statistics, [default](docs/real-everest-integer-maneuver-report.json) /
[small](docs/real-everest-integer-maneuver-small-report.json) host library payload
is 16,672 / 10,528 bytes for FIFO/linear/heap, or 17,316 / 11,172 bytes for ALT.
Each updated report passes 32 cases and records desktop event storage separately.

Clipped integer routes now have bounded streaming geometry and integer
length/ascent/descent adapters; see [prepared route adapters](docs/INTEGER_PREPARED_ROUTE.md).
The [real Center Everest integer-leg report](docs/real-everest-integer-leg-elevation-report.json)
contains 60 exact whole/clipped/split-edge cases, including every-second-read Busy.
For that isolated statistics probe, default-profile retained library payload is
10,348–10,360 bytes, including package cache, package/graph controls, the 312-byte
host statistics cursor, 24-byte output and the one/two retained 12-byte legs.
This excludes search arrays, unused desktop probe slots, application and allocator
overhead. It is not a complete navigation budget or physical MCU peak measurement.
The [small-cache report](docs/real-everest-integer-leg-elevation-small-report.json)
passes the same 60 cases with 4,204–4,216 bytes of retained library payload;
read attempts range from 5 to 37, versus 4 to 35 in the default profile, including
open/initialization and Busy attempts. Small-cache arithmetic results are identical.
Output is only published after all legs succeed; missing elevation, cancellation,
invalid bounds and sum overflow are explicit failures. Full roads use packed
totals and clipped roads use integer profile interpolation without a geometry scan.

Virtual region production now retains [compatible outdoor layers](docs/REGIONAL_OUTDOOR_COMPOSITION.md):
aligned DEM grids, regional contours and administrative polygons. The existing
MCU readers query these through the verified composite file without new region
caches. Synthetic runtime tests cover Busy, terrain interpolation, contour
streaming, administrative lookup and cross-region routing. Arbitrary DEM grids
require explicit reconciliation; the host bounding-grid cell limit is enforced.
An explicit `dem_resampling` configuration now reconciles different grid steps
and origins on the host with rational bilinear interpolation, missing-data
preservation, deterministic overlap selection and bounded cell/work budgets.
Datums must agree and source resolution limits remain explicit.
Common automatic contours are regenerated from the composed surface, and exact
E7 contour chains are stitched with duplicate removal and explicit junctions.
Optional producer-only `administration_union` removes internal polygon seams,
preserves holes/islands and requires the declared Shapely dependency. The
[geometry evidence](docs/regional-geometry-evidence.json) records five optional
tests, including an actual SDK seam query. Native host geometry peaks are unmeasured.
Optional `contour_noding` also resolves different segment subdivisions and
same-elevation intersections before stitching, with E7 output rounding and segment
caps. Updated optional geometry evidence contains six tests, separate from CTest.

Compiler stack and text measurements:

The [runtime calendar primitive](docs/RUNTIME_CALENDAR.md) uses a 16-byte compiled
predicate, no heap/floating point/timezone database, and bounded civil arithmetic.
It passes a full 146,097-day Gregorian cycle and 15,300 grammar/offset/overnight
cases on the actual C++ probe. Object text is S3 896 / C3 826 bytes with data/bss
zero; evaluator compiler frames are 64 bytes. Explicit retained predicate counts
are included by resourceBudget. The evaluator now feeds live road, barrier and
turn permissions through caller-owned readers and an immutable query binding;
production-time snapshots remain available as a separate export policy.
Ordered calendar clauses also have a streamed evaluator with last-match/OR,
Busy/cancel/epoch handling and publication only after all records validate.
Host cursor/output are 128/8 bytes, counted explicitly by resourceBudget.
The seven-property road evaluator reuses one calendar cursor, freezes query time,
and publishes only after all properties finish. Its host cursor/output are 280/2
bytes; ResourceOptions.road_rules includes the embedded calendar cursor once.
Live road access/direction programs now feed GraphReader through an optional
caller-owned EdgeRuleReader (584 host bytes), with fixed query time. Default/small
paged synthetic tests pass 32 float/integer algorithm/mode/time node routes and
four snaps; successful edge sequences are checked. See
[runtime road evidence](docs/RUNTIME_CALENDAR.md). Conditional barriers and bound
via-way turn evaluation now have paged tests, including 32 integrated turn node
routes, 96 clipped-turn cases, 128 grouped node routes and 128 combined rule
cases. Real conditional roads and joint service resources are documented in
[real runtime navigation](docs/REAL_RUNTIME_NAVIGATION.md) and
[joint feature resources](docs/FULL_FEATURE_RESOURCES.md). The real snapshot
does not supply conditional turns/barriers; those still use explicit synthetic evidence.

[Via-way restriction history](docs/ROUTE_HISTORY.md) now runs through XML/PBF,
integer and float routing, including clipped coordinate boundaries. History uses
the existing label key and turn scratch: native integer labels remain 24 bytes,
float labels 32 bytes, GraphReader 296 bytes and integer route cursor 184 bytes.
Modes without active history bypass its tables. Active histories can require more
visited labels and file reads, so caller capacity exhaustion remains explicit.
The dedicated transition module's target-file text is 991 bytes S3 / 1,122 bytes
C3 with zero data/bss; this excludes call-site/compiler changes and firmware link
selection. Its individual compiler frame is 80 bytes on both targets. Imported
rules, Busy, detours, coordinate clipping and a CRC-valid invalid record are tested.

Real [verified regional navigation evidence](docs/REAL_REGIONAL_NAVIGATION.md)
now measures Center-derived OSM/HGT regions through actual source/patch/virtual
digest validation. Three sources and twelve extents add 1,088 host bytes; with
128-state/128-leg coordinate navigation, elevation and maneuvers the retained
library payload is 17,760 bytes default / 11,616 small. This partition reuses
only 1.13% of the virtual bytes and requires an almost full-sized patch. Cold
identity validation dominates adapter read attempts; neither storage savings nor
physical SD latency/power is inferred. Virtual sources/extents are included in
resourceBudget, while application/file/allocator overhead remains explicit.

| Target | Build | Largest new frame | Largest unchanged legacy frame | New archive-object text |
|---|---|---:|---:|---:|
| MinGW GCC 9.2 | Release | 208 B | 256 B | not used as MCU flash estimate |
| ESP32-S3 GCC 14.2 | MinSizeRel, no exceptions/RTTI | 192 B | 240 B | 41,805 B |
| ESP32-C3 GCC 14.2 | MinSizeRel, RV32IMC/ILP32 soft float | 240 B | 272 B | 54,160 B |

Recollected 2026-10-15 after routing-profile guards and route-elevation aggregation.
The C3 1 KiB profile has 54,164 B new archive-object text. See the per-object
`docs/evidence-esp32*.json` reports; these sums exclude reader/search legacy
objects and are not final firmware Flash deltas or cumulative task stack peaks.

Target archives have zero object data/bss and no allocator/exception/RTTI
undefined dependency; libm/float/integer runtime dependencies remain. Optional
memory helpers invoke the supplied provider explicitly. Object text is not a
linked firmware Flash delta. Single .su frames exclude full call-chain peak,
callbacks, filesystem and math stack usage. MCU reports are compile evidence;
there are no real-board latency, power or PSRAM peak measurements yet.

Reproduce with your compiler and tools (build first; paths below are examples):

```sh
python tools/collect_evidence.py build --label host-release --date YYYY-MM-DD \
  --ctest --output host-evidence.json
# After a Generic core-only cross build, use its target tools:
python tools/collect_evidence.py build-mcu --label target-minsizerel \
  --date YYYY-MM-DD --limit 240 --nm TARGET-nm --size TARGET-size \
  --output mcu-evidence.json
build/tmap_stress_tests build/stress
build/tmap_coordinate_tests build/fixtures
build/tmap_memory_tests
```

For optional memory-template cross checks, compile
`tests/embedded_memory_probe.cpp` with the target C++ compiler, `-Iinclude -Os
-fno-exceptions -fno-rtti -fstack-usage`, and scan its .su report as well.
The collector fails on failed tests, stack violations or forbidden archive
dependencies. Legacy comparison uses `tools/compare_legacy.py` with both probe
executables and the same external map file; external datasets are not bundled.

### Administrative lookup and missing capabilities

All missing capabilities below are part of the requested **current unpublished
implementation scope**, not deferred-version plans. Work is ongoing.
[Routing research and measurements](docs/ROUTING_RESEARCH.md) records the
Micromouse/ALT/CH/incremental-search review and implemented ALT increment.
Enable producer graph.landmarks:4 for optional lower-bound tables;
RouteAlgorithm::Auto uses them when available. Existing packages remain
supported by the baseline. Tables cost 8*K*N logical bytes; fewer expansions
do not automatically mean fewer media reads.

`Reader::administrativeLocation(poi_row, output, capacity, levels, flags)` reads
the stored administrative path for a **POI row**, not a coordinate. Sections
50/51 supply these values; legacy files without them return an empty/unknown
path. `searchAdministrativePath()` exposes a search result's stored path, and
`commonAdministrativePath()` preserves complete shared hierarchy segments.

Trail Mate's approximate coordinate-to-region workflow is in its ESP runtime,
not the copied core. It scans a nearby ~30 km band, caps candidates per package
at 256, retains up to 4 nearby eligible POIs, and returns their common hierarchy.
It is a heuristic, not polygon containment. `LegacyAdministrativeQuery` now
exposes iterative bounded traversal, fixed candidate storage and explicit
Incomplete for candidate-limited partial results. Cancel before Reader
close/reopen, including identical media. Dedicated real-data accuracy evidence
remains missing; synthetic validation is not inferred from baseline page counts.

`beginAdministrativeQuery`/`administrativeStep` query v2 polygons with integer
E7 predicates/even-odd rings. Holes exclude interiors; shared boundaries return
the common hierarchy at the deepest matched level and boundary=true. This is
containment in supplied data, not legal accuracy or GPS certainty. Input must
have valid simple rings and correctly arranged holes/disjoint components; no
automatic polygon repair. CLI: `tmap_inspect file region LONGITUDE LATITUDE`.

GeoJSON host importer: `tools/import_administration.py input.geojson output.json
--fields COUNTRY,PROVINCE,DISTRICT --source DATASET_ID`; supports Polygon and
MultiPolygon and rejects non-WGS84/unsupported geometry. It retains source SHA
and uses source identity in series metadata. The
[real country sample report](docs/administration-resource-report.json) uses
Natural Earth 1:110m: 2 components, 240 vertices, 24,576-byte package. Queries at
the tested Kunming/Beijing coordinates each read 20,992 B in 7 calls/240 operations;
an outside query reads 4,608 B. This only verifies coarse supplied country data,
not district accuracy. Source data stays in ignored build cache, not releases.

| Missing item | Current boundary / required follow-up |
|---|---|
| Coordinate administrative hierarchy | implemented/tested v1 approximation; real-data accuracy validation remains |
| Exact point-in-administrative-boundary | v2 polygon query tested against real OSM country/district/municipal geometry; see REAL_OUTDOOR_JOINT.md |
| Very small MCU memory profile | compact caches and integer coordinate routing implemented; full-feature workload still exceeds a few KiB |
| Cross-file runtime routing | verified source/patch routing, original-source seam reconstruction and compatible terrain/admin/contour composition implemented; see RAW_REGION_SEAMS.md and REGIONAL_OUTDOOR_COMPOSITION.md |
| Very large-region acceleration | bounded Dijkstra; no routing hierarchy/guaranteed nationwide search |
| Conditional/via-way OSM rules | XML/PBF import available; unsupported restriction forms rejected |
| Device navigation integration | GPS, audio, scheduler, display, replan policy and filesystem supplied by application |
| Hardware resource acceptance | final firmware flash, stack watermark, RAM/PSRAM peaks, SD latency and power still need measurement |

Real world and China v1 map probes are compared with the original Trail Mate
core in [world report](docs/baseline-world.json) and
[China report](docs/baseline-china.json). Outputs and page counts match exactly,
including a pixel at a 21.6 GB offset. Desktop wall times fluctuate and do not
prove ESP speed. Existing v1 implementations/layout are preserved; reader.h adds
only a friend declaration for the iterative adapter, documented in NOTICE.

The outdoor workspaces have stricter memory limits than the old reader, but
new navigation/terrain features have no equivalent old workload to compare.
There is no claim of completed device performance validation. See
[resource acceptance](docs/RESOURCE_ACCEPTANCE.md).

## Combined real outdoor workload

The newest [real Center joint outdoor report](docs/REAL_OUTDOOR_JOINT.md)
supersedes the earlier synthetic-only combined-feature evidence: a 19.2 MB
Liechtenstein package contains the real navigable graph, DEM, vector contours and
country/district/municipal geometry. Both default and compact configurations
pass 55 joint cases under injected Busy reads. Their configured library payloads
are 21,992 B and 15,848 B on the tested host ABI, excluding application and stack
costs. This does not establish physical MCU or PSRAM acceptance.

## License

AGPL-3.0-only. [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT) and
[NOTICE](NOTICE.md) retain source provenance. No relicensing was performed.
