# Integer endpoint candidate preparation

beginIntegerEndpoint/integerEndpointStep accept a Snap from the same current
package, endpoint side, mode and AscentRatio. They produce at most two legal
directed choices with node/boundary rows, integer clipped legs and exact cost
units for the centimeter-quantized model. Caller must ensure the Snap came from
the current package; a plain Snap does not carry an independent generation tag.
The cursor copies it and tracks package generation for subsequent steps.

The base along_fraction converts once to nearest centimeter using floating
arithmetic at this API boundary. Reverse position is length minus that same
position, avoiding independent rounding. Reciprocal edges must share shape,
reverse direction and length. Core search/cost arithmetic remains integer;
this helper does not claim an entirely floating-free coordinate pipeline.

Exact node choices have no partial edge or added cost, and duplicate node choices
are merged. A one-way road has one choice. Zero climb coefficient computes
distance cost without requiring heights; positive coefficient uses integer
interval profiles and rejects missing required heights. Turn boundary rows are
retained for beginIntegerRouteWithBoundaries. Candidate generation alone does
not assemble or optimize the final coordinate route.

Output is published only when all eligible directions finish. Work budget,
Busy/More, cancellation and package/time guards are explicit. No allocation or
hidden grow. Caller cursor is 472 B, output 112 B and integer leg 12 B on the
tested host ABI; allocate these in caller-owned member/static/PSRAM storage.
ResourceOptions.integer_endpoint includes them and a standalone GraphReader.
S3 step frame 96 B/C3 112 B (callees excluded).

Tests cover both directions, complementary centimeter positions, fractional
climb coefficient, distance-only arrival, exact-node deduplication, one-way,
one-work Busy slices, unpublished partial output, cancellation and no allocation.
Default/small CTest 29/29 each and S3/C3 core builds pass. Whole integer coordinate
routing, clipped geometry and real-coordinate resource comparisons are now
implemented in [the unified coordinator](INTEGER_COORDINATE_ROUTE.md); these
component figures are not device RAM/stack peak evidence.

## Direct same-road candidate

beginIntegerDirectRoute/integerDirectRouteStep compare up to four prepared
direction pairs on the same directed edge, requiring nondecreasing centimeter
positions and allowed travel mode. Positive climb coefficient uses the integer
interval model; zero coefficient needs no height data. A successful output is
one IntegerRouteLeg and cost_units. NoPath means no direct same-road candidate,
not proof that a route through other nodes does not exist. The prepared route
coordinator compares this candidate with legal node-boundary routes.

Prepared candidates are borrowed and must remain immutable/from the same current
package throughout the operation. Work slices, Busy, map/time guards, unpublished
partial output and cancellation follow endpoint preparation. Zero-length direct
intervals are valid. No per-node storage or implicit allocation is added.
ResourceOptions.integer_direct includes its cursor/output and two retained
candidate sets; do not assume it deduplicates buffers shared with other services.
Tests cover forward/climb cost, reverse distance-only choice, one-way rejection,
one-work Busy and cancellation. Complete coordinate route assembly is implemented
by the unified integer coordinator.
The tested host direct cursor is 288 B and output 24 B; two borrowed candidate
sets require 224 B. Step frames are S3 112 B/C3 128 B (callees excluded).
Default/small CTest 29/29 each and S3/C3 builds pass after this addition.
