# Runtime calendar predicate primitive

Supported calendar predicates and their live road, one-way, endpoint-barrier
and node/via-way turn/history integration are implemented. Caller-owned readers
bind an immutable query UTC/offset; production-time snapshots remain an optional
separate policy. Noncalendar conditions and ambiguous member forms fail explicitly.
See REAL_RUNTIME_NAVIGATION.md for actual regional roads and
FULL_FEATURE_RESOURCES.md for the 37-section Center synthetic joint-service test.

`compile_calendar.py` validates the supported OSM calendar grammar using the
existing strict host evaluator and emits conjunctions. `CalendarPredicate` is
16 bytes: first/last date u32, month mask u16, first/last minute u16, weekday mask
u8 and date kind u8. Dates encode year*512 + month*32 + day; recurring dates omit
the year. Date kinds are none, recurring annual and absolute. Months and weekdays
use bit masks. Time is start-inclusive/end-exclusive; after-midnight portions
apply selectors to the previous civil date. Comma-separated time periods are
separate conjunctions combined with OR. Action values and last-matching-clause
precedence are handled by the streamed rule integration layer described below.

`evaluateCalendarPredicate` receives UTC Unix seconds and an explicit local UTC
offset. It requires no platform clock API, timezone database, floating point,
allocation or retained internal cursor. It executes bounded arithmetic rather
than scanning years. Civil conversion adapts the public-domain formulas explained
by [Howard Hinnant](https://howardhinnant.github.io/date_algorithms.html), credited
in the source. The input range is UTC 1970 through 9999, with offsets strictly
between -24 and +24 hours; resulting local dates must not exceed year 9999.
Early local/previous dates in 1969 are supported. DST/offset selection is the
application's responsibility. Leap seconds are not modeled.

Invalid masks, minutes, dates, date kinds, reversed absolute ranges, timestamps
and offsets return Invalid without changing output. Unsupported holiday, solar,
weather or other expressions are rejected by the compiler. A 2,048-character
expression and 32-period cap bound compiler input. This is not a parser for
arbitrary OSM expressions on the MCU.

The caller owns each decoded predicate. ResourceOptions.calendar_predicates
counts explicitly retained predicates as 16-byte workspace entries and checks
overflow. No assumption is made that a file's entire rule table is loaded. The
streamed rule reader is now available. Navigation rule integration remains separate work.

## Streamed ordered clauses

`compile_calendar_rules` maps ordered clauses to explicit caller action codes.
Each little-endian record is 24 bytes: predicate16, ordinal32 and action32.
Periods of one clause share ordinal/action and combine with OR. Matching later
clauses overwrite earlier actions. The compiler caps 64 clauses and 65,536
characters; action meaning is separate from calendar syntax.

`beginCalendarRules` freezes query UTC/offset, borrows a file range and caps its
record count (default 4,096). `calendarRulesStep` consumes an explicit record
budget and retains one 24-byte scratch and one predicate, regardless of table
size. Host cursor/output are 128/8 bytes, included by ResourceOptions.calendar_rules.
An optional action bound rejects invalid values even in inactive records.
Additional caller predicates are counted separately. The noncopyable cursor and
file must stay alive at fixed addresses while pending exact reads exist.

Busy/More preserves read progress. Output is only replaced after the entire range
validates. Ordinals must be nondecreasing, and action codes within one ordinal
must agree, even for inactive clauses. Cancellation and size/epoch changes are
sticky until a new begin. Without an epoch callback the source must remain
immutable. Empty ranges return fallback with matched=false. Wall-time changes do
not alter the captured query clock. Immutable RuntimeRoutingRules binds the
navigation readers to that same clock and package generation.

Tests cover priority, OR, Busy, unpublished output, epoch changes, cancellation,
restart, limits, empty fallback and late invalid ordering. Compiler begin/step
frames are S3 48/64 bytes, C3 80/96 bytes; caller/callee task peaks are unmeasured.

## Evidence

### Resolved road property policy

`compile_road_conditions.py` compiles seven separate property programs in fixed
order: access, vehicle, foot, bicycle, oneway, oneway:bicycle, oneway:foot. Each
has an explicit static fallback and ordered 24-byte calendar records. Mode access
uses inherit/denied/allowed; direction uses inherit/both/forward/backward. Missing
bicycle direction inherits the evaluated general direction, while foot direction
defaults to both. Roundabouts default to forward for bicycles. Unknown static
values and unsupported conditions fail rather than becoming implicit inheritance.

`resolveRoadPermissions` combines the completed property results with highway
eligibility and requested modes. Explicit foot/bicycle access overrides inherited
general access; vehicle denial affects inherited bicycle access. General oneway
affects bicycles only. Invalid input leaves output untouched. The core policy is
integer-only, allocation-free, and performs no I/O or clock reads. Current host
ABI input/output are 7/2 bytes, with 62,208 exhaustive combinations checked against
a separate per-mode policy oracle. S3/C3 individual compiler frames are 32/0 bytes;
this is not a complete task-stack peak measurement.

`beginRoadRules` copies seven range descriptors into a noncopyable caller cursor,
freezes UTC/offset, and bounds their combined record count. `roadRulesStep` uses
one embedded calendar cursor to evaluate properties sequentially; each work unit
advances at most one record or one property completion. Empty properties still
consume bounded work. Output is withheld until all seven programs complete.
Size/epoch validation spans property boundaries and publication; invalid inactive
action codes, cancellation and late errors cannot publish partial masks. Current
host cursor/output are 280/2 bytes, explicitly included by ResourceOptions.road_rules.
Road begin/step individual frames are S3 112/112 bytes and C3 112/96 bytes.
Selecting calendar_rules as well counts a separate additional calendar cursor.

The producer emits paged rule payloads consumed by the optional graph edge evaluator.
`encode_road_program` and `decodeRoadRuleProgram` now agree on a 120-byte
little-endian descriptor: version u32=1, flags u8=0, highway u8, reversed u8,
reserved u8=0, then seven (logical record byte offset u64, count u32, fallback
u32) ranges. Calendar payloads remain 24 bytes each. Encoder checks aggregate
record budget and 64-bit offset overflow; decoder validates version/reserved
fields, masks/direction, fallback domains, aggregate count and all logical bounds
before publishing any output. Descriptor encoding/decoding and failure publication
tests pass. This is a wire primitive rather than an advertised navigable section:
Graph flag bit16 requires runtime evaluation; GraphReader requires an explicitly
bound caller-owned EdgeRuleReader. Sections 1116/1117/1118 contain edge-aligned u32 program
references (UINT32_MAX for static edges), 120-byte programs, and 24-byte conditions.
Equivalent complete programs are deduplicated deterministically in graph edge
order; reverse direction is part of program identity. Dynamic edges require
explicit road_reverse and road_tags. Total program/record caps are explicit;
runtime rules cannot be combined with a filtered condition snapshot. Producer
tests verify deduplication, logical offsets, graph flag and paged packaging.
These artifacts return Unsupported without a bound rule reader.
OSM XML/PBF import now has an explicit runtime_conditions option (CLI
--runtime-conditions). Relevant conditional roads retain both potential directions
and requested mode bits; original properties and explicit direction feed the rule
tables. This conservatively retains extra closed-at-query roads rather than losing
reopenable topology. Static barrier masks still intersect edge access. Irrelevant
mode conditions are excluded; every relevant branch is validated. Production
snapshots cannot be combined with this option. Runtime programs contribute to
topology identity even with a declared source epoch. Tests cover construction
highways reopened by mode conditions, reverse edges under static oneway, inactive
invalid conditions, excluded modes and XML/plain/dense PBF parity. Conditional
turns require the immutable runtime binding described below; barriers use the edge reader.
`PackageSectionFile` now exposes an existing paged section as a logical
RandomAccessFile without another page cache. Its exact-read continuation advances
at most one payload page per attempt, including records straddling page boundaries.
Read sizes are bounded to one payload's length; pending retries must preserve the
offset, output address and byte count. Old views return Cancelled after package
close/reopen. PackageSectionFile::epoch can bind rule cursors to package generation.
The caller owns the view and must retain it while a rule cursor borrows it; a
pending read cannot be repurposed for another request. ResourceOptions.section_file
counts this additional control separately. Cross-page bytes, Busy continuation,
per-attempt page-read bounds and close invalidation pass package-continuation tests.
This connects the rule stream to CRC-checked pages; EdgeRuleReader and
TurnContextReader connect the corresponding sections to runtime graph evaluation.

`beginRoadProgramRead`/`roadProgramReadStep` now connect CRC-checked section 1117
to the descriptor decoder using an explicit page budget and caller-owned scratch.
Section row counts/strides and section 1118 logical bounds are checked before
publication. Cancellation and package generation changes invalidate the read.
An actual paged fixture is read under Busy, decoded, and evaluated through a
PackageSectionFile over section 1118; its masks match the expected Thursday
permissions. This is the storage/evaluation pipeline, not integrated routing.
ResourceOptions.road_program_read counts the read cursor and separate descriptor
output (seven ranges, highway and direction); RoadRuleCursor copies that output.
Passing its own embedded program array to beginRoadRules is explicitly rejected
before reset to prevent aliasing from destroying the input descriptors.

`EdgeRuleReader` now follows edge reference -> paged descriptor -> condition
evaluation with a fixed constructor UTC/offset. It intersects the selected forward
or backward mask with the edge's static access (including barrier restrictions),
supports a static-reference sentinel, and withholds output until completion.
begin/step/cancel reuse caller-owned storage for one in-flight edge. Cancel also
discards the section view's abandoned pending read so a new edge can start. Views
must remain at fixed addresses and be reconstructed after map reopen. Tests read
the actual paged fixture on Monday, verify bicycle-only and barrier-intersected
zero masks, cancel/restart and unpublished waiting output. ResourceOptions.edge_rules
counts its embedded readers once; separately selected reader options are additional
storage. GraphReader now invokes this evaluator before publishing dynamic edge
access, with an immutable constructor binding. The bound reader captures query
time and belongs to the same open package generation. Missing bindings return
Unsupported; wrong/stale bindings return Incompatible. Rules section presence,
flag, strides and edge-aligned reference counts are validated together.
The current host GraphReader/GraphInfo remain 296/28 bytes: new fields fit existing
padding. A dynamic edge's 48-byte raw record is retained in the optional reader
to avoid repeated edge-page loads evicting pending rule pages under Busy. This
raises the optional reader from 528 to 584 bytes. A real paged three-node fixture
with Busy validates unpublished edge access and node routing: Monday foot NoPath,
Thursday foot Ok. The fixture now includes two landmarks and passes 32 node-route
cases: two query times, foot/bicycle, four float algorithms and four integer
algorithms. Successful routes publish exactly edges [0,1]. Four snap cases reject
the closed road for foot and select its midpoint when open/bicycle. Each route and
snap advances with work budget1. The file adapter returns Busy every second read
attempt; cache is reused across cases. Default/small executable, source and fixture
hashes plus stdout are in runtime-road-native-evidence.json and
runtime-road-small-evidence.json, regenerated by collect_dynamic_road_evidence.py.
An additional bidirectional paged fixture passes 64 clipped-coordinate cases:
two times, forward/reverse, foot/bicycle, four float and four integer algorithms.
Monday oneway overrides permit reverse; Thursday defaults forbid it. Successful
routes check directed edge sequences and clipped fractions/integer intervals.
All step budgets are1 with Busy and cache reuse. Integer boundary validation now
advances in the route cursor rather than restarting a two-edge loop in begin:
this prevents dynamic rule evaluation being reset by alternating boundary reads.
Without boundary edges the original direct initialization path is retained.
Boundary endpoint/access errors are now delivered by step rather than begin;
out-of-range rows still fail at begin. Real-map conditional navigation and
conditional turn/barrier evidence remain open; synthetic cases do not prove
those requirements or hardware peaks.

Producer runtime barrier programs now preserve otherwise closed endpoint topology.
Each edge can reference at most two endpoint programs via section1119, two u32
rows per edge (UINT32_MAX absent). Programs share section1117 and conditions1118,
with cross-program deduplication and existing total caps. Baseline masks follow
the existing conservative importer: gates/bollards/entrances allow both, stiles
allow foot, other barriers start closed; mode overrides and conditions can change
them. Direction is both and road-specific direction remains separate. Barrier
programs contribute to topology identity and require graph flag bit17. GraphReader
checks flag/section1119 presence and edge-aligned eight-byte row counts. The edge
reader evaluates both endpoint programs sequentially using the same phase scratch,
intersecting their masks with road access. Even a zero intermediate mask does not
skip validation of later programs. Reversed barrier descriptors or asymmetric
evaluated barrier directions are invalid. No output publishes before all endpoints
finish. The active union member is tracked independently of the outer read stage
for cancellation/restart. Current host EdgeRuleReader remains584 B and GraphInfo
remains28 B. XML/dense/plain PBF parity and 48 paged node-route cases pass: Monday
foot blocked, Tuesday bicycle blocked, Thursday both permitted, with all float/
integer algorithms, Busy and cancellation/restart. Five additional malformed
fixtures recompute valid page CRCs: second-endpoint direction fallback, reversed
descriptor, inactive invalid action, invalid reference and out-of-bounds range.
All return Invalid without publishing edge output, including when the first
endpoint already closes foot access. Barrier direction properties must have
no conditions and both-direction fallbacks; malformed future direction branches
cannot hide behind currently symmetric/zero masks. Repeating the same failed edge
returns its terminal error without new file reads; explicit begin starts a new
attempt. Fixture hashes and malformed-case count are recorded in both reports.
Clipped barrier cases and real-map hardware evidence require further verification.

## Conditional turn property preparation

compile_turn_conditions.py preserves two ordered properties: general restriction
(bicycle only) and mode-specific restriction. Code0 means explicit none; code11
means absent specific-property inheritance. Codes1–6 retain no-left/right/straight/
u-turn/entry/exit and7–10 retain only-left/right/straight/u-turn. Conditions reuse
the strict bounded calendar compiler, and excepted modes return no program.
resolveTurnProperty validates domain and combines completed properties without
allocation, floating point or I/O. All132 valid combinations plus invalid-domain
unpublished output pass core tests. Paged storage and dynamic routing/history
integration are described below.
beginTurnRules/turnRulesStep now stream these two property ranges through one
embedded calendar cursor, freeze query UTC/offset, bound their combined count and
publish only after both finish. Inheritance11 is accepted only as the specific
fallback; all record actions (including inactive ones) are limited to0–10. Size/
epoch validation spans property transitions and publication, with explicit cancel
and restart. Host cursor/output are184/1 B, counted by ResourceOptions.turn_rules.
S3 begin/step frames are96/112 B; C3 are96/96 B, not complete task peaks. Busy,
explicit none, inactive inheritance, epoch replacement, count limits and cancel
tests pass in default/small. These generic file-range checks are supplemented
by the paged descriptors, relation topology and routing/history tests below.
encode_turn_program/decodeTurnRuleProgram now share a40-byte wire descriptor:
version u32=1, mode u8=1/2, three reserved zero bytes, then two16-byte ranges
(logical offset u64,count u32,fallback u32). Encoder bounds combined records and
64-bit offsets; excepted modes produce no program. Decoder validates complete
input before publication, including generic fallback0–10, specific0–11, record
bounds and the rule that foot's generic property is empty/none. Tests verify
encoded offsets/inheritance, overflow, reserved/mode fields, insufficient data,
record budget and unchanged output on errors. The descriptor feeds the paged
relation iterator and bound runtime transition evaluator described below.
TurnRulePrograms1120 and TurnRuleRecords1121 now have bounded begin/read-step
support. The caller-owned read cursor retains40 descriptor bytes, uses the shared
CRC cache and a page budget, validates section counts/strides and publishes only
after complete decoding. Host cursor/range output are96/32 B, plus a1-byte mode;
ResourceOptions.turn_program_read counts them. A test-only paged artifact marks
graph bit18 but omits required relation/context tables, preserving rejection.
Its104 descriptors exercise row24 crossing1KiB and row101 crossing4KiB payloads;
each read attempt with budget1 loads at most one page. Busy, cancellation, close
invalidation and Thursday none evaluation through section1121 pass. This adds
paged storage reading independently of integrated routing.
turn_rule_sections.py now produces relation storage for explicit directed paths:
1120 shared40-byte programs,1121 conditions,1122 relation rows24 bytes
(sequence byte offset u64,edge count u32,program row u32,group identity u64), and
1123 uint32 edge sequences. A group+mode must use one consistent program; multiple
targets retain a common group for future only-turn union semantics. Programs and
identical paths are deduplicated. Paths require2–64 connected mode-accessible edges,
groups positive uint64, and explicit rule/program/condition caps bound production.
Conditional programs affect topology identity and require graph bit18 and a bound
runtime evaluator. Snapshot-filtered topology cannot contain these rules.
Producer tests validate grouping/dedup, wire rows, paths, caps, flag and paged output.
Integrated runtime verification is described below.
OSM normalization is now implemented for supported node-via and ordered via-way
conditional relations in runtime_conditions mode. Programs validate every relevant
branch before edge sequences are expanded; modes with runtime properties omit
their static restrictions so those cannot stay active after a conditional none.
Relation IDs become group identities, multiple paths retain that group, and mode
exceptions trim whitespace consistently. Existing junction completeness,32-via,
64-edge and4096-path guards apply. Potential only targets outside mode topology,
mixed ambiguous u-turn targets, ambiguous node-via destinations and unsupported
member forms fail explicitly. XML/plain/dense PBF tests match node and three-edge
via-way output, excluded modes, inactive invalid grammar and exception handling.
These maps retain required bit18 and reject runtime routing without a turn reader.
Conditional relation expansion now checks positive uint64 group identity, a1–4096
path limit and potential member-pair count before expansion. Node incoming edges
are streamed; target selection retains at most one candidate and fails immediately
on ambiguity. Each path append checks the remaining capacity first, and via-way
expansion receives the remaining path budget rather than an independent full
budget. Tests cover exact path capacity, early over-cap rejection, member pairs
and invalid identifiers/budget types. These bound retained expansion output, not
the producer's complete OSM graph/process RSS or every graph-scan operation.
Conditional paths now contribute proper prefixes and their edge alphabet to the
existing route-history automaton alongside static via-way rules. Context-only
rules add no permanent no/only restriction: static permission masks still derive
only from static rules, and conditional permissions await runtime evaluation.
Graph history mode bits include active conditional modes. Combined rules are
capped4096, states65536, transitions4million and context*edge keys below the
reserved uint32 sentinel. Tests verify prefix transitions, shared prefixes,
conditional-only transitions remain statically allowed, and capacity rejection.
Core label layouts are unchanged; additional context states can increase actual
search-state use and must fit caller capacity. Bit18 requires condition evaluation
during history transitions.
The producer now emits context index1124 (first-match u64,count u32,reserved0)
and matches1125 (relation row u32,prefix length u32,next edge u32,completion0/1).
Each context retains every matching suffix/prefix, including overlapping lengths;
rows sort by relation group,mode,prefix length and relation row for future grouped
only-target evaluation. Relation row numbering agrees with1122 after excepted
programs are omitted. A prospective states*prefix-count work cap (default4million)
is checked before transition/match payload allocation. These two optional dynamic
sections add16 bytes per state and16 per match before physical paging; no runtime
RAM increase is established by these producer tables. Tests verify root/no matches,
multiple targets, overlapping prefix obligations and omitted relation row identity.
Their runtime evaluator and I/O/storage tradeoffs remain to be verified.
TurnDecisionCursor now combines evaluated sorted matches with O(1) retained
state per row: only choices union within group/mode/prefix, distinct obligations
intersect, and no actions reject only completed matching paths. None and excluded
modes impose no restriction. Ordering, action domains and same-group/mode program
consistency are checked before final publication. finish publishes once all rows
are processed; malformed/cancelled streams leave caller output untouched. Host
cursor is24 B plus bool output, counted by ResourceOptions.turn_decision. Tests
cover88 action/mode/completion/target cases, target union, overlapping prefixes,
distinct groups, ordering, action inconsistency, empty and cancelled streams.
S3 begin/match/finish frames32/32/32 B; C3 16/0/0 B, not full stack peaks. This
decision primitive is now connected to paged context/program iteration and
GraphReader history transitions through TurnContextReader.
decodeTurnRelation and decodeTurnContextMatch now validate the wire rows before
publishing typed output. Relations require positive groups,2–64 edges, aligned
uint32 sequence offsets, valid program references and complete sequence bounds.
Matches require valid relation/edge rows, prefix1–63 and completion flags0/1.
validateTurnContextMatch additionally checks prefix against the relation length,
completion against the final step and next-edge equality with the sequence's
expected edge. Tests exercise insufficient sequence bytes, alignment, invalid
flags/references, mismatched completion/next edge and unchanged error outputs.
These bounded allocation-free decoders are prerequisites for the paged match
iterator; TurnContextReader applies the resulting decisions during routing.
TurnContextReader now joins context index -> match -> relation -> expected path
edge -> paged program -> calendar evaluation -> grouped decision. Its fixed query
clock, package generation, work budget, match cap and per-program record cap are
explicit. Descriptor and evaluation cursors share phase storage; pending records
remain stable and only final complete decisions publish. Cancel discards abandoned
condition reads before restart. Current host reader is456 B plus bool output,
counted by ResourceOptions.turn_context. S3 begin/step frames96/80 B; C3 112/112 B
are individual frames, not task peaks. Actual produced three-edge context fixtures
pass48 time/mode/context/outgoing decisions with Busy, showing no rejection only
at completed paths and only restrictions at intermediate prefixes. Match budget
failure and cancellation preserve output. This standalone iterator still needs
GraphReader historical-transition integration, malformed/grouped-stream and real
navigation/resource evidence.
Paged iterator coverage now adds60 group/overlap decisions: same-group alternatives
union, independent group intersection and simultaneous repeated-edge prefix
obligations. Nine malformed artifacts retain valid page CRCs while corrupting
index range/reserved fields, match reference/flags/completion/next edge, relation
range, program domain or inactive inheritance action. They all return Invalid
without publication even after an earlier match rejects the outgoing edge.
Terminal errors repeat without new file reads. Cancellation/restart is exercised
after1–30 step attempts across descriptor/evaluation phases. Both profiles pass;
raw reports retain grouped/invalid counts and all runtime fixture digests. This
strengthens standalone iteration evidence without claiming integrated turn routes.
TurnContextReader::read now preserves in-flight progress for an identical
context/outgoing/mode/edge-count/budget request and reuses its completed result.
It does not restart terminal errors or cancellation for that request; explicit
begin starts a new attempt. validFor checks package identity/generation and exact
UTC/offset binding for future query composition. Tests repeat each completed
context decision with unchanged file-call count, reject clock/offset mismatches,
retain cancelled state and explicitly restart. No additional retained fields were
needed; host reader remains456 B.

GraphReader now accepts an immutable RuntimeRoutingRules binding with optional
road/turn readers and one explicit UTC/offset. It validates exact query/package
bindings and all required bit18 sections before use. Missing readers return
Unsupported; wrong query time/generation returns Incompatible. History transitions
retain their static next-context result while the conditional iterator advances,
then intersect permissions before publishing the next search key. No phase restart
or search-label array growth is introduced. Host GraphReader/GraphInfo remain
296/28 B; binding is24 B, counted by ResourceOptions.runtime_binding. The existing
road-only constructor remains available. All32 time/mode/float/integer algorithm
node routes on a produced three-edge via-way fixture pass with Busy and budget1,
including NoPath and exact success edges[0,1,2]. Grouped/clipped integrated routes,
combined road/barrier/turn queries and real Center turn production/resource
evidence remain to be verified. Older required flags still reject unsupported files.
Center runtime export now validates turn compiler modules and records supported
road/barrier/node/via-way calendar rules in its manifest. Actual CLI production of
the synthetic runtime-turn-center-v1 XML yields24,576 bytes at1KiB pages. The
resource probe constructs only readers required by present sections, using a
binding for turn maps. Host probe allocation/unique_ptr overhead is outside the
library budget.32 cold integer queries per profile pass exact edges[0,2,3] or
NoPath across four days,two modes,four algorithms, with new process/cache per query.
At32 search/output slots and retained ALT, default/small payloads are12,534/6,390 B.
The new turn cursor/output/binding contribute481 B over road/barrier-only budgets.
Read attempts20–102 include Busy, not physical SD reads. Reports retain Center
manifest/source/package/probe digests in center-runtime-turn-*-evidence.json.
This is actual Center production from synthetic data; real region/hardware peaks
remain unproven. The artifact does not supply DEM/contours/admin.
Integrated turn coverage now adds96 clipped-coordinate cases across two times,
two modes, three boundary scenarios and all float/integer algorithms. Starting
inside the from edge still contributes history; starting inside the via edge
without the from edge does not activate the relation, and ending before the final
edge does not complete a no relation. Successful float routes check clipped
fractions and both pipelines check leg/edge sequence. A distinct branch geometry
avoids ambiguous destination snaps. Another128 grouped node-route cases exercise
two same-group targets, independent group intersection, NoPath and exact output
edges with Busy/budget1. This adds no core retained storage. Combined dynamic
road/barrier/turn coverage and actual Center synthetic resource evidence are
described below. Real geographic conditional turns/barriers remain unverified.
Combined road/barrier/turn coverage now passes128 integrated cases on one produced
paged map with DEM/contour sections also retained. Four query days independently
exercise Monday turn closure, Tuesday foot road closure, Wednesday bicycle gate
closure and Thursday open/only behavior. Both modes and all float/integer algorithms
run node and clipped-coordinate queries with Busy/budget1, checking success edge
sequences. Closed departure roads return Missing during snapping, distinguished
from NoPath caused later in search. Both readers bind the same immutable clock;
wrong bindings are rejected. No core retained storage changed. Center synthetic
turn production and cold budgets are documented above; real-region joint-feature
and physical hardware evidence remain open.

Snapshot production now retains explicit restriction:none rather than removing
the property: removing a specific bicycle override had accidentally reinstated
the generic restriction. The importer skips explicit none while keeping mode
precedence. A Monday removal/Tuesday inheritance XML test proves the fix.

## Center runtime production and cold probes

Center's navigation plan now exposes RuntimeConditions, forwards --runtime-conditions,
validates required compiler modules and records policy in the manifest. Snapshot
ConditionAt is mutually exclusive. The actual CLI produced a 15,360-byte 1KiB-page
map from explicitly synthetic three-node XML with Monday direction overrides and
Tuesday foot gate closure. benchmark_center_runtime.py verifies source/package
manifest digests and runs 48 fresh-process integer queries per cache profile
(three times, two modes, two directions, four algorithms), checking exact edges
or NoPath. Each probe opens a new cache and injects Busy every other read attempt.

With 32 search slots, 32 uint32 output rows, integer ALT storage and EdgeRuleReader
retained, resourceBudget reports default12,053 B / small5,909 B library payload.
Every probe reserves ALT arrays even for other algorithms, so these are concurrent
retained-storage budgets, not minimal per-algorithm allocations. readAt attempts
range24–66 including Busy; they are not physical SD reads. Application/file objects,
stdio buffers, allocator overhead and whole-call-chain stack peaks are excluded.
No physical PSRAM/MCU, regional scale, terrain rendering or conditional-turn claim
follows from this small synthetic export. center-runtime-native-evidence.json and
center-runtime-small-evidence.json retain raw queries, source/package/manifest/probe
hashes and scope. Earlier real regional static-navigation evidence remains separate.
Descriptor read and condition evaluation now share an explicit phase scratch union.
Only the active cursor is cancelled; placement construction switches phase after
descriptor output completes, with no automatic aggregate copies. The current host
EdgeRuleReader shrinks from 704 to 528 bytes (176 bytes saved, 25%). Its separate
seven-range output remains alive while the union is repurposed. Cancel/restart and
actual paged condition tests pass in both cache profiles after this change.
The graph producer must eventually retain all potentially permitted directions,
and runtime evaluation must finish all relevant properties before publishing a
mask. Seven simultaneous streaming cursors are unnecessary: one caller-owned
calendar cursor can evaluate each property in sequence. No integrated navigation
workspace budget or real-map live-rule claim follows from this standalone policy.

Production-time snapshots also resolve node tags before barrier masks are applied.
Unresolved relevant barrier access conditions fail explicitly; conditions for an
excluded mode do not block that mode's export. Vehicle denial precedes explicit
bicycle overrides. Conditional foot/bicycle overrides on nondefault highway types
are validated before static filtering, so potentially reopened topology cannot
silently disappear. Importer tests cover open/closed gates, inactive invalid
clauses, excluded modes and override precedence; PBF dense/plain node fixtures
verify the same snapshot result as XML. These are producer snapshot checks, not
runtime conditional routing evidence. This change adds no MCU storage or code.

`runtime-calendar-native-evidence.json` and `runtime-calendar-small-evidence.json`
record actual C++ probe and input hashes. Each passes 146,097 civil days spanning
2000–2399, independently encoded by Python datetime, plus 15,300 supported grammar,
offset and overnight cases compared with the host evaluator. Total predicate
evaluations are 162,684 including disjunctions and invalid inputs. The complete
400-year test checks exact year/month/day and weekday simultaneously.

ESP32-S3/C3 core cross-builds pass with exceptions/RTTI disabled. Dedicated object
text is S3 896 / C3 826 bytes, data/bss zero, and the evaluator's individual frame
is 64 bytes on both. C3 unresolved dependencies contain only integer division,
with no allocator or floating-point helper symbols. These are module/compiler
measurements, not final firmware Flash increments, physical stack/RAM/PSRAM peaks
or latency/power measurements. Navigation now uses this evaluator through the
road and turn rule readers; see REAL_RUNTIME_NAVIGATION.md for the separate
real Center-produced conditional-road evidence.

The new runtime_calendar CTest entry runs the independent oracle. Full default
and small suites now contain 33 tests, including raw regional seams and joint
feature capacity checks. No hardware or real live-rule navigation
evidence is inferred from these primitive results.
