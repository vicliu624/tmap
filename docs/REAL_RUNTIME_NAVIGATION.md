# Real conditional navigation through Center

This test uses the original Geofabrik Liechtenstein 2026-10-13 PBF, without
inserting or changing OSM tags. Source:
[official archived PBF](https://download.geofabrik.de/europe/liechtenstein-261013.osm.pbf).
Source bytes: 3,469,995; SHA-256:
`07f3884fd1dbe647d6ff68ec3fc66725a77beb4b20eab792431fc4bc9ce8c97e`.
The downloaded MD5 also matches the archive's published checksum.

Center's CLI invokes the SDK importer/producer with Foot, runtime conditions,
1,024-byte integrity pages and two ALT landmarks. The resulting package is
8,091,648 bytes, 20,432 nodes and 51,736 directed edges. SHA-256:
`657ee1cec1f9a03aff3517d7d59fdb4900c4c4c726a559e758b8538ff0050162`.
No DEM was supplied: this experiment measures navigation and live road rules,
not simultaneous contour rendering or elevation profiling.

All 36 represented conditional directed edges from ten source ways are checked
at January/June 07:00 and 12:00 with explicit +01:00/+02:00 offsets. The SDK
does not infer a timezone or daylight saving. All 144 edge checks agree with
the host snapshot policy. There are 64 closed-edge observations. Rush-hour
`destination` access is conservatively excluded; paths with explicit `foot=yes`
remain available even while their generic seasonal `access=no` applies.
Raw relation access polygons are not road/turn restrictions and are outside
this test's routing scope. The source inventory must not be mistaken for a
promise that every condition on every OSM entity is enforced.

One real segment per source way and direction is used for route endpoints,
at all four instants and with all four integer algorithms. Each of the 320
queries runs a fresh probe process/package/cache with every other read returning
Busy. Search and output each retain 32 slots, and ALT workspace is retained
for every algorithm so these figures do not imply minimal per-algorithm budgets.

| Completed result | Queries |
| --- | ---: |
| Success | 176 |
| No path | 40 |
| Explicit BudgetExceeded | 104 |

Eight successful queries take a multi-edge detour. All successful paths are
checked for continuity and currently permitted road access. Completed outcomes
are checked against full-graph host reachability; this is not an independent
shortest-path/cost oracle. Capacity failures remain in the report. A small
workspace can inspect an entire large map without supporting arbitrary routing
queries over that map.

| Retained typed payload, current host ABI | Default cache profile | Compact cache profile |
| --- | ---: | ---: |
| Package + graph + road rules + one GraphEdge output | 10,641 B | 4,497 B |
| Integer routing, 32 state/output slots, retained ALT workspace | 12,053 B | 5,909 B |

The direct-edge output is included explicitly in the resource calculation.
Cold edge checks take 14–16 file-read attempts; route checks take 18–438.
These counts include deliberately Busy attempts and are not SD timing results.
Typed payload sums include caller-owned buffers and controls, but exclude host
probe ownership pointers, allocator metadata, native file adapter, stdio,
task stacks, display/GPS and other application services. These are current
host ABI measurements, not board RAM/PSRAM peaks. Core ownership remains
caller-controlled; the explicit allocator helpers prefer external RAM and
do not silently fall back when an available external allocator fails.

Reports: [default](real-runtime-native-evidence.json),
[compact](real-runtime-small-evidence.json), and
[raw source inventory](real-liechtenstein-condition-inventory.json). Each
benchmark records source/package/graph/manifest/probe hashes and individual
outcomes. Center's artifact contains its producer manifest and tool digests.
Reproduce with `tools/benchmark_real_runtime.py ARTIFACT PROBE --output REPORT`.
Use the artifact produced by Center, not a rewritten graph fixture.

Default and compact CTest suites both pass 31/31, including installed consumer,
storage ABI, stack budgets and continuation tests. Updated core cross-builds
also succeed for ESP32-S3 and ESP32-C3. No physical MCU, SD, power, complete
call-chain stack watermark or final linked firmware Flash measurement is claimed.
