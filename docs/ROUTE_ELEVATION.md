# Elevation totals for clipped roads

GraphReader exposes ascentPrefix and descentPrefix: directed edge row,
distance fraction in [0,1], and caller-owned output in meters. Both read the
same packed cumulative road samples, using bounded binary search and linear
interpolation. They do not reread DEM cells. Fraction zero is zero; fraction one
uses the edge total. Missing required elevation is Incomplete, never a guessed
zero. Output is valid only on Ok; retry Busy/More with identical arguments.

For a directed road interval [a,b], cumulative ascent is ascentPrefix(b) minus
ascentPrefix(a), and descent is descentPrefix(b) minus descentPrefix(a).
Fractions refer to distance along the directed shape, not vertex index. Retain
the first result before requesting the second, and do not interleave an unfinished
request with another prefix call: both share one reader-owned continuation.
RouteLeg fractions are segment-relative. `route_elevation.h` now provides
beginRouteElevation/routeElevationStep to convert them and aggregate length,
ascent and descent for the whole route. The caller retains immutable legs and
a cursor; each step accepts a nonzero work budget. It streams shape points for
partial edges and reads packed prefix samples. Full edges use their totals.
Output is published only on Ok; failures and cancellation remain terminal until
new begin, and package replacement cancels the operation. Empty routes yield
zero totals. It shares GraphReader's prefix continuation, so serialize requests.
Each prefix invocation itself is bounded to 64 iterations; a step budget counts
invocations, not a strict CPU or I/O time bound. No temporary array or allocation
is used. Set ResourceOptions.route_elevation to include retained cursor/output
in the budget. For integer edge-row output the budget selects the integer cursor
and centimeter output instead.

`beginIntegerRouteElevation` / `integerRouteElevationStep` aggregate complete
directed edge rows from integer node routing into uint64 centimeter totals.
They use checked integer additions, return BudgetExceeded on overflow, and
publish only after completion. Missing edge elevation is Incomplete. The caller
owns immutable edge rows, cursor and output; Busy/More retry and cancellation
follow the coordinate statistics contract. These functions do not handle clipped
coordinate legs and perform no floating arithmetic or allocation. The C3 object
has only GraphReader initialize/edge and memcpy undefined dependencies, with no
floating runtime helpers. Step frames are 48 B on both S3 and C3 (callees excluded).
The desktop resource probe accepts algorithm `integer-stats` to measure retained
integer search output plus statistics; real normal/Busy evidence is recorded in
REAL_MOUNTAIN.md. Its extra I/O is reported separately and its retained cursor
and output are included in the host payload total.

The ascent/descent selector occupies existing cursor padding; tested host
GraphReader remains 296 B. There is no allocation or additional scratch array.
Both channels validate cumulative bounds, including descent against its edge
total. Individual shared implementation frames are S3 80 B / C3 224 B, within
the 240-byte new-core limit; complete call-chain stack remains a hardware concern.

Tests alternate ascent/descent at identical edge/fraction values to catch stale
continuation reuse, check zero/full prefixes, invalid input preserving output,
Busy retries and no allocation. This verifies packed-profile arithmetic, not
independent DEM accuracy or a surveyed route's true ascent/descent.

Aggregation tests exercise clipped endpoints spanning two roads under one-work
slices and alternating Busy, no partial output, cancellation/restart, empty
routes and zero allocation. Aggregate step frames: S3 96 B / C3 224 B.
Failure tests retain sentinel output when any edge lacks elevation, when a
segment fraction is NaN, and when the package closes during an active operation.
Incomplete remains terminal on repeated calls rather than publishing a partial
sum; map closure returns Cancelled. These are executable checks, not just API
contracts inferred from normal-route success.

Desktop `tmap_inspect file measure-route-coordinates lon1 lat1 lon2 lat2 [penalty]`
uses an actual 128-state/128-leg configuration and emits JSON totals, route legs,
read counts and retained host payload. `tools/benchmark_coordinate_elevation.py`
compares this output against independently interpolated packed profiles for the
real Center mountain package; see REAL_MOUNTAIN.md and its raw report.
