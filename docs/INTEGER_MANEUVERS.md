# Maneuvers from clipped integer routes

`beginIntegerManeuvers` borrows the published IntegerRouteLeg array.
`integerManeuverStep` converts one leg at a time with an explicit work budget,
reads the endpoint tangent points and streams Depart/Continue/Left/Right/UTurn/
Arrive events through ManeuverVisitor. No route-sized conversion/output array is
allocated. Road name references come from the packed directed edge records.

Distances accumulate checked uint64 centimetres from the integer leg intervals;
the visitor receives metres through the existing Maneuver interface. Geographic
endpoint conversion and tangent classification use floating point, matching the
optional drawing layer. Search and elevation costs remain integer. Classification
uses the existing 30-degree turn and 150-degree U-turn thresholds. These are
geometric hints, not intersection-aware lane or verbal instruction generation.

The cursor is 280 bytes on the current host ABI, including its 152-byte single-leg
conversion scratch and event value. Caller cursor/legs must remain at fixed
addresses while running. `ResourceOptions.maneuvers` selects this cursor when
`integer_prepared` is enabled. The callback can consume events immediately; any
retained event array is additional caller/application storage and must be budgeted.
Empty input succeeds without events. Cancellation, callback rejection, closed or
replaced package and terminal errors are sticky until a new begin. Busy resumes
without repeating previously delivered events. Because output is streamed, callers
must discard partial instructions if the operation fails later.

Tests cover clipped forward/reverse coordinates, Depart/Continue/Arrive, exact
packed-length distance, named event references in the real-map oracle, left/right/
U-turn fixtures, empty input, callback rejection, explicit cancellation and closed
package. The main continuation tests run under a heap guard with every-second-read
Busy. Cross-compiler frames for the step are 80 bytes on ESP32-S3 and 208 bytes
on ESP32-C3. Callee/callback frames are not included; no physical task-stack claim.

Real Center Everest evidence is `real-everest-integer-maneuver-report.json` and
`real-everest-integer-maneuver-small-report.json`. Each contains 32 full integer
coordinate pipeline cases with FIFO/linear/heap/ALT, both directions, zero/5 climb
weights and normal/Busy reads. The Python oracle independently locates endpoints,
computes tangent classification, verifies name IDs and centimetre interval
distances. Endpoint tolerance is 0.05 metres to account for geographic rounding;
distance tolerance is 1e-8 metres. This is packed-data consistency, not surveyed
accuracy, physical MCU latency or an independent shortest-path proof.

The updated probe reports the concurrently retained maneuver cursor in library
payload and separately reports its desktop event storage. Application allocator
overhead and MCU RAM/PSRAM/stack peaks remain unmeasured. Earlier coordinate-only
reports retain their original smaller payload figures and executable digests.
At 128 search states and 128 leg slots, with coordinate search, elevation
statistics and maneuver controls retained concurrently, native library payload
is 16,672 bytes default / 10,528 bytes small for FIFO/linear/heap, or 17,316 /
11,172 bytes for ALT. Across all 32 cases, physical read attempts (including
open and Busy) are 56–179 default and 57–181 small. These numbers exclude the
separately reported desktop event array and any rendered polyline/Navigator.
Default/small full CTest pass 29/29 after integration.
