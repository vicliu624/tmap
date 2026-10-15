# Prepared integer routes

`beginIntegerPreparedRoute` consumes two caller-owned endpoint candidate sets
prepared by `IntegerEndpointCursor`. It compares a direct same-road interval
with up to four boundary-constrained graph routes, including clipped endpoint
costs. Costs use checked integer arithmetic and the exact rational ascent
coefficient recorded in the candidate sets.

Candidates must belong to the current package generation, travel mode and
ascent coefficient. Departure and arrival roles are checked. Search expansion
limits apply across all candidate searches rather than restarting for each pair.
Cancellation and file Busy responses preserve bounded continuation behavior.

The caller supplies labels, queue and `IntegerRouteLeg` output storage. Middle
legs use `first_cm = 0` and `last_cm = UINT32_MAX` to mean the whole directed
edge; endpoint legs contain explicit centimetre intervals. No additional array
of middle edge IDs is allocated. `IntegerPreparedRouteStorage` provides the
existing staged, external-memory-first ownership policy for these buffers.

Only `integerPreparedRouteResult` returning `Ok` publishes a route. Its pointer,
count and cost outputs are empty while processing or after failure. The caller's
scratch output can already contain a tentative candidate during processing and
must not be consumed until publication.

This API accepts prepared candidates. The [integer coordinate wrapper](INTEGER_COORDINATE_ROUTE.md)
accepts latitude/longitude directly and reuses scratch across phases. The
[maneuver adapter](INTEGER_MANEUVERS.md), geometry and elevation adapters consume
the clipped integer leg representation and are included by resourceBudget.
Preparing endpoints separately requires its own cursor budget. Package cache,
graph reader, candidate sets, coordinator, state arrays and leg output are
included by the corresponding resource options; application and allocator
overhead must be supplied explicitly.

Tests cover direct-route preference, cross-road endpoint concatenation, Busy
continuation, cancellation, coefficient mismatch, insufficient leg capacity,
and the shared expansion limit. These are host correctness checks, not physical
MCU peak stack, PSRAM, storage latency or power measurements.

## Optional geometry adapter

`IntegerGeometryCursor` accepts the published clipped integer legs and streams
E7 drawing points through the existing point visitor. It measures road geometry
in a bounded first pass and locates centimetre endpoints relative to the packed
road length in a second pass. This avoids an additional route-sized point array,
at the cost of extra geometry reads. Drawing conversion uses floating point;
integer search and cost computation do not acquire that dependency.

The cursor and borrowed legs must remain at fixed addresses until completion.
Duplicate junction points are suppressed across legs. Busy retries preserve
scan progress, callback cancellation is terminal, and replacing/closing the
package cancels the operation. Points are streaming output, so callers must
discard a partially drawn route on failure. The adapter does not promise atomic
publication of a full polyline. Zero packed-length or degenerate shapes are
rejected explicitly.

Single-work-unit tests cover forward/reverse clipping, concatenation, Busy,
empty input, invalid intervals and cancellation with heap allocation guarded.
The current native ABI reports a 272-byte geometry cursor, 584-byte prepared
route coordinator and two 112-byte endpoint candidate sets. These are separate
retained controls/outputs, not an all-feature budget. `resourceBudget` uses the
target compiler's `sizeof` values, including the selected cache/state/output
capacities. Default and small-profile CTest each pass all 29 tests.
ESP32-C3 cross-compiler reports 224 bytes for the vertex scanner and 80 bytes
for the coordinator frame after splitting the stages. These are individual
compiler stack frames, not peak call-chain or task-stack measurements.

## Clipped integer elevation statistics

`beginIntegerLegElevation` and `integerLegElevationStep` aggregate length,
ascent and descent in uint64 centimetres directly from published integer legs.
Full middle roads use packed totals; clipped endpoints use the integer cumulative
profile interpolation. No geometry scan, floating-point calculation or additional
route array is required. The cursor is 312 bytes in the current host ABI and the
output is 24 bytes. Enable `integer_route`, `integer_prepared` and
`route_elevation` to include this adapter in `resourceBudget`.

Each step has an explicit work budget. Busy resumes the same interval. Output is
only replaced after the entire route succeeds; missing elevation returns
`Incomplete`, invalid bounds return `Invalid`, overflowing sums return
`BudgetExceeded`, and cancellation or package replacement is terminal. Empty
intervals have zero totals even when no elevation data exists. Caller legs and
the cursor must stay alive and fixed in memory until completion.

The Center shared-terrain Everest package passes 60 exact cases: every directed
edge as a whole, its middle half, and two halves aggregated together, each with
normal storage and every-second-read Busy injection. The independent Python
oracle reads the packed sample records and performs integer half-up interpolation
and summation. Evidence is `real-everest-integer-leg-elevation-report.json`, with
package and executable SHA256. It proves arithmetic and continuation on real
data; it does not establish surveyed accuracy or physical MCU resource peaks.
The same 60 cases pass in the small-cache build; raw evidence is
`real-everest-integer-leg-elevation-small-report.json`. Default and small full
CTest both pass 29/29 after the statistics integration.
Cross-compiler frames for the aggregation step are 80 bytes on ESP32-S3 and
96 bytes on ESP32-C3, below the 240-byte single-function limit.
