# Routing research and measured implementation

Reviewed 2026-10-14. This is a focused primary-source review, not a reconstruction
of every year's winner. Competition ranking does not identify its search code.

Resource priority: reserved internal RAM/PSRAM, stack, disk/Flash, total
calculation and physical I/O precede wall-clock speed. A faster implementation
is not a default choice if its resource tradeoff is worse. Lower occupied state
count in equal fixed arrays is not a reduction of reserved RAM: a smaller array
must actually be configured and shown to succeed. Repeated computation is also
a resource cost, so low-stack IDA*/reconstruction cannot be declared superior
without measuring its repeated expansions and reads. All results are per workload.
The optional queue-free label-scan implementation now provides an actual example:
it saves 40,000 B at 10,000 states on Monaco while performing substantially more
scans. Integer ALT still completes all eight queries with 2,048 states and a
smaller total payload. See LABEL_SCAN_DIJKSTRA.md; removing an auxiliary array
alone does not establish an overall resource winner.

## Maze-robot lessons

Peter Harrison describes backward flood/Bellman distance values in
[Solving the maze](https://micromouseonline.com/micromouse-book/mazes-and-maze-solving/solving-the-maze/),
including why a shortest route need not be physically fastest. His
[queue-based flood](https://micromouseonline.com/micromouse-book/mazes-and-maze-solving/faster-maze-solving/)
trades explicit small memory for avoiding repeated scans.
[MINOS 2009](https://micromouseonline.com/2009/04/26/minos-2009/),
[2018 half-size presentation](https://micromouseonline.com/download/MINOS18-Halfsize-Micromouse_presentation_final.pdf)
and [2021 UKMARS mazerunner](https://ukmars.org/2021/05/ukmarsbot-mazerunner/)
provide technical material across years. None proves a universal winning method
or speed on TMAP storage.

Transferable ideas: reuse distance information, bounded frontiers, arrival
direction in costs/restrictions, and separating discovery from execution.
TMAP already models arrival edges. Uniform FIFO flood/BFS cannot minimize
arbitrary distance/ascent weights; wall following cannot guarantee an optimum.
Small uniform mazes and large directed paged networks are different workloads.

## Exact road and robot planning

[Goldberg/Harrelson ALT](https://www.microsoft.com/en-us/research/publication/computing-the-shortest-path-a-search-meets-graph-theory/)
combines A* with directed landmark lower bounds. It needs preprocessing/storage,
so expansion savings must be compared with media reads.
[Customizable Contraction Hierarchies](https://publikationen.bibliothek.kit.edu/1000056544)
separate topology preprocessing and weight customization. They are important for
large regions; turn states, climbing preferences, shortcuts and unpacking need
explicit resource/format design.
[D* Lite](https://publications.ri.cmu.edu/d-lite) reuses incremental search state
for related replanning tasks. It is relevant to changing closures but retained
state is a cost; static maps do not automatically benefit. Bidirectional search
needs reverse access and correct meeting conditions for directed turn states.

## Implemented increment: optional directed ALT

### Requested six-method assessment

| Method | Role | Resource assessment for TMAP |
|---|---|---|
| Flood Fill | Unit-weight target distance field | Compact integers on small grids; full fields scale with all states and plain FIFO cannot minimize arbitrary road weights |
| Modified Flood Fill | Local field repair, varying formulations | Reuse can save work, but weighted FIFO revisits states; disconnected updates require careful invalidation |
| Adachi 0..3 | Unknown-cell exploration order and reversal/refinement | Exploration policies, not replacements for weighted shortest paths on known maps |
| Dijkstra | Nonnegative weighted exact baseline | No lower-bound table; priority operations and broad searches cost states/I/O |
| A* | Informed weighted search with proved lower bounds | Selected framework; optional bounds must justify their memory/storage cost |
| Bellman propagation | Repeated cost relaxation | Small arrays/no priority heap possible; whole sweeps can cost many more reads/calculations |

[Harrison's Adachi account](https://micromouseonline.com/2008/05/27/adachi-micromouse-maze-solving-algorithms/)
describes 0 as repeated end-to-end exploration, 1 as targeting unknown candidate
route portions, 2 as reversing once the current direction is established, and
3 as initial exploration plus refinement. He labels this his interpretation and
is uncertain about 3; it is not treated as a precise 1984 specification. A
[2015 competitor implementation](https://titech-ssr.blog.jp/archives/1046800312.html)
separates exploration of promising unknown walls from final physical-route choice.

benchmark_maze_algorithms.py and maze-algorithm-report.json record primitive
operation counts on known 16x16 node graphs. Declared buffer models: compact
uint16 flood 7 B/state; general weighted FIFO 17; Bellman 12; TMAP-style sparse
search 36. These are not MCU RAM/Python RSS measurements. Graph, cache, controls,
sparse capacity rounding and Python lazy-heap duplicates are excluded. Weighted
FIFO is one relaxation formulation, not every Modified Flood Fill variant.

Uniform grid: FIFO scans 960 edges, Bellman 29,760, A* with valid Manhattan bounds
and deeper-g ties 88 (59 discovered nodes). FIFO has the smaller declared full
field. Seeded weighted grid: ordinary flood is inapplicable; weighted FIFO scans
1,544, Dijkstra 946, A* 940, Bellman 29,760. This includes a weak-heuristic case.

The real core implements QueueRelaxation using its existing heap array as a
fixed FIFO. Arrival-edge states respect restrictions/climbing and can reopen.
First goal pop is not a completion proof; drain the frontier with nonnegative
incumbent-cost pruning. All 24 reference cases pass with Busy. Comb: 1,469
expansions, 1,494 states, 3,350,528 read bytes, zero heap comparisons; slightly
more media I/O and no smaller buffers, so not the default improvement there.

Choice: bounded sparse A* framework with zero-heuristic/no-index baseline;
optional ALT if its resource tradeoff is acceptable. Queue relaxation remains
a measured alternative. This is not a universal resource minimum or a demand
that tiny grids adopt the general road-state representation.

RouteAlgorithm::Auto uses ALT when tables exist; otherwise Dijkstra. Explicit
Landmarks returns Missing without tables. Producer JSON graph.landmarks selects
0..8, default 0. Tables contain exact directed centimeter distances on a relaxed
graph that allows all modes and ignores turns/climbing. Relaxation reduces costs,
making its lower bounds admissible for nonnegative restricted/climbing queries.
No unproven geographic-rounding heuristic is used.

Potential: max of 0, d(L,t)-d(L,v), d(v,L)-d(t,L), over available finite pairs.
UINT32_MAX denotes unavailable/overflow. Potential clamps to 31 bits; the high
bit records settled state, retaining a 32-byte label. Improved states can reopen
because disconnected data can yield inconsistent but admissible bounds.
Precomputed storage: 8*K*N logical bytes plus 32-byte policy and page overhead.
Continuation adds 128 B; cache and labels do not grow.

Cold comb fixture: 832 nodes, same package, 4,096-slot arrays, every second read
Busy, identical optimal cost 700.56 m:

| Metric | Dijkstra | ALT, 4 landmarks |
|---|---:|---:|
| Expanded states | 1,469 | 63 |
| Retained states | 1,494 | 189 |
| Read attempts | 1,616 | 388 |
| Successful read bytes | 3,309,568 | 794,624 |
| Label size | 32 B | 32 B |

The same fixture also succeeds with ALT and 256 slots (9,216 B arrays), while
Dijkstra with 256 slots returns BudgetExceeded. This proves a smaller usable
budget for this query, not a universally sufficient 256-slot configuration.

Both match 24 independent Bellman-Ford cases with modes, no/only turns, ascent,
disconnected graphs and Busy. 14 CTest entries pass; S3/C3 frames remain 192/240 B.
Reproduce with stress_fixtures/stress. This is synthetic evidence, not device
latency, real-region superiority or power measurement. CH/D* research is not
an assertion of implementation.

All previously proposed capabilities belong to the current unpublished scope.

## Integer search resource comparison

The optional integer FIFO now minimizes exact rational climbing costs on arrival
edge states. It uses 24-byte labels plus 4-byte queue rows, versus 32+4 in the
original search. In the same cold 832-node comb, 2,048 slots reserved, every
second read Busy, all four methods produce exactly 70,056 cm with zero ascent.
The measurement includes graph initialization after package open; package open
itself is excluded consistently. Reports are integer-resource-report.json and
integer-small-resource-report.json; regenerate with the integer_route CTest.

| Method | Reserved arrays | Cursor | States / expansions | Read attempts / successful bytes (4 KiB pages) |
|---|---:|---:|---:|---:|
| Integer FIFO | 57,344 B | 152 B | 1,494 / 1,469 | 1,636 / 3,350,528 |
| Dijkstra | 73,728 B | 312 B | 1,494 / 1,469 | 1,616 / 3,309,568 |
| ALT, 4 landmarks | 73,728 B | 312 B | 189 / 63 | 388 / 794,624 |
| Original floating FIFO | 73,728 B | 312 B | 1,494 / 1,469 | 1,636 / 3,350,528 |

These arrays use equal reserved capacity to compare representations. They do
not imply ALT needs that capacity: the existing stress test proves ALT succeeds
with 256 slots (9,216 B arrays). The new integer test proves 256 slots fails and
1,494 slots succeeds (41,832 B arrays) on this query. The latter is a tested
capacity, not a universal bound. Cache, graph reader, file/provider, output,
allocator and task stacks are excluded from the table, not from full budgets.

With 1 KiB pages, integer/original FIFO read 1,716,224 B in 3,352 attempts;
Dijkstra 1,590,272 B in 3,106 attempts; ALT 222,208 B in 434 attempts. Shrinking
pages reduces bytes but can increase transactions. No latency/power conclusions
follow without measuring the target storage and provider.

Decision: keep ALT as an optional indexed acceleration and retain the existing
default selection. Integer FIFO saves 22.2% of same-capacity search arrays and
uses a smaller cursor, but has no I/O or expansion advantage over floating FIFO
in this query. It is useful where integer costs and node endpoints suffice;
it is not a resource-wide replacement for sparse ALT. Integer coordinate/ALT
integration and real-region/hardware comparison remain incomplete.

ALT is a completed increment; other capabilities and hardware evidence are
still being worked through. No deferred-release scope substitution is intended.
