# Standalone library implementation audit

The consolidated current-state audit is [DELIVERY_AUDIT.md](DELIVERY_AUDIT.md).
The incremental notes below retain implementation history; where counts,
sizes or pending-feature statements disagree, use the consolidated audit and
its identified current reports rather than treating older notes as current.

Current optional-host integration gate: default/compact profiles pass 36/36
with TMAP_TEST_GEOMETRY=ON and optional codec/geometry packages available.
The six regional geometry tests now run in CTest/CI, rather than only manually.
Fresh separate reports preserve current inspector/source/GEOS hashes. Installed
tools include requirements-geometry.txt; installed consumers assert its presence.
The dependency-free suite remains 35 entries and does not imply optional-codec
execution when dependencies are absent.

ResourceOptions now explicitly counts independent bulk-read, validation and
file-identity cursors, summed read-buffer capacity, and additional point-elevation
cursor/sample pairs. Shared TerrainReader is included once. Tests verify exact
sizeof arithmetic, all five new count/byte overflow cases, and zero output on
failure. Zero counts preserve previous workload totals. Default/compact full
suites pass 35/35, core-only and S3/C3 rebuilds pass; latest compiler evidence
is evidence-esp32{s3,c3}-budget-cursors.json.

Full-file validation now has cancelValidation and the common cancel overload.
Tests verify mid-scan cancellation, idempotence, restart, revocation after
success and keeping the map open. ValidationCursor remains 24 B on the host
ABI. cancellation.h now aggregates integer, statistics, identity and rule
interfaces too. Latest S3/C3 compiler evidence is
evidence-esp32s3-cancellation.json / evidence-esp32c3-cancellation.json.

The latest 37-slot/64-slot complete-feature configurations are rebuilt and pass
35/35 tests each, including installed consumers, shared-cache progress and ABI
checks. current-full-feature-evidence.json remeasures all four profiles against
the actual Center synthetic 37-section artifact: 37-slot payload 9,176 B,
64-slot payload 10,256 B; 32-slot profiles reject explicitly. This report is
separate from the real Liechtenstein combined geographic workload.

The host PBF LZMA path now bounds decoder dictionary/state to 64 MiB separately
from the raw output cap. A one-byte stream declaring a 1 GiB dictionary rejects
before large dictionary allocation; ordinary ALONE/XZ fixtures remain valid.
The PBF suite passes all nine tests. This does not claim a whole-producer RSS
limit or change MCU code/objects.

The optional LZ4/ZSTD paths have since been bounded too; the expanded suite
passes 11/11 without skips when dependencies are supplied. See
PBF_CODEC_BOUNDS.md for empty-frame/trailing/window regressions and the explicit
distinction between decoder bounds and entire producer memory consumption.

Latest real-reader revalidation is in CURRENT_REAL_REVALIDATION.md: current
default/compact probes pass 40 mountain coordinate cases each, 144 real
conditional permission checks each and 400 real conditional route checks each.
The latter retain 132 explicit capacity failures per profile. World/China
legacy outputs are again identical. The reconciliation report records current
core source hashes rather than claiming historical resource reports are fresh.

## Current evidence reconciliation

The latest default and compact builds pass **35/35 CTest entries**, including
installed consumers, ABI checks and shared-cache progress. The optional GEOS
administrative suite additionally passes **6/6 with no skips** when its host
dependency is supplied. `REAL_OUTDOOR_JOINT.md` and its two JSON reports now
prove real country/district/municipal queries together with DEM, contours and
navigation on the Center-produced whole Liechtenstein graph. This supersedes
the older statements below about unverified real administrative data and
synthetic-only combined features. Representative interior points do not prove
all boundary accuracy or worldwide OSM topology support.

Current new-core single-frame maxima are S3 **160 B**, C3 **240 B** following
shared-cache progress fixes. Prior resource reports retain their own historical
configuration and source identities; their object sizes must not be read as
measurements of the latest code.

Requirement evidence map:

