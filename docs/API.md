# API ownership and bounded task loops

Use reader.h for legacy maps, package.h/terrain.h/contour_renderer.h and
coordinate_route.h for outdoor capabilities. Devices implement RandomAccessFile
with 64-bit offsets; NativeFile is desktop-only. readAt returns Status::Ok only
when the complete requested output is valid. Busy requests a retry. File size
and content must stay stable while the package is open.

Workspaces and large cursors belong in caller static/member/PSRAM storage, not
ESP task stacks. The file outlives its open package. One owner serializes package
and reader calls. GraphReader retains one ascent/turn continuation internally:
do not interleave separate routing/ascent/turn queries while one is pending.
Use separate package/workspace/reader ownership for independent concurrent jobs.
Callback values are borrowed until return; copy retained output. All input and
output arrays outlive their active cursors and downstream consumers.

Optional memory.h provides MemoryBuffer<T> on a caller MemoryProvider. Explicit
reserve calls default to external/PSRAM first. If external memory exists but its
allocation fails, BudgetExceeded is returned without internal fallback. If no
external memory exists, the default requests internal memory. RequireExternal
forbids that absent-external case; InternalOnly is an explicit override. Provider
must honor region/alignment and outlive the wrapper. Large buffers initialize
in place, are noncopyable and release through the same region/provider. Readers
never invoke this allocation layer themselves. Raw borrowed buffers remain under
the caller's placement control; portable code cannot infer their physical region.

## Results and scheduling

`cancellation.h` is the common include for float/integer routing, clipped
statistics/geometry/maneuvers, calendar road/turn rules, file identity and
virtual-map cancellation overloads. It also adapts bulk reads and full-file
validation to `cancel(cursor)`. Validation can now be cancelled independently
without closing the package; repeated steps return Cancelled until a fresh
beginValidation. Cancellation after successful validation revokes that cursor's
completion too. Its host size remains 24 B; no array is cleared or allocated.

ResourceOptions counts separately retained `read_cursors`, `validation_cursors`
and `file_identity_cursors`; sum bulk-read buffer capacities in
`read_buffer_bytes`. These are additional objects, not embedded cursors already
included in a graph/virtual-map reader. `elevation_queries` counts explicit
caller-owned ElevationCursor/sample pairs and implies one TerrainReader, shared
with any selected elevation/profile service. The reader's own convenience
cursor remains included in its sizeof and is not counted again. The base
outdoor package/workspace is always retained by this budget model; it is not
a minimal stand-alone file-hashing budget. Application handles/metadata and
allocator/call-chain stack storage remain explicit allowances.

Outdoor storage is configured at compile time and exported by CMake. Physical
page size is in the file header, supported at 1/2/4 KiB up to compiled capacity.
Link ABI tags reject mismatched client/library layouts. open(file,budget) limits
directory rows and preserves chunk CRC across More/Busy. V1 remains unchanged.
beginRead/readStep/cancelRead support bulk data with persistent caller output and
page budgets. Only Ok validates the whole output; keep its buffer intact between
calls. Larger arbitrary synchronous reads are not the recommended Busy interface.

TerrainReader::beginElevation/elevationStep accept caller ElevationCursor and
sample budgets; cancelElevation ends the cursor. The convenience elevation()
method retains one pending query, so serialize convenience queries across Busy.
Successful corners survive block/page transitions; replacement generations
cancel pending work. This adds 56 B control storage per TerrainReader.

Retry package.open with the same file after Busy; validated header progress is
retained. Consume info only after Ok/isOpen. Data pages are verified on read;
the validation cursor supports full streaming validation. More means bounded
work remains, Busy means storage must be retried. Yield before stepping again.
BudgetExceeded is explicit; NoPath, OutsideCoverage and Incompatible are distinct.
Missing/Incomplete heights are unknown, never a zero-height assertion.

Discard partial outputs on terminal errors. Close/reopen invalidates old cursors.
Abandon an individual query by stopping it and beginning a new one in its cursor;
close cancels the complete session. Budgets count operations, not microseconds:
CRC/storage/math calls require hardware latency measurements.

## Administrative queries

