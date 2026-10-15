# Real Center-produced navigation resource evidence

## Current profile-aware reader remeasurement

The current Center/SDK path was rerun with explicit foot-profile section 1113.
Packages are 1,765,376 B (4 KiB pages) and 1,783,808 B (1 KiB pages).
The reader caches the validated profile by package generation. A first attempt
to reread that section at every search step increased I/O and failed the
alternating-Busy diagnostic progress limit; the uncached reports are retained
as `real-navigation-monaco-profile-uncached-report.json` and
`real-navigation-monaco-profile-small-uncached-report.json`. They are failure/
regression evidence, not accepted resource targets.

Current reports use three capacities (2,048/10,000/16,384), four algorithms,
eight queries: 96 cases each for 4 KiB, 1 KiB and alternating Busy. ALT completes
8/8 at 2,048; all algorithms complete 8/8 at 10,000 and 16,384. Normal and Busy
results, centimeter costs, states and output edge counts agree in all 96 cases.

| Method / slots | Current host payload 4 KiB / 1 KiB | Read bytes 4 KiB | Read bytes 1 KiB |
|---|---:|---:|---:|
| ALT4 / 2,048 | 92,232 / 86,088 B | 275,392–19,477,440 | 83,904–5,223,360 |
| Dijkstra / 10,000 | 410,312 / 404,168 B | 3,146,688–76,465,088 | 874,432–20,383,680 |
| Floating FIFO / 10,000 | 410,312 / 404,168 B | 6,685,632–307,872,704 | 1,910,720–88,053,696 |
| Integer FIFO / 10,000 | 330,152 / 324,008 B | 6,685,632–307,856,320 | 1,910,720–88,049,600 |

Raw evidence: `real-navigation-monaco-profile-4k-report.json`,
`real-navigation-monaco-profile-1k-report.json`, and
`real-navigation-monaco-profile-busy-report.json`. The original measurements
below remain a dated pre-profile baseline. Current figures retain the same
host-payload and hardware limitations described below.

Measured 2026-10-14. Trail Mate Center's new `TmapTool navigation` command and
Core SDK integration produced two actual foot-profile TMAP outdoor v2 draft
packages from Geofabrik's archived
[Monaco 2026-10-13 PBF](https://download.geofabrik.de/europe/monaco-261013.osm.pbf).
Source: 692,392 B; SHA-256
`32c409ec20a3449be6450e7e615a9d575dd7808e7326a8d087d3f4128b394a05`.
Artifacts contain graph.json, sections.json and a source/tool/package manifest.
This is the real producer-to-C++-reader path, not a generated test maze.

Both maps have 4,681 nodes, 11,722 directed edges and four ALT landmarks.
The largest of 95 weak components has 4,514 nodes. The 4 KiB package is
1,761,280 B; the 1 KiB package is 1,782,784 B. More page headers make the compact
file slightly larger. No DEM, contours or climbing weights were supplied.
Profile selection retains all supported foot rules and rejects unsupported
applicable conditions; bicycle-only restrictions do not constrain foot graphs.
The default both-mode profile still rejects Monaco's incomplete relevant vehicle
restriction, so the foot result is not evidence of complete bicycle support.

tools/benchmark_real_navigation.py verifies the manifest digest and reproduces
the graph document into identical package bytes, then runs the actual C++
tmap_navigation_resource_probe. It samples node-ID quantiles in the largest
weak component: four endpoint pairs in both directions, eight directed queries.
Successful route lengths are 212.03–1,678.43 m. Endpoints were not selected by
looking for successful small-capacity searches. All failures are retained.

Cold library caches per query; package open/graph initialization included in
I/O counts. 16,384-slot Dijkstra finishes all queries and is the exact cost
baseline. Successful results from every method/capacity match its centimeter
cost. Six capacities and four algorithms yield 192 cases per page profile.

| Method | 128 slots | 512 slots | 2,048 slots | 8,192 slots | 10,000 slots | 16,384 slots |
|---|---:|---:|---:|---:|---:|---:|
| Dijkstra | 0/8 | 0/8 | 2/8 | 7/8 | 8/8 | 8/8 |
| ALT4 | 2/8 | 4/8 | 8/8 | 8/8 | 8/8 | 8/8 |
| Floating FIFO | 0/8 | 0/8 | 2/8 | 6/8 | 8/8 | 8/8 |
| Integer FIFO | 0/8 | 0/8 | 2/8 | 6/8 | 8/8 | 8/8 |

The completion counts are identical in both page profiles. The following tested
configurations finish all eight, without claiming minimum capacities:

| Method / slots | Host payload, 4 KiB | Host payload, 1 KiB | Read bytes range, 4 KiB | Read bytes range, 1 KiB |
|---|---:|---:|---:|---:|
| Dijkstra / 10,000 | 410,264 B | 404,120 B | 3,142,528–76,460,928 | 873,344–20,382,592 |
| ALT4 / 2,048 | 92,184 B | 86,040 B | 271,232–19,473,280 | 82,816–5,222,272 |
| Floating FIFO / 10,000 | 410,264 B | 404,120 B | 6,681,472–307,868,544 | 1,909,632–88,052,608 |
| Integer FIFO / 10,000 | 330,104 B | 323,960 B | 6,681,472–307,852,160 | 1,909,632–88,048,512 |

Every query actually allocates only its configured state/queue/output capacity.
Payload includes PackageWorkspace, OutdoorPackage, GraphReader, search cursor,
labels, queue and uint32 route-output array using the host ABI's sizeof. It
excludes allocator metadata, file adapter/query input, ownership wrappers and
task stacks. It is not an MCU RAM/PSRAM peak. Source/probe/tool hashes and all
per-query reads/states/expansions/results are retained in:

- real-navigation-monaco-report.json
- real-navigation-monaco-small-report.json
- real-navigation-monaco-busy-report.json

Busy every second read preserves outcomes, costs and retained-state counts in
96 compared cases (2,048/10,000/16,384 slots). Native and Busy profiles are not
SD latency measurements; desktop OS caching makes wall time inappropriate as
device evidence. No hardware CPU, power or PSRAM claim follows from byte counts.

This real workload reinforces resource-first selection: integer representation
saves same-capacity storage, but FIFO often repeats far more work/reads. Optional
ALT reduces usable search storage and I/O here, with landmark storage cost
recorded in section 1112 (149,792 logical bytes). Sparse A*/zero-index baseline
remains the default framework; integer FIFO is not promoted merely for removing
floating cost arithmetic. Much longer/mountain queries, real DEM/climbing,
conditional/via-way rules, GUI production and board measurements remain open.