| Expanded requirement | Authoritative implementation/evidence entry point | Remaining qualification |
|---|---|---|
| Low-resource algorithm selection, maze alternatives | `ROUTING_RESEARCH.md`, `LABEL_SCAN_DIJKSTRA.md`, `INTEGER_ROUTE_ALGORITHMS.md` | No universal resource winner; capacities must be measured per workload |
| Approximate coordinate hierarchy and exact boundaries | `legacy_administration.cpp`, `administration.cpp`, `REAL_OUTDOOR_JOINT.md` | Real selected hierarchy validated; geographic source completeness varies |
| PSRAM-first ownership/convenience | `memory.h`, memory policy/object/staged/session tests | Physical board allocation/peaks unmeasured |
| Configurable storage and complete configured-object budget | `resource_budget.cpp`, storage-profile ABI and shared-cache tests | Application, allocator and call-chain stack overhead separate |
| Bounded retry/cancel and progress under eviction | `test_shared_cache.cpp`, cancellation/Busy tests, real joint reports | Provider must eventually satisfy a read; separate readers per in-flight operation |
| Navigation acceleration | Integer and float ALT, queue-free scan and reference suites | No nationwide capacity guarantee |
| Cross-file routing and terrain/admin composition | `RAW_REGION_SEAMS.md`, `REGIONAL_OUTDOOR_COMPOSITION.md`, verified source/patch reports | Complete independent neighbouring-country dataset not evaluated |
| PBF and complex permissions | `ROUTE_HISTORY.md`, `REAL_RUNTIME_NAVIGATION.md`, calendar road/barrier/turn tests | Supported calendar grammar only; unsupported forms reject explicitly |
| Low-floating-point navigation | `INTEGER_COORDINATE_ROUTE.md`, `INTEGER_MANEUVERS.md`, integer statistics | Geographic rendering and DEM interpolation still use floating point |
| Center-produced navigable real data and resource evidence | `REAL_NAVIGATION.md`, `REAL_MOUNTAIN.md`, `REAL_OUTDOOR_JOINT.md` | Hardware runtime evidence remains unavailable |

This matrix is a scope/evidence reconciliation, not a declaration of goal
completion. Earlier progress notes below are historical where they disagree
with this section. Unimplemented extensions are stated explicitly below;
Trail Mate adoption and final device acceptance remain separate.

Fresh core-only and cross-compiler archive/dependency/stack reports are
`evidence-core-only-joint.json`, `evidence-esp32s3-joint.json` and
`evidence-esp32c3-joint.json`. Their forbidden dependency lists are empty.
The budget audit also fixed a lost-overflow flag when an overflowing reader
count preceded terrain storage. Regression cases combine elevation with
SIZE_MAX edge readers, turn contexts, bindings and graph readers; each rejects
the request and clears the returned budget. Both 35-test suites and both real
55-case joint workloads pass after this correction.

Owner cancellation is additionally checked after every one-unit initialization
prefix through completion, with same-owner restart after each cancellation:
51 route/queue-free/ALT boundaries and 26 Busy session boundaries per profile.
The fake provider rejects internal-region allocation and verifies release;
sessions also verify zero live bytes. Reports are
`owner-cancellation-native-evidence.json` and
`owner-cancellation-small-evidence.json`; reproduce with
`tools/check_owner_cancellation.py`. This strengthens rollback evidence without
claiming physical PSRAM behavior or untested arbitrary element counts.

Installed-consumer validation now also compiles exported resource budgeting,
route storage and object ownership headers, links the budget implementation,
and exercises external allocation failure without internal fallback. It passes
against both default and compact installed packages. README size tables were
recompiled against current headers (32-bit pointers): ContourCursor 488 B,
ContourRenderCursor 560 B, CoordinateRouteCursor 1,376 B and exact admin cursor
488 B. The core-only recipe explicitly disables TMAP_BUILD_TESTS as well.

Real administrative evidence now also covers all 33 imported components, with
198 cold queries per profile (interior, vertex, four one-E7 neighbours).
GEOS agrees on all 179 successful and 19 Missing results, including 33 boundary
observations and 32 multiple-deepest-component matches. See
REAL_ADMINISTRATIVE_BOUNDARIES.md for exact sampling and accuracy limits.