Include legacy_administration.h for v1. LegacyAdministrativeQuery::begin takes
Reader, point, workspace, fixed candidates/capacity, cursor, radius and scan/work
bounds. step is bounded; cancel ends one query. Results are approximate.
Incomplete preserves a valid candidate-limited partial path with limited=true;
BudgetExceeded/Cancelled/Invalid clear it. Cancel before Reader close/reopen.
Capacity is 1..32, default-policy example 4. No POI name decoding is required.

Include administration.h for v2. beginAdministrativeQuery/administrativeStep
stream spatial index and integer E7 rings. Depth counts hierarchy segments;
boundary marks an inclusive edge. Equal deepest matches yield complete common
segments. Hole interiors are excluded. BudgetExceeded clears partial path;
cancelAdministrativeQuery cancels one query; package generation cancels all.
Results borrow caller workspace strings. Polygon data validity/legal accuracy
and GPS uncertainty are separate from the exact integer containment predicate.

## Contour execution

beginContourRender accepts a viewport, fixed ContourSegment array and options.
Step contourRenderStep(package, cursor, 16) until terminal. Only Ok publishes
cursor.count segments; cursor.interval_m reports actual density. Overflow makes
bounded coarsening attempts; failures clear count. A viewport change starts a
new query. z0–11 disables contours, z12–13 requests 100 m, z14–15 20 m, z16–18
10 m. Low-level contourStep can stream vertices without retaining a whole view.
Offline generation prepares the vectors; projection/clipping/query happen live.
Optional coarse overview trades disk storage for reduced I/O.

## Routes and navigation

`beginFileIdentity`/`fileIdentityStep` in `tmap/file_identity.h` verify an expected
file length and SHA-256 with persistent caller storage. Each step attempts at
most read_budget reads of at most 64 bytes. Busy/More preserve the scratch buffer,
offset and hash; only successful exact reads feed the digest. No buffer or digest
is allocated. Max_bytes rejects excessive validation before reading. Empty files
are verified without reads. Complete digest mismatch or initial length mismatch
returns Incompatible; cancellation is sticky until begin restarts.

Optional FileEpoch callback records a media version at begin and checks it before
steps/after the last read, including repeated Ok calls. Version or length changes
return Cancelled. Without a callback, same-size replacement cannot be detected
after successful verification: the application must retain immutable sources and
invalidate dependent readers when media changes. File/callback context must outlive
the cursor; cancel before releasing them. Digest matching is identity against the
supplied expected digest, not manifest authentication.

Host cursor is 352 B, including SHA state and the 64-byte read scratch. Actual
S3/C3 verifier frames peak at 48 B (hash routine call-chain excluded). Tests cover
one-read slices, Busy without hash replay, corruption, initial size, max_bytes,
same-size epoch replacement, empty input, repeat success and cancel/restart.
The virtual manifest loader invokes this verifier for the manifest, every source
and patch, and the reconstructed logical file. It publishes only after all pass.

`Sha256` in `tmap/sha256.h` provides streaming source identity digests following
[FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf), with a
16-word rolling schedule and fixed caller-owned state. update accepts arbitrary
chunks; the application bounds work by chunk size. finish emits 32 bytes and is
idempotent; later update is Invalid until reset. Null/nonempty input is Invalid,
and exceeding the SHA-256 bit-length representation is BudgetExceeded without
consuming input. No allocation, mutable globals or floating point are used.

Host sizeof(Sha256) is 176 B plus the caller's digest/read buffer. Actual GCC
14.2 single-function frames peak S3 80 B/C3 48 B. Standard empty/abc/56-byte
padding/million-a vectors, chunk sizes 1..69, repeat finish/reset, and the virtual
TMAP digest from independent Python hashlib all pass. Source identity verification
must process all file bytes and compare against a trusted manifest digest; hashing
does not authenticate the manifest itself. The virtual loader requires a trusted
manifest identity supplied by the application; this primitive alone does not
verify files automatically or detect later source replacement.

`CompositeFile` in `tmap/composite_file.h` implements RandomAccessFile over
caller-owned FileExtent descriptors: source pointer, virtual offset, source
offset and length. Bind validates a contiguous, gap-free view, source bounds,
overflow and direct self-reference (up to 65,536 descriptors). Sources and
descriptors must outlive the view and remain immutable. The application must
prevent indirect source cycles and invalidate readers on source replacement or
rebinding; close dependent packages before detach/bind.

