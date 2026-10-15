# Integer directed ALT

IntegerRouteAlgorithm.Landmarks reuses directed landmark tables with an integer
heap. Labels remain 24 B, queue entries 4 B. Caller supplies a capacity-sized
uint32 lower_bounds array and IntegerLandmarkWorkspace (read scratch,
goal distances/progress). Only inserted labels load bounds; heap comparisons
perform no I/O. Distance bounds are scaled by exact cost denominator; nonnegative
climb preserves admissibility. Priority saturates without changing path cost.
Missing buffers return Invalid, missing tables Missing, bad dimensions Invalid.
Busy/More preserve bounded progress. No implicit allocation/floating arithmetic.

IntegerLandmarkStorage is an optional explicit provider-backed owner. Default
uses external/PSRAM when available, without internal fallback on failure. Staged
initialization hides partial pointers; either allocation failure rolls both back.
Stop borrowers before cancel/reset. ResourceOptions.integer_landmarks counts
payload/workspace; owner/provider metadata and allocator overhead are additional.

Real Monaco: 96 cases each for 4 KiB, 1 KiB and Busy. Successful costs match
full-capacity Dijkstra. At 512 states both ALT implementations finish 4/8;
at 2,048 both finish 8/8. All 96 normal/Busy results, costs, states, expansions
and output counts match.

| 2,048-state method | Host payload 4 KiB / 1 KiB | Read bytes 4 KiB | Continuation calls |
|---|---:|---:|---:|
| Floating ALT | 92,232 / 86,088 B | 275,392–19,477,440 | 307–1,474 |
| Integer ALT | 84,036 / 77,892 B | 275,392–18,772,928 | 307–1,451 |

Payload includes optional bounds/workspace and configured controls/arrays,
excluding adapter, allocator, wrappers, stack and app. It is not device peak or
a few-KiB full-navigation claim. Reports: real-monaco-integer-alt-report.json,
real-monaco-integer-alt-small-report.json, real-monaco-integer-alt-busy-report.json.

24 independent Bellman cases cover fractional climb, modes/no/only turns,
one-work slices and Busy. Owner tests verify both failure points, external-only
requests. Negative route tests also supply valid ALT buffers to packages without
tables: Missing remains terminal, and edge output reports zero count. Cancelling
while table initialization is incomplete remains Cancelled; a fresh begin resets
the caller landmark stage/count, then matches the independent reference again.

Owner tests also cover hidden partial data and cancellation. Budget tests
include optional storage.
Default/small CTest 29/29 each; S3/C3 builds pass. C3 route frame 128 B; optional
owner initialization frames S3 32 B/C3 64 B (callees excluded). Hardware peaks,
latency/power, integer coordinate costs and complete conditional/via-way support
remain incomplete or unverified.