The full current default/compact suites pass 35/35 and Center's TMAP-filtered
suite passes 21/21. CMake now optionally installs host Python tools under
share/tmap/tools, enabled by default for top-level desktop builds and disabled
for Generic embedded builds. Installed-consumer checks execute importer,
packer and regional-union entry points from that directory. The installed
packer also successfully produces the synthetic outdoor example and the C++
inspector opens it. This closes the installed ToolsDirectory gap without
changing MCU objects or embedding Python in the device library.

Center subsequently ran its actual CLI with the installed tools directory and
original selected-way regional inputs. Its 12,288 B output is byte-identical
to the source-tools reference; default/compact probes pass 20 two-direction,
five-integer-algorithm Busy queries. See INSTALLED_CENTER_PRODUCTION.md and
installed-center-real-artifact-evidence.json for scope and manifest hashes.

Current capability reconciliation on 2026-10-15. Deliverable: Projects/tmap, version 0.1.0, without
replacing Trail Mate. Outdoor v2.0 remains a draft format. No remote publication
or device upload was performed.

| Capability | Implementation and evidence |
|---|---|
| Independent library | C++17 static target, CMake install/export, PlatformIO manifest; installed/subdirectory consumers pass |
| Existing maps | Unchanged v1 raster/POI/search source; real world/China outputs, CRC and page counts agree |
| Outdoor storage | CRC32C, 64-bit offsets, two-page cache; damaged/truncated files and repeated Busy tested |
| Terrain | DEM interpolation, gaps/noise policy, ascent/descent and partial-road profiles; reference comparisons pass |
| Live contours | Spatial tree, streamed geometry, zoom LOD, projection/clipping, fixed buffers/density fallback; precision and mountain resource tests pass |
| Routing | Fixed sparse A*/Dijkstra and FIFO weighted relaxation; modes/turns/exact snapped endpoints; all methods match 24 independent cases |
| Administration | v1 bounded approximate coordinate query; v2 streamed integer polygon query with holes/boundaries/UTF-8; synthetic fixtures, GeoJSON import and coarse real country sample pass |
| Navigation | Clipped geometry, maneuvers, arrival, incremental deviation matching; 10,000-point route with 13-segment slices tested |
| Regions | Producer topology remapping, source/patch virtual view, trusted binary-manifest/source/virtual SHA verification; runtime route, capacity/digest/epoch/cancel failures tested |
| Center production | New Center Core/CLI SDK integration produces actual Monaco foot packages; 192 real-query/capacity cases per page profile plus 96 Busy cases; REAL_NAVIGATION.md |
| Tooling | Paged writer, marching squares, DEM/HGT and OSM XML/PBF import, CLI inspector; producer/CLI tests pass; real PBF entities independently compared with pyosmium |
| Resource bounds | Caller arrays, explicit budgets and zero exercised core allocations; 2,048-node route, capacity and cancellation stress tests |

Current local CTest passes all 33 entries in default and compact profiles, including the full-cycle runtime calendar primitive, streamed ordered calendar clauses, file identity verification, streaming SHA-256, virtual-region production/routing, composite-file continuations, session/route-storage rollback, integer routing/cost arithmetic, explicit cancellation, staged initialization, object ownership, resource budgets, PBF and the optional external-first
memory-provider policy. Actual ESP32-S3 and ESP32-C3 GCC 14.2
MinSizeRel core builds pass with exceptions/RTTI disabled. C3 uses RV32IMC/ILP32
soft float. New largest individual frames: S3 192 B; C3 240 B. See
RESOURCE_ACCEPTANCE.md for limits of compiler and desktop evidence.

