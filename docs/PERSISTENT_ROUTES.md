# Persistent routes and bounded viewport reads

TMAP can plan a route without retaining a complete result array, export it to a
separate `.tmroute` file, and read its geometry through a bounded spatial or
distance window. A saved route can be displayed after closing the map and
releasing all graph and search storage.

This is a portable library contract. Filesystem paths, SD/FatFS integration,
durable synchronization, task scheduling, GPS state and UI remain application
responsibilities. Trail Mate firmware is not changed by this implementation.

## Three independently budgeted phases

| Phase | Retained resources | Released afterwards |
| --- | --- | --- |
| Search | Map package, graph, bounded labels/queue, optional landmarks, planner cursor, small spool adapter | Search arrays and planner |
| Export | Map package and graph, one route leg, bounded geometry cursor, archive writer/workspace | Map, graph, spool and export state |
| Load/display | Archive reader, one 1 KiB page, small wire scratch, bounded query cursor | No graph or search allocation is needed |

Persisting the result does **not** spill search labels, the priority queue or
parent states to SD. Search exhaustion still returns `BudgetExceeded`. The
[20 km acceptance evidence](ROUTE_ARCHIVE_EVIDENCE.md) records both successes and
capacity failures. A small MCU can read a route prepared elsewhere even when it
cannot calculate that route itself.

## Plan directly into a spool

`IntegerRouteLegSink` accepts idempotent, random-position leg writes. It replaces
the complete output array, while the existing bounded search workspace remains
unchanged. The array APIs remain available.

```cpp
// All large objects and arrays below are borrowed, caller-owned storage.
// Run each initializer until Ok, scheduling Busy/More without consuming output.
beginIntegerCoordinateRouteToSink(
    graph, start, destination, options, snap_radius_m,
    search_workspace, spool, maximum_route_legs, planner);

// Call repeatedly from the application's worker, with a chosen work quantum.
integerCoordinateRouteStep(graph, planner, 8);

// Only a successful completed planner publishes the final count and cost.
integerCoordinateRouteSummary(planner, leg_count, cost_units);
```

For already prepared endpoints, use `beginIntegerPreparedRouteToSink` and
`integerPreparedRouteStep`, then `integerPreparedRouteSummary`. Both forms
retain the existing endpoint alternatives, access/turn checks, clipped starting
and ending roads and search algorithms.

`RouteLegSpool` implements the sink over `WritableRouteFile`. A spool row is
12 bytes and contains a directed edge and its first/last centimeter offsets.
The planner walks the parent chain with bounded steps and writes the reverse
walk into its final row positions. It never constructs a full intermediate edge
or coordinate array. Partial endpoint writes have separate continuation phases.

A better endpoint alternative may overwrite the previous candidate. Only the
final successful summary defines the valid row count; a spool may have an unused
tail. It is temporary job storage, not a committed route file. On cancellation
or failure, the application discards it. Calling the array-result API on a sink
planner returns `Unsupported`; use the summary API instead.

## Export a self-contained route

After search succeeds, copy its summary and release its labels, queue, landmark
storage and planner. Keep the map package, graph and spool until export ends.

```cpp
writer.begin(route_file, map_info, uint32_t(leg_count), cost_units,
             maximum_file_bytes, options.ascent, options.mode);
beginRouteArchiveExport(graph, spool, uint32_t(leg_count), writer, export_cursor);

// Repeatedly schedule this step. Ok means the final header was written.
routeArchiveExportStep(graph, spool, writer, export_cursor, 8);
```

The exporter copies checked leg records, converts one leg at a time through the
existing geometry reader, and appends points to a fixed page. It deduplicates
shared endpoints and represents discontinuities explicitly. Cumulative geometry
distance is recorded in centimeters. This can differ slightly from the graph's
rounded edge-length cost; ascent-weighted cost is also a different quantity.

Each data page holds up to 40 points. Adjacent pages overlap by one endpoint so a
cross-page segment is emitted once. Finalization builds a balanced bounding tree
on the file, bottom-up, by reading a page or child node at a time. It does not
allocate an index proportional to route length. The exact eventual index size
is included in the file budget.

The low-level writer also supports caller-generated geometry. Write the declared
leg records sequentially, then append `RouteArchivePoint` records with ordered
distances starting at zero. `legs == 0` permits geometry-only tracks. A break flag
prevents a line across a gap. `finishStep` builds the index and completes the
header. This does not turn planned routes into recorded GPS tracks; recording
policy and provenance belong to the application.

## Persist and publish safely

`WritableRouteFile` extends exact random reads with exact, idempotent random
writes. `Busy` or `More` consumes no input. An adapter must finish an operation or
report a retry without making the core advance over incomplete I/O. A permanent
write failure invalidates the job.

The application creates an exclusive, empty temporary file. `begin` writes an
uncommitted header. Cancellation, storage exhaustion or failed output leaves a
file that normal archive open rejects. Only successful finalization publishes
the complete header, with lengths, map identity and checksum.