One serialized exact-read continuation is retained. Reissue identical offset,
output pointer and byte count after Busy/More, keep that buffer alive and its
completed prefix intact, and consume it only on Ok. A changed request abandons
the old continuation. Each call performs at most extents_per_read source reads
(default one), locating each extent by binary search; source latency is outside
the operation bound. cancelRead returns persistent Cancelled until restartRead
or bind. No file handles, cache pages or output buffers are allocated/owned.

Tests prove a three-source read progresses despite each source initially Busy
without replaying completed prefixes, cancel/restart, source offsets and invalid
descriptors. A TMAP package split inside a physical page opens and returns the
same elevation under Busy/More in default and 1 KiB profiles. GCC 14.2 S3/C3
adapter functions peak at 48 B single-function frames. This is the cross-file
storage foundation. `tools/virtual_regions.py` now prepares a graph-only virtual
package from paired regional JSON documents and TMAP files. It verifies every
source reproduces its document, merges compatible topology epochs/stable node
and edge IDs/turn restrictions, and builds remapped records on the host. Exact
whole pages are borrowed from source packages; other pages are stored in a patch.
Adjacent source ranges coalesce into extents. Input config is a regions array
of document/package paths relative to the config. CLI requires --manifest and
--patch; optional --page-bytes selects 1024/2048/4096. Source/patch lengths and
SHA-256 plus virtual package digest are recorded in the JSON manifest.

`virtual_map.h` provides beginVirtualMap/stepVirtualMap with caller-owned
VirtualSource slots, FileExtent descriptors, VirtualMapCursor and
VerifiedVirtualFile. Supply a trusted manifest FileIdentity and a source-byte
limit. The loader verifies the binary manifest, all sources, descriptor ranges,
and the complete logical SHA-256 before publishing. size() is zero until Ok;
failure or cancellation detaches the view. Optional source epochs detect later
replacement, including after reads; sources without epochs must be immutable.
Bindings, descriptors and files must outlive readers and remain immutable.
Close dependent readers before cancellation or rebinding and prevent indirect
source cycles.

The little-endian binary manifest has a 64-byte header, 40-byte source identities
and 32-byte extent records. Limits are 256 sources and 65,536 extents, further
bounded by caller capacity. Parsing and hashing advance by records and 64-byte
chunks. Final binding scans all extents and health checks scan all sources:
the work budget is not a strict CPU-time bound. Full identity checks read all
source bytes and the logical view, so startup I/O must be budgeted separately.

Tests load the generated binary manifest, reconstruct bytes and route across
the border under Busy, then check corrupted sources/manifests, insufficient
capacities, source replacement and cancellation. Loader function frames peak
at 128 B on both S3 and C3; this excludes the complete call-chain stack. The
tool cannot concatenate arbitrary TMAP files directly, merge DEM/administration/
contours, or infer a restriction omitted from every input region. Border overlap
must include both operands of a turn restriction in at least one source document.

Tiny default-page fixture: virtual package 40,960 B, patch 32,768 B, reused pages
8,192 B, four extents. Source packages remain required for referenced pages; this
does not mean total storage is only the patch or smaller than a standalone merge.
Reports: virtual-region-resource-report.json and compact counterpart. Descriptor
RAM is extent_count*sizeof(FileExtent), separate from cache/search/file handles.

`OutdoorSession` in `tmap/outdoor_session.h` explicitly owns one workspace,
package and optional graph/terrain readers. The application supplies an already
opened RandomAccessFile and MemoryProvider; both must outlive the session. No
platform filesystem/PSRAM API is required in the library. SessionOptions selects
required graph/terrain features and a max_object_bytes preflight ceiling.
`begin` performs no allocation; `initializeStep(work_budget)` advances fixed-size
construction and opens the directory with one-section slices. Busy/More preserve
progress. Only Ok publishes package()/graph()/terrain(). All allocation, parsing,
missing-required-feature failures release owned objects in dependency order;
cancel does likewise and preserves Cancelled until begin restarts.

