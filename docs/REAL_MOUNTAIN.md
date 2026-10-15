# Real mountain navigation and contour composition

## Shared bidirectional terrain model follow-up

### Integer route statistics on actual search output

The resource probe's `integer-stats` mode retains the actual integer route edge
array and runs bounded integer length/gain/loss aggregation. Eight real node
queries and eight alternating-Busy repeats on the shared-model package pass.
Each cost equals length_cm + 5 × ascent_cm, reverse gain/loss exchanges exactly,
and Busy preserves result/totals/states. Raw report:
`real-everest-integer-statistics-report.json`, including package/probe digests.
This is consistency evidence on actual search output, not independent survey
verification or a new global-optimality proof.

At 128 states, retained library host payload is 8,240 B including the 112-byte
statistics cursor and 24-byte output, compared with 8,104 B without statistics.
These routes incurred zero additional physical read attempts during aggregation
because their edge records remained cached; larger graphs may require reads.
For the longest forward/reverse pair, distance is 1,170,925 cm, gain/loss
3,420/324,074 cm forward and 324,074/3,420 cm reverse. The small graph, host ABI
and physical-device limitations above remain unchanged.

The producer now samples each canonical road geometry once. Reverse cumulative
distance/gain/loss is derived algebraically, exchanging gain and loss; cached
profile descriptors refer to already packed bytes rather than retaining another
full regional sample table. MCU format, RAM and reader code are unchanged.
Both directed arrays are still stored, so this change saves host DEM computation
and removes directional phase differences, not half the device file size.

Center generated `everest-real-complete-dem-1k-alt4-shared-terrain-v1` and the
four-case coordinate oracle passed again. With the report's identical endpoints,
length is 5,449.07711006 m in both directions, forward ascent/descent
779.20/9.77 m and reverse 9.77/779.20 m. Symmetry passes within 0.05 m.
The former independently sampled forward profile gave 783.58/11.22 m for these
endpoints. Shared interpolation is internally consistent; it is still not
survey-grade elevation accuracy. Current raw evidence is
`real-everest-shared-terrain-coordinate-report.json`; older reports retain the
previous production model for comparison. Explicitly declared topology epochs
were changed for this regenerated artifact.

Writer tests verify a single DEM sampling call for reciprocal geometry, reversed
sample tuples, involution and exchanged totals. Default/small CTest: 29/29 each.

## Complete-coverage climb-weighted follow-up

The same HGT source was recropped to 86.86,27.96–86.94,27.9997 to cover all
intermediate points of the downloaded ways. Center produced
`everest-real-complete-dem-1k-alt4/navigation.tmap`, 1,363,968 B, SHA-256
6a27144c5da1f0a698c48aa0d784d722589ee2e7e456326679099546fe54e9a3.
All 10 directed edges now have complete sampled elevation. No source roads
were dropped to make coverage pass; the source XML is identical.

Resource probe and benchmark accept `--ascent-penalty` (integer 0..1000).
This follow-up uses 5: cost_cm = length_cm + 5 × ascent_cm, identically for
floating algorithms and exact integer FIFO. Four algorithms, two capacities
(128/512) and eight endpoints give 64 cases per mode: length-only, weighted,
and weighted alternating-Busy, 192 total. Every case finishes. Successful
costs also match an independent host integer Bellman relaxation over packed
directed lengths/ascent, not just another library algorithm. The Bellman
oracle is limited to small graphs without turn restrictions; it validates
search costs, not independent geographic elevation accuracy.

| Query | Length-only cost cm | Weighted cost cm |
|---|---:|---:|
| 0 | 1,170,925 | 1,187,815 |
| 1 | 539,368 | 551,373 |
| 2 | 616,362 | 743,287 |
| 3 | 642,893 | 769,818 |
| 4 | 642,893 | 1,793,658 |
| 5 | 616,362 | 1,757,527 |
| 6 | 539,368 | 1,661,988 |
| 7 | 1,170,925 | 2,792,075 |

Reverse routes can have the same distance but very different uphill cost.
All 64 weighted Busy cases preserve results, costs, states and edge counts.
At 128 states weighted Dijkstra reads 5,696 B per query; terrain sampling is
already packed into edges, so runtime search does not reread DEM cells.
Host payload remains 9,288 B floating / 8,104 B integer for this configuration.
Raw reports: `real-navigation-everest-length-report.json`,
`real-navigation-everest-weighted-report.json`, and
`real-navigation-everest-weighted-busy-report.json`.
Datum/survey, sparse-graph and hardware limitations below still apply. This
is real climb-weighted node routing evidence; coordinate clipping/terrain
profiles and wider mountain route choice need separate validation.

