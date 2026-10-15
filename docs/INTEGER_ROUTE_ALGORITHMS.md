# Integer queue and Dijkstra resource comparison

The optional queue-free `LabelScanDijkstra` now removes four bytes per search
slot, including explicit PSRAM ownership and bounded label scans. See
[current actual-map tradeoffs](LABEL_SCAN_DIJKSTRA.md) for equal-capacity exact
comparisons, CPU/work costs and ALT's smaller tested all-query configuration.
The queued algorithm measurements below remain historical evidence for those
specific package/probe configurations.

## Heap priority without larger labels

IntegerRouteAlgorithm.HeapDijkstra is now implemented. IntegerRouteLabel uses
its final 32-bit word for queue_position (UINT32_MAX means absent), replacing
the former queued flag and padding. sizeof remains 24 B; the existing 4-byte
queue array becomes an indexed binary heap. Decrease-key updates positions
in place, without a second array or duplicate entries. FIFO/linear modes use
the same word as a membership marker. Heap sift operations are bounded by
32 levels for the accepted uint32 capacity range; they execute inside a work
item, so work budget is not an exact comparison/CPU-time bound.

The same 24 Bellman cases now exercise both linear and heap Dijkstra, with
one-work slices and alternating Busy. Tests also inspect heap order and each
label's queue position after every step, covering insert/decrease/pop behavior.
S3/C3 core builds pass; C3 route step frame is 96 B. Arrays/cursor sizes and
no-allocation behavior are unchanged by the heap addition.

Real Monaco follow-up: 80 normal and 80 Busy cases, with costs/states/expansions/
output counts agreeing between normal and Busy. At 10,000 states:

| Variant | Host payload | Continuation calls | Read bytes |
|---|---:|---:|---:|
| Integer linear Dijkstra | 330,168 B | 4,946–226,969 | 3,146,688–76,448,704 |
| Integer heap Dijkstra | 330,168 B | 1,750–8,188 | 3,146,688–76,448,704 |
| Floating Dijkstra | 410,312 B | 1,750–8,188 | 3,146,688–76,465,088 |

Heap resolves the linear-scan continuation cost without additional state RAM.
These calls are scheduling evidence, not equal CPU cycles across implementations.
ALT still needs only 2,048 states for all queries in this sample; integer heap
needs 10,000 among tested capacities. Raw evidence: real-monaco-integer-heap-report.json
and real-monaco-integer-heap-busy-report.json. Subsequent integer ALT evidence is in INTEGER_ALT.md.

## Earlier FIFO and linear comparison

IntegerRouteOptions.algorithm selects QueueRelaxation (existing default),
Dijkstra (linear minimum) or HeapDijkstra. All retain exact rational length/ascent costs and arrival-edge states,
including mode-specific no/only turns. Labels remain 24 B and queue entries 4 B.
No heap-position table or growable container is added.

Dijkstra finds the least-cost queued label by a resumable linear scan, one
comparison per work item. Ties use arrival keys. The queue is unchanged while
scanning, and the selected head survives Busy node reads without rescanning.
Positive/nonnegative costs permit completion at the first minimum-cost target;
FIFO continues draining with incumbent pruning. Cancel/package replacement and
storage/expansion budgets remain explicit. The new selection continuation and
algorithm option add 16 B to the tested host integer cursor; arrays are unchanged.

This is a low-memory priority choice, not a claim that linear scanning minimizes
CPU operations. It can compare many retained states, unlike a binary heap.
Its purpose is reducing repeated graph expansion/I/O without adding per-state
heap metadata. No MCU time or energy measurement was taken. The default remains
FIFO until applications choose a policy based on their workload.

Real Monaco 4 KiB evidence: four algorithms × two capacities × eight queries,
64 cases plus 64 alternating-Busy repeats. Costs agree with the 10,000-state
floating Dijkstra baseline; normal/Busy costs, results, retained states,
expansions and output counts match. 2,048 states succeed for 2/8 integer-Dijkstra
queries versus 8/8 ALT. 10,000 states complete all eight.

| Tested all-query configuration | Host payload | Expansions range | Read bytes range |
|---|---:|---:|---:|
| Floating Dijkstra / 10,000 | 410,312 B | 440–9,632 | 3,146,688–76,465,088 |
| Integer FIFO / 10,000 | 330,168 B | 1,023–49,130 | 6,685,632–307,856,320 |
| Integer Dijkstra / 10,000 | 330,168 B | 440–9,632 | 3,146,688–76,448,704 |
| ALT4 / 2,048 | 92,232 B | 15–1,672 | 275,392–19,477,440 |

Raw reports: real-monaco-integer-dijkstra-report.json and
real-monaco-integer-dijkstra-busy-report.json. Payload excludes task stack,
adapter, allocator and application storage; this is not MCU RAM/PSRAM peak.
The integer implementation also passes 24 independent Bellman reference cases
with fractional ascent, both travel modes and turn restrictions under one-work
slices and alternating Busy. S3/C3 builds pass; C3 step frame is 112 B.
Integer ALT is implemented and measured in INTEGER_ALT.md; integer coordinate-endpoint costs remain unimplemented.

## Continuation work evidence

The resource probe now records `continuation_calls`, counting package open,
route begin and search step invocations, including initialization. Search steps
use a seven-work budget. These counts are reproducible scheduling work evidence,
not exact instruction counts, CPU cycles or time; algorithms do different work
inside each item. No diagnostic field was added to MCU cursors.

On the same real queries and all-query-success capacities above:

| Algorithm | Continuation calls range |
|---|---:|
| Floating Dijkstra / 10,000 | 1,750–8,188 |
| ALT4 / 2,048 | 307–1,474 |
| Integer FIFO / 10,000 | 2,187–36,065 |
| Integer linear Dijkstra / 10,000 | 4,946–226,969 |

The linear variant reduces graph I/O but substantially increases continuation
work in this sample. It remains an explicit option, not a default recommendation
for low total computation. The next integer priority implementation should
evaluate heap-position storage against label padding before adding any arrays.
Raw evidence: `real-monaco-integer-dijkstra-work-report.json`, 64 cases with
costs still compared against full-capacity Dijkstra.
