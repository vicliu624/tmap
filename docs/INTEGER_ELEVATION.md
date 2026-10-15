# Integer road elevation prefixes

integer_elevation.h provides beginIntegerElevation and integerElevationStep.
Input is a directed edge row and absolute distance in centimeters along it.
Output is cumulative ascent_cm/descent_cm, calculated together from packed
profile records with bounded binary search and uint64 interpolation. No floating
arithmetic or DEM resampling is used. Interior values round to nearest centimeter
(half upward); each prefix differs from exact interpolation by at most 0.5 cm.
Subtracting two rounded prefixes can introduce up to 1 cm interval error.

Begin returns Ok with pending cursor work; Busy/More from dependency reads can
be retried. Step accepts nonzero work budget and publishes only on Ok. Cancel,
package replacement and terminal errors persist until a fresh begin. Distance
outside the edge is Invalid. Missing required heights/profile is Incomplete;
distance zero is zero even without height coverage. Full distance uses packed
totals. Cursor and output are caller-owned and can use explicit MemoryObject/
provider storage; no hidden allocator exists.

ResourceOptions.integer_elevation counts cursor/output and, if no routing service
is retained, the required GraphReader. Tested host cursor is 120 B and output
8 B. Individual step frames S3 112 B/C3 144 B, excluding callees. C3 object has
integer division helper __udivdi3 but no floating helpers. This is not complete
task-stack or board RAM evidence.

Tests compare the integer midpoint with floating interpolation within 0.5 cm,
exercise one-work Busy slices, unchanged partial output, cancellation and budget
accounting. A begin-status regression was found and corrected; test retry loops
are now bounded to catch its recurrence. Default/small CTest 29/29 each and S3/C3
builds pass. This is a foundation for integer clipped-road costs; integer
coordinate routing and conversion from snapped geometry are not yet complete.

## Clipped interval cost

beginIntegerEdgeInterval/ integerEdgeIntervalStep accept directed edge row,
start/end centimeter distances and AscentRatio. They retain both rounded
prefixes, subtract gain/loss, and compute `cost_units = length_cm × denominator + ascent_cm × numerator`
with checked integer arithmetic. The cost is exact for
the centimeter-quantized interval model, not exact unrounded terrain interpolation.
Required height gaps are Incomplete. Empty in-range intervals yield zero;
reversed intervals or zero denominator are Invalid. Output is published only
on completion; Busy, cancellation and package generation behave as for prefixes.

Caller cursor is 176 B and output 24 B on the tested host ABI. Select
ResourceOptions.integer_interval to include them; standalone use also counts
GraphReader once even if integer prefix and interval services coexist. No new
array/allocation is required. Tests cover one-work Busy, output withholding,
fractional coefficient 3/2, empty/invalid intervals and cancellation. This API
still requires road-distance endpoints; coordinate snapping/geometry conversion
and route integration remain required work.
