# Persistent-route resource evidence

The primary acceptance routes are **20.779 km in London** and **20.267 km in
Kunming**, using maps produced by TrailMateCenter Core/TmapTool and delivered in
the SD directory layout. Shorter routes remain regression controls. No map was
assembled or substituted by this test runner.

Raw, machine-readable results are in
[`route-archive-real-evidence.json`](route-archive-real-evidence.json). The runner
streams SHA-256 over both real map files and requires exact agreement with
Center's delivery manifest before running any route query.

## Real inputs and reproducible requests

| Map | File bytes | SHA-256 |
| --- | ---: | --- |
| `maps/tmap/osm/london-osm-z13-17.tmap` | 10,316,398,592 | `122b58d9ae7eb124f5f67d23dba204deb53506fd3ede17cdafb2022a814e75a1` |
| `maps/tmap/osm/kunming-osm-z13-16.tmap` | 9,443,880,960 | `37b8e46092acf53e57a3b75acbea1b092bebad861a8948fd31a774ca666086f4` |

London uses coordinate endpoints `(51.5009000, -0.1245000)` and
`(51.4036000, -0.3377000)`, with 50 m snapping. Kunming uses graph nodes 15,664
and 44,486, whose coordinates are `(25.0957587, 102.7707160)` and
`(24.9628584, 102.6651724)`. These are real connected graph routes, not repeated
short routes or synthetic geometry stretched to reach 20 km.

Both use foot mode, integer ALT, zero ascent penalty and a 200,000-expansion
budget across endpoint candidates. The result is compared against the existing
array planner for every clipped leg and for the entire generated geometry.
Search storage is then released before archive export. The map and graph are
closed before the saved file is reopened and read.

## Primary results

Sizes use the measured 32-bit MinGW GCC 9.2 ABI, MinSizeRel, default 4 KiB/two-page
outdoor workspace and 64-bit file offsets. They are typed library storage, not a
whole firmware RAM measurement.

The parity harness intentionally retains a reference-only array while comparing
the planners. That test storage is excluded from the production-phase payload
figures; they are not host-process heap peaks. The independent read-only program
has no such reference array and measures its actual provider allocations.

| Metric | London | Kunming |
| --- | ---: | ---: |
| Geometry length | 20,778.62 m | 20,266.91 m |
| Directed clipped legs | 550 | 200 |
| Unique geometry points | 1,171 | 351 |
| Geometry pages | 30 | 9 |
| Saved file | 43,776 B | 14,592 B |
| Search state capacity | 65,536 | 49,152 |
| Peak occupied search labels | 55,391 | 35,113 |
| Configured search library payload | 2,108,244 B | 1,583,956 B |
| Export library payload, after search release | 11,800 B | 11,800 B |
| Archive read payload, after map/graph release | 1,768 B | 1,768 B |
| Header-only open | 192 B read | 192 B read |
| Complete geometry query | 34,560 B read | 10,496 B read |
| Small midpoint spatial query | 1,728 B read | 1,600 B read |
| Midpoint distance window, +/-10 m | 1,728 B read | 1,600 B read |

The archive's length is derived from rounded geometry segments. Its stored cost
uses graph edge costs; the centimeter-level differences are preserved, not
silently treated as identical measurements.

The low read payload does not make long-distance search a few-KiB operation.
London fails with both 32,768 and 49,152 states. Kunming fails with 32,768 states.
The short London control also fails with 256 states. These explicit failures are
retained in the raw report. There is no automatic expansion, hidden external
search store, or silent lower-quality route fallback.

## Zoom and position windows

Each map is tested at the actual route start, midpoint and end in a 320 x 240
Web Mercator viewport, at z12, z14, z16 and z18. Each query starts with a cold
library page cache. Header reads are reported separately. The table lists
successful logical I/O bytes for the query, including its index reads.

| Position | Zoom | London read bytes | Kunming read bytes |
| --- | ---: | ---: | ---: |
| Start | 12 | 10,816 | 3,776 |
| Start | 14 | 2,752 | 2,624 |
| Start | 16 | 1,728 | 1,600 |
| Start | 18 | 1,728 | 1,600 |
| Midpoint | 12 | 15,424 | 7,104 |
| Midpoint | 14 | 3,904 | 3,904 |
| Midpoint | 16 | 2,880 | 2,624 |
| Midpoint | 18 | 1,728 | 1,600 |
| End | 12 | 8,576 | 3,840 |
| End | 14 | 2,688 | 1,408 |
| End | 16 | 1,664 | 1,408 |
| End | 18 | 1,664 | 1,408 |

For every window, a separate full-route scan projects and clips all segments.
Its drawn-segment count must equal the indexed window's drawn count. Oracle
reads are outside the measured window query. The raw report retains candidate,
tested and drawn counts, so page-level read amplification is visible.