Default external-first allocation applies to all owned objects, with no internal
fallback after external failure. `requiredBytes(options)` and `reservedBytes()`
cover object payload only; add sizeof(OutdoorSession), allocator overhead and
file/provider/application memory separately. Construction of one fixed-size
workspace is one work item, so the slice bounds operation count, not bytes/time;
provider latency remains external. Reset/cancel invalidate all borrowed pointers:
stop dependent queries and release application references before resetting.

This is the base map session. RouteStorage, renderer buffers, administrative
scratch and GPS/navigation state remain explicitly composed services; the session
does not allocate unused features or a display framebuffer. Tests inject each
of four allocation failures, Busy, missing features and corrupt directory data,
then verify zero live reservations and successful restart. Actual GCC 14.2
S3/C3 session functions peak at 48 B per frame, excluding provider/call-chain
stack. This does not claim final firmware or physical PSRAM measurements.

`tests/test_session.cpp` also exercises the complete composition with a provider
that requires every allocation to request External and injects Busy every second
file read: open graph/terrain, initialize 128 route states and 64 clipped legs,
search coordinate endpoints, stream the resulting clipped geometry into a bounded
64-point buffer, match the final GPS fix and verify arrival, and render contours
into 512 segments. Cursors/readers and retained buffers use explicit owners.
The test checks provider live bytes against the sum reported by every component,
cancels dependent operations, releases matching/geometry/route/render buffers,
then resets the map session and checks zero live allocations/bytes. This verifies
composition and ownership under the default and 1 KiB ABI profiles; it does not
measure physical PSRAM or include malloc metadata, wrappers, callback context,
NavigationState, file/provider objects and task stacks in that payload sum.

`tmap/route_storage.h` provides CoordinateRouteStorage and IntegerRouteStorage
for explicitly owned label, queue and output arrays. `begin(states, outputs,
max_payload_bytes)` validates products/sums and the payload ceiling without
allocating. `initializeStep(work_budget)` shares one budget across all three
reservations and per-element construction. No pointer is published until all
arrays are ready. Each reservation consumes a work unit; provider latency is
outside this bound. Any allocation/alignment failure releases earlier arrays,
preserves the failure result, and permits a new begin. Cancel/reset release
all reservations. An active/ready owner rejects begin without disturbing it.

`requiredBytes()` covers payload; `reservedBytes()` reports current reservation
during construction. Owner metadata, provider overhead and other session objects
remain separate application budget entries. Release only after cancelling all
borrowers. These owners manage route arrays; they do not open files or initialize
the entire terrain/render/navigation session. Default allocation remains external
first with no fallback after external failure.

```cpp
tmap::IntegerRouteStorage storage(provider);
auto result = storage.begin(256, 128, 8192);
// On subsequent scheduler slices:
if (result == tmap::Result::More) result = storage.initializeStep(32);
// Only after result == Ok, compose the borrowed workspace:
// {storage.labels(), storage.queue(), storage.states()}
// storage.output() has storage.outputs() uint32 edge rows.
```

Fault tests cover each of the three allocation failure points, rollback/retry,
hidden partial arrays, cancellation and preflight limits. GCC 14.2 compile-only
probe tests/route_storage_probe.cpp (-Os, no exceptions/RTTI, C3 RV32IMC/ILP32)
reports maximum single-function frames S3 32 B, C3 coordinate owner 80 B and
C3 integer owner 64 B. These exclude provider/call-chain stack and board peaks.

`tmap/integer_cost.h` provides checked exact integer cost primitives. For one
fixed AscentRatio n/d, an edge costs `length_cm*d + ascent_cm*n` units, each
unit being 1/d centimeters. Summation and ALT priority use the same units;
no individual-edge division or rounding changes route ordering. Denominator
zero is Invalid; a nonzero ascent coefficient with unknown terrain is Incomplete;
uint64 overflow is BudgetExceeded with zero output. Convert to display meters
only after computation. Different ratios cannot be compared in these raw units.

