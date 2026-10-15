# Integer routing with clipped endpoint boundaries

beginIntegerRouteWithBoundaries accepts start/destination node rows and optional
initial/final directed edge rows (UINT32_MAX means absent). Initial edge must
enter the start node; final edge must leave the destination. Both must allow
the selected mode. Incorrect direction/node/access is Invalid.

The initial boundary is the root arrival state, so the first search edge must
obey its turn restrictions. Destination arrival also checks the transition to
the final boundary. A prohibited transition does not prematurely finish search:
another legal arrival may still be found. The boundary edges themselves are not
included in returned middle-edge rows or costs. The future coordinate planner
must include clipped initial/final interval costs with checked integer addition.
This is a search primitive, not complete coordinate snapping/route assembly.

All integer algorithms (FIFO, linear/heap Dijkstra, ALT) use this logic. Caller
arrays remain unchanged; cursor retains two additional uint32 edge rows (8 B).
ResourceBudget uses actual cursor sizeof. Existing beginIntegerRoute is the
no-boundary wrapper. No allocation or floating arithmetic is introduced.

Tests use a one-way chain with a foot-only forbidden turn, including a package
with ALT tables and alternating Busy. Plain node routing succeeds, while initial
and final boundaries correctly prevent the prohibited transition. Same-node
middle routing with both boundaries fails for foot but succeeds for bicycle,
returning zero middle edges. Invalid connecting boundaries are rejected. Tests
cover all four algorithms. Independent random reference tests remain applicable
to ordinary node routing; boundary tests are targeted fixtures.

Default/small CTest each passes 29/29; S3/C3 core builds pass. Boundary begin
frames are S3 128 B/C3 144 B, and route-step frames S3 96 B/C3 128 B, excluding
callees. No physical device stack-watermark or latency measurement is implied.