A smaller two-page Kunming regression reads both pages for its small viewport:
their bounds overlap that view. Spatial indexing does not guarantee fewer reads
for every route or view. A whole-route overview may need every page. The workspace
and traversal stack remain fixed in all of these cases.

## External-first ownership measured independently

`tmap_route_archive_read_probe` opens only the completed route file. It has no
map, graph, labels, queue or complete geometry array. An instrumented provider
accepts external allocations and refuses internal allocations.

| Allocation | Bytes |
| --- | ---: |
| Archive workspace | 1,216 |
| Reader control | 160 |
| Query cursor | 392 |
| Measured provider peak | **1,768** |
| Provider live bytes after owner destruction | **0** |
| External allocation requests | **3** |
| Internal allocation requests | **0** |

Three owner controls add 36 B and the provider control adds 24 B in this ABI.
Allocator bookkeeping, stdio/SD-driver buffers, task stack and display state are
not included. When external allocation is deliberately refused, the result is
`BudgetExceeded`, with one external request, zero internal requests and zero
live bytes. This verifies the policy through a provider; it does not measure
physical PSRAM hardware.

The route-only linkage test inspects the independent executable and rejects
retained raster reader, outdoor package, graph, route search, archive writer,
geometry exporter or viewport-conversion implementations. Archive checksums use
a 64-byte table, avoiding a dependency on the raster reader's CRC table.

A compiled comparison with published source `d8fc215` checks the retained array
API's control storage. Prepared/coordinate cursors remain 584/936 B on this ABI.
Array and sink pointers share one slot, with the mode flag in existing padding;
the new output mode does not increase these existing cursor sizes.

## Search and storage costs remain separate

The raw report records search map reads, geometry-export map reads, spool size,
spool writes, archive writes and full validation reads. Search starts with
reopened package/graph state after the array parity control, so its counters
include fresh library initialization. OS caches may remain warm. These are
logical successful I/O bytes, not physical SD transfers or latency measurements.

Long-distance ALT still performs substantial map I/O. This change reduces result
retention and saved-route read memory; it does not establish a low-I/O external
search algorithm. Use the raw counters when judging whether on-device planning
is appropriate for a particular board and storage medium. Smaller devices can
load a route computed on a more capable host.

## Regression and compiler gates

The current host suite contains 40 passing CTest entries, including the installed
consumer, original coordinate/array route tests, ABI checks, route archive
behavior and route-only linkage. The archive regression covers:

- A 5,001-point route spanning more than 100 pages, local and distance windows,
  seam uniqueness and interleaved cursors with different step quanta.
- Repeated crossings/loops, endpoints outside a visible crossing, explicit gaps,
  date-line wrapping and invalid viewport inputs.
- Both clipped endpoint writes under every-second-operation Busy injection,
  complete array/sink leg parity and export after search buffers are freed.
- Busy reads and writes, cancelled queries/validation/export state, closed-reader
  generations, I/O failure, incomplete files, truncation and corrupt CRCs.
- Exact file-budget success, explicit budget exhaustion, map mismatch, checked
  leg reads, full validation and exclusive native-file creation/reopen.
- No implicit C++ heap allocations in guarded core paths; external-first owners
  and refusal without internal fallback.

Real-map acceptance has 14 cases: ten successes, including both 20 km routes
under normal and every-seventh-operation Busy I/O, plus four retained capacity
failures. Busy and normal output SHA-256 must match. The compact archive CRC
implementation must also preserve the previously verified archive bytes.

Core-only builds disable desktop adapters, examples, tests, exceptions and RTTI.
ESP32-S3 cross-compilation and compiler stack reports are checked at the
repository's 240-byte individual-frame limit; host reports use 256 bytes. Exact
gate output is retained in [`route-archive-validation.json`](route-archive-validation.json).
Compiler frames are not task-stack watermarks. Physical MCU RAM/PSRAM peaks,
SD latency, energy use and framebuffer cost still require board measurements.

## Reproduce

Build the library and both probes, then use a Center-produced SD root and its
delivery manifest. The output directory is test storage, not a map producer.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DTMAP_EMBEDDED_CHECKS=ON
cmake --build build
ctest --test-dir build --output-on-failure
python tools/route_archive_evidence.py \
  --probe build/tmap_route_archive_probe \
  --read-probe build/tmap_route_archive_read_probe \
  --sd-root /path/to/center/sd-output \
  --manifest /path/to/center/sd-delivery-manifest.json \
  --output-dir build/route-archive-real \
  --report build/route-archive-real-evidence.json
```

Windows executables have an `.exe` suffix. `--check-previous` additionally requires
output-byte parity with a preceding verified report at the same report path.
The recorded node IDs belong to the exact input builds above; new source maps
need newly selected acceptance endpoints and source hashes.

See [the API and responsibility split](PERSISTENT_ROUTES.md) and
[wire format](ROUTE_ARCHIVE_FORMAT.md). Trail Mate firmware remains unchanged.