The optional `tmap/integer_route.h` now uses these primitives in exact node-to-node
FIFO relaxation. `beginIntegerRoute` accepts caller IntegerRouteLabel[N] and
uint32 queue[N]; advance `integerRouteStep` with a positive work budget, retry
Busy/More, and expose forward edge rows with `integerRouteEdges` only on Ok.
Travel modes, node-via turns, rational climbing, cancellation, bounded state/expansion
capacity and package generation invalidation are retained. Goal-first FIFO arrival
does not complete the search; the remaining queue is drained with nonnegative
incumbent pruning. Unknown climbing returns Incomplete; integer overflow or
exhausted search capacity returns BudgetExceeded. No implicit allocation occurs.

IntegerRouteLabel is 24 B; queue costs 4 B/slot (256 slots = 7,168 B). Host cursor
is 152 B. ResourceOptions.integer_route selects these actual sizes and uint32
edge output instead of clipped legs. Node routes can feed existing edge geometry
and maneuver streaming. Coordinate endpoints, clipped-road climbing and ALT
acceleration remain on the original double pipeline; this alternative does not
yet cover them and is not the default. FIFO can require more reads/computation.

Tests compare 24 independent Bellman reference cases, including Busy, mode/turn/
climbing restrictions and disconnected graphs, and verify every reconstructed
edge and cumulative integer cost. A 2,048-node/2,047-edge chain verifies full
output, bounded capacity/expansion failures and unknown elevation rejection.
Actual GCC 14.2 route frames peak at 96 B on S3/C3; the C3 search object has no
floating runtime helper references (GraphReader and memory functions remain).
These are compiler/synthetic tests, not measured board latency or energy.

Actual GCC 14.2 cost-primitive builds
report maximum primitive frames S3 48 B/C3 16 B; the C3 integer_cost object has
no undefined symbols, including floating runtime helpers. This does not establish
the total resource tradeoff of arbitrary queries or eliminate floating
geometry, snapping, interpolation and GPS matching elsewhere in the library.

`tmap/cancellation.h` provides overloaded `cancel(cursor)` for RouteCursor,
CoordinateRouteCursor, ContourRenderCursor, ManeuverCursor, ElevationCursor and
AdministrativeCursor, plus ProfileCursor, ContourCursor, SnapCursor,
RouteGeometryCursor, LegGeometryCursor and NavigationCursor. Cancellation revokes published route/contour counts and
retains caller ownership of buffers, without scanning/clearing them. Calls are
idempotent; step with valid positive budget reports Cancelled. Starting the
operation again initializes a new query. Cancel a dependent operation before
releasing its borrowed storage. Existing references/copies previously delivered
to application code remain the application's responsibility.

Callback cancellation in profile, contour and route/leg geometry streaming is
sticky: later steps continue to report Cancelled rather than Ok, until begin
restarts the operation. Package-generation invalidation also remains available.
Explicit route tests exercise mid-operation cancellation, repeated cancellation,
restart and result revocation, plus render/maneuver termination. Streaming tests
exercise repeat callback cancellation for route geometry, profile and contours,
and explicit snapping/navigation cancellation with navigation restart.

`StagedMemoryBuffer<T>` in `tmap/staged_memory.h` separates reservation from
initialization for large caller buffers. `beginReserve(count)` allocates but
returns More without constructing elements. `initializeStep(element_budget)`
constructs at most that many elements and returns More/Ok. Zero budget preserves
progress. `data()` stays null until Ok, preventing use of the unconstructed suffix.
`initialized()` reports progress; `bytes()` includes the entire live reservation,
not just constructed elements. `cancel()` releases it and subsequent steps return
Cancelled; a new beginReserve can restart. `reset()` returns to Invalid state.
An occupied owner rejects another reservation without disturbing progress.

The default external-first policy has no internal fallback on external failure.
Types must be nothrow default constructible and trivially destructible so partial
cancellation is constant work apart from the provider release. Budget is in
elements, not bytes/time; each construction costs sizeof(T) and the provider's
allocation/release latency is platform-dependent. Prefer small element types
for tightly sliced initialization. Synchronous MemoryBuffer remains available
for startup operations whose complete construction cost the application accepts.

Actual GCC 14.2 S3/C3 compile-only probes (`tests/staged_memory_probe.cpp`, -Os,
no exceptions/RTTI; C3 RV32IMC/ILP32) report maximum single-function frames of
32 B for RouteLabel reservation, initialization and cancellation. This excludes
provider and call-chain stack, board latency, allocator overhead and PSRAM peaks.

