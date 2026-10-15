# Embedded resource evidence and integration gate

Requirement: equivalent Trail Mate workloads must not become more expensive;
new features must have explicit, frugal storage and work budgets.

## Current persistent-route acceptance

See [20 km route evidence](ROUTE_ARCHIVE_EVIDENCE.md) and its raw JSON for the
current native-map search, spool, export and independent saved-route read phases.
The read provider peaks at 1,768 B on the measured 32-bit ABI; long-route search
still requires explicit, substantially larger budgets. Physical board peaks
remain unmeasured. Later sections below preserve their original checkpoints.

## Archived display baseline

At this checkpoint the imported .cpp implementations were byte-for-byte
identical and reader.h added only a friend declaration (see NOTICE). The current
reader additionally dispatches native-container headers to a separate parser.
The baseline host Workspace is 14,024 bytes
and SearchCursor 856 bytes. Real world/China probes match pixels, annotations,
POIs, searches and page reads (217/183 at checkpoints), including a China tile
at offset 21,634,830,336. Source hashes are recorded in NOTICE.md. This establishes
compatibility and unchanged algorithm/storage behavior, not faster ESP execution.

## New capabilities

The earlier joint-feature evidence is in FULL_FEATURE_RESOURCES.md and
full-feature-native-evidence.json: defaults remain unchanged; the 37-slot,
two-page 1 KiB workspace is 3,560 B and the explicitly configured joint-service
typed payload is 9,240 B. Default/compact/37-slot/64-slot suites pass 32 entries;
the updated ABI checks reject all three storage-profile mismatches. Additional
ESP32-S3/C3 64-slot core evidence records no forbidden allocation/exception/RTTI
dependencies and individual-frame maxima 192/240 B. These are compiler and
host observations, not physical peaks. The archive sizes and 29-test reports
in the next paragraph record the earlier routing/elevation stage.

The earlier 2026-10-15 recollection includes routing-profile guards and coordinate
route-elevation aggregation. Tested host GraphReader is 296 B; optional elevation
aggregation retains a 192-byte cursor plus 24-byte output. ResourceBudget includes
these using target sizeof. Default/small host evidence each records 29/29 tests.
Current new archive-object text (legacy reader/search excluded): S3 41,805 B,
C3 54,160 B, C3 1 KiB configuration 54,164 B. All target objects report data/bss=0,
and nm checks find no allocation/exception/RTTI dependency. Maximum new individual
frames remain S3 192 B / C3 240 B. These are compiler/static-library figures,
not linked firmware Flash or complete task-stack peaks. Raw evidence is in
evidence-host.json, evidence-small.json and evidence-esp32*.json.

Host sizes: workspace 9,504 B; ContourCursor 496; CoordinateRouteCursor 1,384;
ContourRenderCursor 568; label plus heap slot 36; route leg 20; draw segment 24.
128 search slots use 4,608 B, 512 use 18,432. A 512-segment snapshot uses 12,288.
Reader/control objects, geometry, filesystem and UI buffers are additional.
Place large workspaces in caller static/member/PSRAM storage, not task stacks.

The optional MemoryBuffer<T> convenience layer explicitly reserves through a
platform MemoryProvider. Default policy requests external/PSRAM exclusively when
present and returns BudgetExceeded on failure; it requests internal RAM only if
external memory is absent. Raw borrowed buffers retain caller placement control.
The core does not detect physical memory regions or choose a platform allocator.
Host policy tests and S3/C3 large-buffer template compilation pass; this is not
a real-board PSRAM allocation measurement. See README and memory.h.

Arrays never grow. Overflow returns BudgetExceeded or a bounded contour density
retry. Allocation interception finds zero core allocations in exercised paths.
Operations are resumable/cancellable; Busy retains successful stages. Package
and graph reader calls require single-owner serialization.

Tests cover 2,048 nodes/2,047 edges, exact costs/legs, capacity, no-path/cancellation,
repeated Busy and malformed semantics, plus independent random shortest paths.
Negative restriction-range caching reduced the stress query from 16,933,632 to
328,448 successful read bytes without enlarging the two-page workspace.

Screen geometry uses float and integer local differences; elevations, distances
and accumulated metrics retain double. Double-reference Mercator tests span
z12–18, longitude -170/0/170 and latitude -84/-45/0/45/84: tested local errors
are <0.01 px, and conservative viewport query bounds contain the reference.
This is a tested sample, not a global mathematical bound.

Actual GCC 14.2 MinSizeRel results:

| Target | Configuration | New maximum frame | Unchanged legacy maximum |
|---|---|---:|---:|
| ESP32-S3 | Xtensa ESP32-S3 | 192 B | 240 B |
| ESP32-C3 | -march=rv32imc -mabi=ilp32 | 240 B | 272 B |

New frames pass 240 B; host passes 256. Undefined-symbol checks show no allocator,
exception runtime or RTTI dependency. Math/runtime helpers remain dependencies.
Single-function .su frames do not prove task call-chain high-water usage.

Archive object text bytes (data/bss zero):

| Source | S3 | C3 |
|---|---:|---:|
| unchanged reader + search | 16,951 | 19,542 |
| package | 3,821 | 4,813 |
| terrain | 6,703 | 9,042 |
| navigation | 11,461 | 15,550 |
| coordinate routing | 3,334 | 4,278 |
| contour renderer | 1,317 | 1,692 |
| exact administration | 2,928 | 3,446 |
| v1 approximate administration | 2,636 | 3,204 |

New text totals: 32,200 B S3 and 42,025 B C3. These are not final flash deltas:
linker --gc-sections removes unused sections; libm/compiler helpers and application
configuration can add code. The core defines no mutable global RAM in these
builds. Unused functionality needs no workspace allocation.

Mountain crop N27E086: z14 cold reads 709,184 -> 103,104 B; z18 148,032 -> 99,008.
Package grows 278,528 -> 475,136 B with optional overview. Final z14 uses 200 m
density to fit 512 segments. The default 10/20/100 m policy requests a starting
density; fixed capacity may require coarsening. See mountain-resource-report.json
for the source digest, actual densities and budgets. No device timing is inferred.

## Before replacing Trail Mate

The independent library passes local implementation checks. Adoption requires
final linked flash comparison and actual equivalent-workload device measurements:
task stack high-water, heap/PSRAM peak, SPI/SD traffic, draw and routing latency.
New features need separate budgets because no old equivalent exists. No whole
application memory improvement or hardware acceptance is claimed.