After finalization, perform the platform's required durable synchronization,
close or hand off the file, and rename/commit it into the application's route
collection. Check synchronization and rename errors. `NativeRouteFile::flush`
flushes stdio; it does not promise physical-media durability. SD removal,
out-of-space handling and orphaned temporary-file recovery belong to the
application, which owns the filesystem transaction.

For explicit export cancellation use `cancelRouteArchiveExport(writer, cursor)`.
It cancels both participants. Reader and validation cursors can also be cancelled.
Media replacement requires closing the reader; outstanding cursors then return
`Cancelled`.

## Read only the required window

`RouteArchiveReader::open` reads the 192-byte header. Nodes and pages are checked
on access. `beginValidation` / `validationStep` provides a separate bounded scan
of every stored leg, geometry page and index node, including content outside the
current view. Validation checks record CRCs, coordinates, monotonic distances,
page seams, counts and index-pointer bounds.

```cpp
RouteArchiveQuery request; // Small options object; large cursors are caller-owned.
routeArchiveViewport(viewport, request.viewport);
request.max_nodes = node_limit;
request.max_segments = segment_test_limit;
reader.beginQuery(request, query_cursor);

// Each callback receives one candidate segment; no output polyline is retained.
reader.queryStep(query_cursor, 8, draw_candidate, application_context);
```

The tree stores geographic bounds and cumulative-distance ranges. A query reads
only potentially intersecting nodes and pages. Within a page, integer segment
bounds conservatively reject unrelated segments. The callback projects the two
endpoints using `project` and clips them using `clipSegment` before drawing.
Conservative candidate filtering avoids dropping a crossing because geographic
straight lines and projected straight lines differ. Off-screen candidates can
be read as part of an intersecting fixed-size page, but are not drawn.

`routeArchiveViewport` converts the SDK's unrotated Web Mercator `MapViewport`
into conservative geographic bounds. Wrapped longitude is supported. An
application with a different projection or a rotated view supplies conservative
`Bounds` directly and performs its own final clipping.

To read around navigation progress, set `first_cm` and `last_cm`. These options
can be combined with spatial bounds or used with the default worldwide bounds.
A distance window and a map viewport are independent: panning to the destination
must not change the current navigation-progress state. Loops and repeated visits
to an area retain their route order and distinct cumulative distances.

`max_nodes` bounds index work; `max_segments` bounds candidate tests, including
rejected segments. Exhaustion is explicit. A smaller step quantum yields more
often without changing the query. Callback rejection returns `Cancelled`.

One work unit emits at most one candidate. When collecting into a fixed output
array, keep the step quantum within the available slots, drain that array, then
resume the same cursor. Direct drawing callbacks do not need an output array.

## Memory and linkage ownership

The core has no allocator. Workspaces and cursors belong in caller-controlled
storage. Explicit owners use the existing external-first policy:

```cpp
MemoryObject<RouteArchiveWorkspace> pages(provider);
MemoryObject<RouteArchiveReader> reader(provider);
MemoryObject<RouteArchiveQueryCursor> query(provider);
```

After `pages.create()`, construct the reader with `reader.create(*pages.get())`.
The provider maps `External` to PSRAM or equivalent storage. With external memory
present, allocation failure never silently falls back to internal RAM. Use
`RequireExternal` when internal allocation must also be forbidden on devices
without external memory.

The measured 32-bit host read payload is **1,768 bytes**, independent of route
length: workspace 1,216 B, reader 160 B and query cursor 392 B. Three optional
owner controls add 36 B; the measured provider control adds 24 B. File/driver
buffers, allocator overhead, task/call-chain stack and display state are extra.
This figure does not establish a complete few-KiB navigation-device budget.

Reader, writer, spool, geometry export and viewport conversion are separate
compilation units. The route-only linkage test rejects retained raster, graph,
search, writer, projection and export implementations. Archive CRC uses a 64-byte
nibble table, independently checked against the existing CRC implementation.
For embedded builds, retain function/data sections and enable the toolchain's
unused-section elimination as appropriate for the rest of the SDK.

## Map changes and feature boundaries

Stored coordinates can be displayed without any original map. Stored edge IDs
and clipped leg offsets are only exposed through `readLeg` after the original
map ID, build, series and revision match. A changed graph must be revalidated or
replanned by the application. Changing the displayed basemap does not require
changing the graph used for navigation.

The archive stores geometry and graph references, mode and ascent-cost ratio.
It does not freeze time-dependent access permissions, GPS matching state, UI
preferences or live navigation progress. Its distance index is available for
bounded consumers; this change does not add a new persisted maneuver or elevation
sample format. Existing geometry retains full detail, so a whole-route view may
read every page. Memory remains bounded even in that case.

The application owns route names, directories, collections, progress, deviation
and replan policy. Center owns map production and desktop workflows. TMAP owns
the route codec, checks, bounded processing and retrieval. No Trail Mate firmware
integration is included here.

See the [wire format](ROUTE_ARCHIVE_FORMAT.md),
[20 km evidence and reproduction commands](ROUTE_ARCHIVE_EVIDENCE.md),
[export/parity probe](../examples/route_archive_probe.cpp), and
[independent external-memory reader probe](../examples/route_archive_read_probe.cpp).