## Original limited-coverage composition

### Real clipped coordinate follow-up

`benchmark_coordinate_elevation.py` verifies the Center package/document identity,
then selects shape points inside two roads (not graph endpoints), runs both
directions with penalties 0 and 5, and independently recomputes clipped length,
ascent and descent from packed shapes/profile records. Four cases pass at a
0.05 m arithmetic tolerance. Search cost agrees with length + penalty × ascent.
The independent oracle validates aggregation of shared packed inputs; it does
not independently verify DEM elevations or prove coordinate-route optimality.
Raw evidence: `real-everest-coordinate-elevation-report.json`.

At penalty 5, the automated report's forward length is 5,449.0771 m, ascent
783.58 m, descent 11.22 m; reverse length is 5,449.0771 m, ascent 9.77 m,
descent 779.20 m. Directional
sampling/threshold phase can produce small differences between forward ascent
and reverse descent; these are producer-profile totals, not exact surveyed
terrain gain. Forward/reverse reads are 110,144 / 93,760 B. The actual configured
128-state/128-leg coordinate search plus retained elevation cursor/output has
12,624 B host payload, excluding allocator, file adapter, task stack and app.
This is a tiny sparse route, not a full-region MCU acceptance result.

Retrieved 2026-10-14 through the official OSM map endpoint:
https://api.openstreetmap.org/api/0.6/map?bbox=86.9,27.96,86.94,27.99
XML bytes: 764,088; SHA-256:
3540b852266a4f0571f73692ffcd76d875c84d3ca3dd97b4b1c6d1a0ccefdac6.
OSM attribution/license applies. Center's historically named PbfPath also
accepts .osm XML through SDK auto detection; this source was not converted to PBF.

The outdoor document uses the previously verified real N27E086 HGT crop,
86.90,27.96–86.94,27.99, 145×109 samples, nominal resolution 31 m.
Source: https://s3.amazonaws.com/elevation-tiles-prod/skadi/N27/N27E086.hgt.gz
Vertical datum remains explicitly unknown. Center's manifest retains the XML,
outdoor-document and producer hashes. No synthetic elevation is used here.

Center produced `everest-real-1k-alt4/navigation.tmap`, 490,496 bytes,
SHA-256 9e44588d5161a5de95dc6ca4a9295bf545aff44117752a4a07bab68899bb757e.
It combines navigation, elevation and vector contours. The graph is very small:
5 junction/end nodes and 10 directed edges. Long ways retain intermediate shape
points and can extend beyond the requested bounding box. Only 6/10 edges have
complete elevation coverage; missing heights are not invented. This is not yet
evidence of complete climb-weighted routing across this extract.

`real-navigation-everest-report.json` preserves 160 cases: four algorithms,
five capacities, eight distinct directed endpoint pairs (four pairs reversed).
All complete even at the smallest tested 128 states and match the large Dijkstra
cost baseline. Routes span 5,393.68–11,709.25 m. This is a sparse mountain sample,
not proof of large-region memory needs or a stronger algorithm ranking than Monaco.
At 128 states host payload is 9,288 B for floating algorithms, 8,104 B for integer
FIFO. Dijkstra/FIFO/integer read 5,696 B per query; ALT reads 8,768–12,864 B.
ALT's table overhead is unnecessary for this tiny graph, unlike the urban sample.

On the same package, a 512-segment buffer and 320×240 viewport at 86.925,27.98
produced the following bounded density results; raw data is in
`real-everest-contour-report.json`:

| Zoom | Segments | Final interval | Density retries | Reads | Read bytes |
|---|---:|---:|---:|---:|---:|
| 12 | 256 | 200 m | 1 | 65 | 65,088 |
| 14 | 348 | 200 m | 3 | 95 | 95,808 |
| 16 | 244 | 50 m | 2 | 142 | 143,936 |
| 18 | 124 | 10 m | 0 | 41 | 40,512 |

Host payload printed by the contour probe: package workspace 3,360 B,
render cursor 568 B, segment buffer 12,288 B. These are components, not complete
application RAM. Dense slopes deliberately increase contour intervals rather
than overflow the bounded buffer; a wider buffer changes that tradeoff.
Elevation at the viewport center is 8,089.91 m, datum=unknown; no external
survey accuracy validation is claimed.

No physical MCU RAM/PSRAM peak, task-stack watermark, SD latency or power was
measured. GUI integration, real climb-weighted route validation and broader
mountain samples remain required. This report proves the real OSM/HGT → Center
→ combined TMAP → C++ navigation/contour path with explicit limitations.
