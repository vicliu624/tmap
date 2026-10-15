# Integer coordinate navigation

`beginIntegerCoordinateRoute` accepts two E7 latitude/longitude points, travel
mode, rational climb cost, snap radius, caller-owned sparse workspace and clipped
leg output. `integerCoordinateRouteStep` continues with an explicit work budget;
`integerCoordinateRouteResult` publishes only a completed result. Busy preserves
the current phase. Cancellation, package replacement and terminal failures
revoke publication. A fresh begin restarts after cancellation/failure.

The coordinator reuses one union for road snapping, endpoint preparation and
prepared route search. Only the active member is constructed in place; all three
members have trivial destruction. Candidate sets persist outside the union so
search can borrow them. The cursor is noncopyable: its active search contains
pointers to those member candidate sets, so it must not move while running.
Caller labels, queue, optional landmark buffers and leg output must also stay
alive until completion. No implicit heap allocation or route-sized copying occurs.

The current host ABI reports 936 bytes for the entire coordinator, including the
two 112-byte candidate sets and phase scratch. This replaces independently
retained snap, endpoint and search cursors; it is not added to those cursors.
Enable `integer_route`, `integer_prepared`, `integer_coordinate` in ResourceOptions
and select route state/leg capacities. Add `route_elevation` and/or `route_geometry`
for concurrently retained consumers. Endpoints are already included in the
coordinate cursor and are not counted again as output. Optional standalone
services remain separately budgeted. Application and allocator overhead are explicit.

Road snapping uses the existing bounded geographic geometry routine, including
floating point. Positions are rounded once to centimetres. Search, route cost
and clipped elevation aggregation use integer arithmetic. This is a low-floating
point route pipeline, not a wholly fixed-point geographic engine. Exact costs
refer to the quantized packed-road model, not continuous terrain or survey truth.

Tests run both directions with FIFO, linear Dijkstra and indexed heap, one work
unit per call and Busy storage. Actual published legs feed geometry and elevation
adapters; endpoints, publication gating, exact rational cost, cancellation,
restart, invalid coordinates/radius/coefficient and insufficient output are checked
under a heap allocation guard. Cross-build single-function frames are 112 bytes
for begin and 144 bytes for step on ESP32-C3; 64 and 112 bytes on ESP32-S3.
These are compiler frames, not peak task-stack measurements.

The real-map benchmark is `tools/benchmark_integer_coordinate_route.py`, driven
by `tmap_integer_coordinate_resource_probe` and a Center-produced artifact.
It verifies package/document identity and reports fixed capacities, retained
payload, I/O and continuation calls. Packed-profile interpolation is independently
checked; agreement among algorithms is a consistency check, not a separate
optimality proof. Physical MCU RAM/PSRAM, stack peaks, latency and power remain
unmeasured.

Current reports are `real-everest-integer-coordinate-report.json` and
`real-everest-integer-coordinate-small-report.json`. Each has 32 cases with
identical costs across algorithms and Busy repeats and exact packed-profile
statistic agreement. At 128 states/legs with concurrent clipped statistics,
retained host library payload is 16,392 bytes (default) or 10,248 bytes (small)
for FIFO/linear/heap; ALT adds 644 bytes for its lower bounds and scratch.
Read attempts, including open and Busy, are 50–139 default / 51–141 small for
FIFO/linear/heap, and 63–169 / 64–171 for ALT. The small sparse mountain graph
does not establish capacity needs or an algorithm preference for large maps.
