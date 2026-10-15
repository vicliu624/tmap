# Joint feature capacity and resource evidence

Current remeasurement: [four-profile report](current-full-feature-evidence.json).
Both 37-slot and 64-slot builds pass all 35 CTest entries after rebuilding the
latest core/tests, with optional host codec/geometry dependencies supplied.
The current configured joint payload is 9,176 B / 10,256 B respectively, 64 B
below the historical figures below after compacting persistent tree cursors.
Center joint queries still finish in 153 rounds / 145 read attempts. Default
32-slot configurations correctly reject this 37-section package. Historical
figures below identify the earlier cursor layout and must not be read as fresh
measurements. Real combined geographic evidence is now REAL_OUTDOOR_JOINT.md.

The producer and reader previously imposed a hard 32-section format ceiling.
The current complete combination needs 37: DEM, detailed/overview vector
contours, graph and road terrain profiles, ALT, routing profile, history,
runtime roads/barriers/turns, and polygon administration. The format ceiling
is now 64. Reader storage still defaults to 32, so existing workspaces remain
9,504 B default or 3,360 B compact on the measured host ABI.

Applications select their directory capacity explicitly. A valid package with
too many sections returns BudgetExceeded and leaves the package closed. The
test covers 37 real feature sections, a valid padded 64-section directory,
and a CRC-valid malformed header claiming 65. Unknown section types do not
require new cache storage. CMake exports the capacity; link-time ABI tests
individually reject mismatches in page size, page count and section slots.

For the current 37-section combination, choose 37 slots rather than reserving
64 unnecessarily. This is only five additional 40-byte directory rows on this
host ABI. The core still rejects workspace sizes above the unchanged legacy
Workspace ceiling. At 4 KiB pages, three cache pages permit at most 40 slots;
four cache pages remain rejected. Other ABI builds retain the sizeof guard.
The 4 KiB/three-page/40-slot core-only build and host stack gate pass; the
[41-slot configuration check](directory-ceiling-evidence.json) records the
explicit CMake rejection before any build starts.

| Measured profile, two cache pages | Workspace | Configured joint typed payload | Outcome |
| --- | ---: | ---: | --- |
| 4 KiB capacity, 32 slots | 9,504 B | — | Explicit capacity rejection |
| 1 KiB capacity, 32 slots | 3,360 B | — | Explicit capacity rejection |
| 1 KiB capacity, 37 slots | 3,560 B | 9,240 B | Joint services pass |
| 1 KiB capacity, 64 slots | 4,640 B | 10,320 B | Joint services pass |

These joint-service figures retain integer ALT with 32 search/output slots,
roads, barriers, turns and immutable query binding; integer partial-edge
elevation; contour renderer and 16 output segments; polygon administrative
query workspace and cursor. They include the two permission outputs reserved
by resourceBudget. The test asserts the exact total against the actual retained
service objects/arrays. File adapters, host driver objects/ownership pointers,
allocator overhead, task stacks, display/GPS and other services are additional.
This is not a minimum all-purpose navigation requirement or a board peak.

Each service advances with work budget one, in serialized round-robin calls
sharing one package/cache. Every other file-read attempt returns Busy. On the
Center package, all four services finish in 153 rounds/145 file-read attempts:
three route edges, three rendered contour segments, independently expected
500 cm ascent/zero descent to the first edge midpoint, and the exact two-level
administrative path. The query clock is Thursday at the Unix epoch; supported
conditional turn/road/barrier programs are present. The broader four-day/mode/
algorithm rule matrix remains in RUNTIME_CALENDAR.md and its synthetic evidence.
This joint test does not replace that matrix or the real Liechtenstein test.

Center produced its 39,936-byte/37-section package using explicitly synthetic
OSM XML and an explicit synthetic DEM/contour/admin document. Source coordinates
and DEM coverage match. Both inputs and all producer tools are hashed in its
manifest. It exercises the actual Center Core/CLI chain, without claiming this
small combined package is a real geographic dataset.

Raw evidence: [joint host report](full-feature-native-evidence.json),
[ESP32-S3 64-slot core](evidence-esp32s3-full.json),
[ESP32-C3 64-slot core](evidence-esp32c3-full.json).
Reproduce with `tools/collect_full_feature_evidence.py CENTER_ARTIFACT BUILDS...
--output REPORT`. Build configurations are recorded in the report. The separate
test `full_features` participates in CTest for both capacity rejection profiles
and profiles that can run the entire 37-section package.

Cross-builds disable exceptions/RTTI. Archive checks reject allocator and
exception/RTTI dependencies and enforce new individual frames <=240 bytes.
Compiler frames do not measure call-chain peaks, physical RAM/PSRAM, SD latency,
power or linked firmware Flash. Real geographic combined terrain/admin/rules
resource measurements and physical device integration still need their own
evidence; the current real navigation, mountain and regional experiments are
documented separately.
