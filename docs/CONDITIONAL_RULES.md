# Conditional navigation rules

`tools/import_osm.py --condition-at ISO_INSTANT` resolves selected foot/bicycle
month/weekday/time conditions on the producer. An explicit UTC offset is mandatory; the
instant describes local wall time at the source location. No host timezone is
inferred. XML and PBF share the same evaluation path.

Supported conditions are weekday lists/ranges (`Mo,We`, `Mo-Fr`, `Fr-Mo`) and
optional month lists/ranges (`May-Oct`, `Nov-Mar`, `Jan,Mar-May`) preceding them,
as well as `24/7`. Month end is inclusive. Overnight intervals carry the selected
start day's month and weekday across month/year boundaries: June 30 22:00–July 1
06:00 belongs to June's selector. Named seasons remain ambiguous and unsupported.
Single conditions may omit enclosing parentheses; inactive clauses still undergo
full validation. Syntax reference: [OSM conditional restrictions](https://wiki.openstreetmap.org/wiki/Conditional_restrictions)
and [opening-hours specification](https://wiki.openstreetmap.org/wiki/Key:opening_hours/specification).

Explicit date selectors include `Jun 01-Oct 01`, `Dec 15-Jan 10`, a single
`Feb 29`, and same-month `Jun 01-15`. Annual dates repeat and may wrap the year;
Feb 29 matches only in leap years. Absolute dates support `2026 Jun 01-15` and
`2026 Dec 31-2027 Jan 02`; absolute endpoints must be chronologically ordered,
and an omitted end year means the start year. Both endpoints are inclusive.
Selectors may precede weekdays and time intervals. Overnight portions use the
previous starting date, including after the final selected date. Invalid dates
are rejected even in inactive clauses. Date lists, offsets from Easter/holidays,
week-number selectors and open-ended dates remain unsupported.

Weekday selectors can be followed by
clock intervals (`08:00-10:00`, optionally comma-separated), or both.
Overnight intervals such as `Mo-Fr 22:00-06:00` carry the early-morning portion
from the previous selected weekday: Saturday 05:00 matches Friday's interval,
Saturday 22:00 does not. Sunday-to-Monday wraparound is supported. Equal start/end
is rejected as ambiguous; 24:00 is permitted only as an end.
Intervals include their start and exclude their end; `24:00` is a valid end.
Last matching clause wins. If none matches, the unconditional tag remains.
`restriction...=none` removes the unconditional restriction for the snapshot.
All clauses are parsed and validated, including inactive clauses.

Access, foot, bicycle, vehicle, one-way and node-via restriction conditional
tags are evaluated for the selected travel modes. Unsupported values,
holidays, unsupported date forms, named seasons, weather, vehicle dimensions, solar times, nested
expressions fail explicitly. Via-way restrictions
remain unsupported. Without `--condition-at`, applicable conditions still fail
instead of silently dropping rules or roads.

The output JSON records `condition_snapshot`. This field is provenance, not a
runtime package validity guard. Graph metadata also retains the snapshot and
selected travel-mode mask. Regional merging requires those fields to match,
even if topology_epoch is identical; mixing a timed and untimed graph is rejected.
The merged graph and virtual JSON manifest preserve the profile metadata. Older
graphs lacking both fields may still merge with one another under their explicit
topology contract, but cannot merge with a graph declaring a different profile.

Newly produced graphs with profile metadata carry optional section 1113:
24 little-endian bytes (u32 version=1, u32 snapshot flag, u64 Unix UTC seconds,
i32 local UTC offset seconds, u32 travel-mode mask). The topology digest includes
this profile, so different snapshots cannot share a declared graph identity.
`readRoutingProfile` reads and validates this record without allocation;
Missing means an older graph without this metadata, not proof of timeless rules.
`checkRoutingProfile` checks a single requested mode and, for a snapshot, exact
local offset and the same UTC minute. It returns Incompatible outside that
conservative window. Applications must call it before routing and as time changes.
GraphReader now invokes the guard at ordinary route start, snapping start, and
every active ordinary/integer/coordinate route step. Set a borrowed RoutingClock
callback/context with setRoutingClock; it returns current UTC seconds and local
offset, or false when unavailable. Conditional packages without a usable clock
are Incompatible. Expiration during search terminates that search; retry remains
terminal until a new begin. Static profiles and older metadata-free packages
require no clock. Completed route geometry and Navigator progress still need
application-level validity management.
There is still no dynamic calendar evaluator or automatic invalidation of an
already delivered route. Clock/context lifetime is owned by the application.
The callback must be bounded and must not mutate/reenter readers.
Tests start ordinary, integer and coordinate searches with a valid clock, advance
it past the snapshot minute, and verify Incompatible remains terminal even if
the clock subsequently moves back. Restart requires a new begin operation.
Two clock pointers and a generation-bound cached profile increase GraphReader
by 48 B from the earlier 248-byte tested host baseline, to 296 B. ResourceBudget
uses current sizeof(GraphReader), so the added storage is included automatically.
The profile is read once per package generation; subsequent guarded steps check
only the clock and cached profile. Closing/reopening invalidates it. This avoids
evicting graph pages and preserves forward progress under alternating Busy.
Actual validateRouting frames are S3 48 B / C3 32 B, excluding callees.

Center's navigation plan exposes `ConditionAt` and records the instant and
static-snapshot policy in the artifact manifest. Applications must
retain the production document and restrict use to the represented conditions;
do not distribute such a package as an unconditional routing dataset. Resolving
conditions on the host avoids a runtime expression parser, but is only part of
the planned complete conditional-rule support.

The optional check uses a 24-byte local read buffer and caller-owned profile;
it adds no heap allocation or PackageWorkspace fields. Compiler-reported
readRoutingProfile frames are S3 80 B / C3 64 B, and checkRoutingProfile 32 B
on both. These are individual function frames, not full call-chain stack peaks.
Malformed-record tests recompute valid page CRCs after changing the version,
flags, timestamp range, offset or travel-mode mask, so they verify semantic
validation independently of CRC rejection. Failed reads preserve the caller's
previous profile. Direct checks also reject invalid caller-constructed profiles
and timestamps outside the supported signed Unix range.
The 24-byte logical record occupies one physical page plus a directory entry;
include this extra page in file-size budgets. C++ tests cover missing metadata,
Busy, minute boundaries, offset mismatch and wrong modes under both page profiles.

Tests exercise weekday wraparound, comma lists, interval boundaries, ordered
clauses, invalid inactive clauses, absent offsets and a road reopening only
during the selected period. The reopened graph is passed through the actual
TMAP packer. These are synthetic semantic tests, not evidence of comprehensive
OSM conditional syntax coverage or real calendar-aware hardware navigation.
