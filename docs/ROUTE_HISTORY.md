# Bounded via-way restriction history

The XML/PBF importer now exports ordered via-way chains as `via_restrictions`,
each containing directed edge IDs, `kind` (`no`/`only`) and mode mask. Consecutive
members must have a unique shared junction. Via-road traversal is enumerated
with explicit path/edge/exploration caps; missing, inaccessible or ambiguous
boundaries are rejected rather than guessed. Multi-from/to cardinality retains
the no_entry/no_exit checks. Mixed node/way via members are rejected. An only-turn
target outside the included graph now fails explicitly instead of being lost.

The common XML/PBF path follows the [OSM restriction model](https://wiki.openstreetmap.org/wiki/Relation:restriction):
one via node or connecting via ways; pedestrian rules require the foot-specific
key. Existing resolved calendar snapshots apply before relation compilation.
Supported live calendar road/barrier and node/via-way turn evaluation is now
implemented; see REAL_RUNTIME_NAVIGATION.md. Noncalendar expressions remain
unsupported and fail explicitly.

The producer compiles proper path prefixes and suffix fallback into sparse
transition tables. A no rule rejects completing its prohibited sequence. An
only rule requires following an active prefix's permitted continuation; rules
with the same prefix combine alternatives, while simultaneous distinct prefix
obligations intersect. Nonalphabet edges reset history unless an active only
rule forbids them. No fixed-length predecessor scan guesses the relevant history.

## MCU storage and protocol

History is encoded in the existing label key as
`context * edge_count + directed_edge + 1`. Zero stays unused and UINT32_MAX is
the root sentinel; the product is checked to avoid collisions/overflow. Integer
labels remain 24 bytes, float labels remain 32 bytes, and no context-sized or
graph-sized RAM allocation is introduced. Sparse caller capacities still bound
visited states, so active history can consume more slots and return BudgetExceeded.
The graph reader reuses its existing turn scratch; ordinary graphs and modes
without active history bypass these tables. Other node-via restrictions still
apply first. Initial clipped roads seed history, and final clipped roads are
checked before publishing a coordinate route. Output decodes physical edge IDs.

Draft v2 graph metadata offset 8 keeps low mode bits 3 and adds the active-history
mode mask at bits 8–9. Sections 1114/1115 must appear together for a nonzero mask.
State rows are 16 bytes: transition start u64, count u32, default allowed mask u32.
Transition rows are 16 bytes: physical edge u32, next context u32, allowed mask
u32, reserved zero u32. Rows are sorted by physical edge within each state.
Defaults go to context zero. Shape/cardinality/key-space, mode and reserved fields
are checked; this bounded lookup is not a global semantic verifier of every row.
Trusted source identity remains separate from local CRC/record validation.

The compiler caps 4,096 rules, 64 edges per sequence, 65,536 prefix states and
4,000,000 transitions. OSM via members cap at 32 ways and traversal expansion is
bounded. These are explicit producer limits, not promises that a small MCU can
retain all reachable routing labels. Table pages use the existing table cache
slot; the continuation saves range ends so Busy cannot force prefix rereads.

## Evidence and remaining scope

Tests include XML export, dense/plain PBF parity, serialized imported restrictions
read by the actual SDK, 32 node-mode/history cases across FIFO/linear/heap/ALT,
eight detour cases where the same physical edge needs distinct histories, integer
and float coordinate clipping/final-boundary cases, Busy one-work slices and a
CRC-valid invalid flag record that fails without published output. The detour
fixture independently forces four foot edges while the unrestricted bicycle
path has three; collapsing histories loses the valid foot route.

The current work is synthetic restriction evidence, not a real PBF restriction
survey or physical hardware measurement. Complex ambiguous/inaccessible member
forms are still rejected. No claim of complete OSM or live conditional support
is made. Current ABI sizes and cross-compiler frames are checked alongside the
full default/small regression suite; existing real maps without history retain
their encoded format and are remeasured separately.

Current native sizes are GraphReader 296 bytes, integer cursor 184 bytes, integer
label 24 bytes and float label 32 bytes, unchanged by history support. The
transition module target-file text is S3 991 / C3 1,122 bytes with data/bss zero;
its individual frame is 80 bytes on both. These are module/compiler measurements,
not complete firmware Flash increments or peak task-stack watermarks.
Real immutable regional artifacts without history pass current default/small
reruns in `real-everest-regional-native-history-guard-report.json` and
`real-everest-regional-small-history-guard-report.json`; routes, costs, statistics
and maneuvers match the pre-history baseline. This protects existing behavior but
does not measure real via-way datasets.