`MemoryObject<T>` in `tmap/memory_object.h` owns one explicitly allocated object
and accepts constructor arguments through `create(args...)`. It placement
constructs directly in provider storage, invokes the destructor on reset, and
releases to the original region. Repeated reset is harmless; create on an occupied
owner returns Invalid. Default PreferExternal and RequireExternal have the same
no-silent-fallback rules as MemoryBuffer. All constructors used on MCUs must work
without exceptions; destructors must be noexcept. Host constructor exceptions
release the reservation through a guard before propagating.

Declare provider and borrowed dependencies before their dependent owners; reverse
destruction then releases graph/terrain/package before the workspace. Never reset
the workspace while readers or unfinished operations borrow it. Wrapper storage
and allocator overhead belong in the application's budget.

```cpp
// provider outlives these owners. Check every result before dereferencing get().
tmap::MemoryObject<tmap::PackageWorkspace> workspace(provider);
tmap::MemoryObject<tmap::OutdoorPackage> package(provider);
auto result = workspace.create();
if (result == tmap::Result::Ok) result = package.create(*workspace.get());
```

`resourceBudget(ResourceOptions, ResourceBudget&)` computes storage without
allocating. It uses the selected cache macros and actual target ABI. Capacities
select retained route labels/queue/legs, contour segments and shared geometry
points; optional services add their readers, cursors and result objects. Profile
samples are optional because visitors can consume them immediately. Navigation
and profiles require at least two retained geometry points. Invalid combinations
or any product/sum overflow return Invalid with zeroed output.

`workspace_bytes`, `control_bytes` and `output_bytes` include one open outdoor
package and all selected concurrently retained services, without presumed scratch
sharing. Add file/provider/ownership wrappers, display/GPS objects, task stacks,
driver caches and other application memory as `application_bytes`. Add allocator
metadata/alignment overhead explicitly as `allocator_overhead_bytes`. The caller
also owns ResourceOptions/ResourceBudget themselves. This is a storage plan,
not a guarantee that a particular query fits the selected capacities, and does
not allocate or choose a physical memory region. Use MemoryProvider/MemoryBuffer
for explicit external-first allocation; an external failure never silently falls
back to internal memory under the default policy.

```cpp
tmap::ResourceOptions options;
options.route_states = 128;
options.route_legs = 64;
options.navigation = true;
options.geometry_points = 256;
tmap::ResourceBudget budget;
// Include tmap/resource_budget.h. Check the result before using budget totals.
const auto result = tmap::resourceBudget(options, budget);
```

Provide RouteLabel[N], uint32_t heap[N], RouteLeg[M] and a persistent
CoordinateRouteCursor. Initialize GraphReader on an open package (retry Busy).
Call beginCoordinateRoute(graph, start, end, options, radius,
{labels, heap, N}, legs, M, cursor). GeoPoint is {latitude, longitude} in signed E7.
Advance coordinateRouteStep with a bounded budget. Only Ok exposes cursor.count
legs and cursor.cost. Legal start/end directions and partial roads are handled;
complex regions may exceed the selected sparse search capacity.

beginLegGeometry/legGeometryStep stream clipped points;
beginLegManeuvers/maneuverStep stream departure, turns and arrival. Callbacks
return false to cancel. Road names use caller buffers. Climbing costs use
DEM-derived cumulative edge profiles with explicit unknown terrain.

TerrainReader::elevation reports interpolated meters, resolution and datum.
beginProfile/profileStep sample a caller polyline with explicit spacing/noise
threshold. Gaps return Incomplete and are not bridged in ascent calculation.

Navigator borrows retained route geometry. beginUpdate/updateStep match fixes in
bounded segment slices; a newer fix invalidates unfinished matching. Synchronous
update needs a budget covering the route. Application code owns GPS, display,
voice and replan policy; the library supplies progress/deviation/arrival results.

Executable examples of Busy/cancellation/capacity handling are in
tests/test_coordinate.cpp and tests/test_renderer.cpp; examples/inspect.cpp
demonstrates the complete file/import/inspection workflow.