Conditional access currently supports explicit production-time calendar snapshots
with months/dates/weekdays/clock intervals, a packed profile and guarded search
clock checks. The runtime calendar primitive and bounded clause reader are implemented,
including Busy continuation and epoch validation after reads and before publication;
their integration with supported live calendar road, endpoint-barrier and node/via-way turn permissions is now implemented. Real conditional-road evidence is in REAL_RUNTIME_NAVIGATION.md; unsupported noncalendar expressions still fail. Center can compose an
explicit outdoor document with the graph; real mountain evidence and exact
node/clipped route-elevation aggregation are documented in REAL_MOUNTAIN.md.
Integer node routing now offers FIFO and linear Dijkstra, plus integer statistics.
optional LabelScanDijkstra now omits the queue entirely, including PSRAM ownership
and resource-budget support. It saves four bytes per slot but can perform many
more scans. Actual Monaco, Liechtenstein and Everest comparisons and compiler
costs are in LABEL_SCAN_DIJKSTRA.md; the measured Monaco all-query winner remains
integer ALT at the smaller tested capacity. This is an explicit workload result.
Real resource comparisons and continuation-work costs are in
INTEGER_ROUTE_ALGORITHMS.md. Integer ALT is implemented with optional caller-owned/PSRAM storage and real evidence in INTEGER_ALT.md. The unified integer coordinate route pipeline reuses phase scratch and supports clipped geometry and integer elevation statistics; 32 real Center Everest cases pass per cache profile with FIFO/linear/heap/ALT and Busy injection. See INTEGER_COORDINATE_ROUTE.md. Integer clipped-leg maneuver hints are now implemented and checked against an independent real-map endpoint/direction/name/distance oracle; see INTEGER_MANEUVERS.md.

Compatible aligned DEM mosaics and retained regional contour/admin geometry now pass verified composite-file C++ queries with Busy, alongside cross-region routing. This adds producer-side composition without new MCU region/cache storage; see REGIONAL_OUTDOOR_COMPOSITION.md. Center-derived real-region production and actual verified source/patch navigation now have byte reuse, full validation I/O and default/small retained budgets in REAL_REGIONAL_NAVIGATION.md. The benchmark partitions one real Center artifact, rather than claiming independently exported regional datasets.

Bounded ordered via-way rules now compile from XML/PBF into paged finite-history tables for integer and float node/coordinate routes. Existing labels/turn scratch retain their ABI sizes; imported, mode/entry, detour, Busy and malformed-record tests pass. See ROUTE_HISTORY.md for explicit bounds and unsupported member forms.

Original-source regional seam reconstruction is now implemented without new MCU
storage, including Center multi-source production and verified real-way source/
patch routing. Graph-only seams require exposed shared junctions; see
RAW_REGION_SEAMS.md for precise source requirements and resource evidence.

Not implemented: noncalendar runtime conditional evaluation,
vertical-datum transformation, antimeridian-crossing paths, automatic
network downloads or device/UI integration. Unsupported restriction forms are
rejected. DEM resolution/datum limit terrain accuracy and missing heights are
not treated as zero.

The previously unextracted coordinate/admin workflow is now implemented in a
separate adapter without changing existing Reader layout/methods. Real district
accuracy, richer rules/imports, cross-file routing,
broader geographic validation and physical-device runtime evidence remain open.
Compact storage now supports 1/2/4 KiB physical pages, 2..4 configured cache
pages within the v1 workspace ceiling, 4..64 directory slots (default 32), streaming directory
CRC/open and ABI mismatch rejection. 1 KiB/32 slots uses 3,360 B. Bulk read and
elevation cursors preserve progress/cancellation across Busy; a cross-block DEM
regression identified and fixed the old restarting-corner failure. Other
remaining scope is unchanged.
Trail Mate adoption remains separate and subject to device tests.

The user expanded the current unpublished scope to include all previously
proposed capabilities. Active implementation is therefore incomplete; these
are current requirements, not a future-release deferral. Directed ALT is now
implemented and tested against baseline and independent reference costs.
ROUTING_RESEARCH.md records maze-robot/broader routing sources and measurements.
Other missing-capability entries remain open.

Current administrative increment: v1 bounded nearby-POI coordinate query and
v2 streamed integer polygon containment are implemented, including holes,
shared boundaries, complete common hierarchy, candidate limits/Incomplete,
UTF-8 validation, Busy, cancellation and CLI. Existing Reader layout and
implementations remain intact; header adds a friend declaration. Synthetic v1
and v2 fixtures pass; real administrative dataset accuracy remains unverified.
Routing adds optional fixed-FIFO weighted relaxation (same arrays) and deeper-g
A* ties, with independent reference cases. Maze primitive/resource-model report
does not represent measured MCU memory. Full expanded goal remains active.
