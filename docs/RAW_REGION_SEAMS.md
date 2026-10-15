# Original OSM regional seams and verified multi-file navigation

Regional graphs can segment the same source way differently. A node inside a
main road shape may become a junction only when a neighbouring branch is loaded.
Combining the existing node/edge lists cannot infer this connection. Coordinate
proximity is also insufficient: distinct stable OSM nodes at the same position
may represent different levels. The producer now unions original XML/PBF
entities by stable identity before constructing junctions and turn restrictions.
This adds no MCU topology table or new core object storage.

`import_osm.py INPUT OUTPUT --region-input OTHER --topology-epoch EPOCH` accepts
additional original sources. Overlapping nodes must agree at E7 and on tags;
ways must agree on ordered node references and tags; relations must agree on
ordered type/reference/role members and tags. Metadata versions/timestamps are
not inferred as a compatibility contract: the caller explicitly supplies the
compatible snapshot/profile epoch. Conflicts fail instead of choosing one
region. Equal coordinates with distinct node IDs are not joined. XML and
dense/plain-node PBF can be mixed. The retained unique entity/reference limits
default to 1,000,000 / 8,000,000 and are explicit host producer bounds, not MCU
memory estimates. Host producer RAM peaks remain unmeasured.

Center's navigation plan now accepts `RegionalPbfPaths` alongside `PbfPath`;
the legacy field names accept original XML as well as PBF. Relative paths are
resolved from the plan. `MaximumRegionEntities` and `MaximumRegionReferences`
forward the bounds. Center retains read-only handles for all originals during
production, hashes every source, and records the regional policy. Up to 32
additional sources are allowed. Duplicate paths and invalid limits fail before
producer access. Existing single-source plans retain their behaviour.

The graph-only merger also now retains/deduplicates `conditional_turns` and
ALT count. It rejects inconsistent tags within a shared relation group/mode.
Without original sources, its shared-node mode still requires that graph
segmentation already exposes the intended junctions; it cannot reconstruct
missing shape-node identities from coordinates alone.

For source/patch virtual maps, `virtual_regions.py` accepts `raw_source_groups`
in its config, one list of original files for each regional document/package.
Each package must reproduce its document; each source group must reproduce
its graph semantics at the selected mode/snapshot/runtime policy. The producer
then unions those originals and remaps the resulting graph while retaining the
existing terrain/contour/admin composition. Raw source identities and policy
are recorded. A mismatching raw source is rejected. The MCU verifies the trusted
binary manifest, every regional source/patch and the full virtual digest before
using the usual graph API. No MCU-side raw OSM parsing is introduced.

## Actual real-way evidence

Original source is the unchanged Geofabrik Liechtenstein 2026-10-13 PBF,
SHA-256 `07f3884fd1dbe647d6ff68ec3fc66725a77beb4b20eab792431fc4bc9ce8c97e`.
The preparation script deterministically selected actual OSM ways **6065793**
and **1324063797**, with shared node **12252504377**. Complete original way
references, coordinates and tags are retained in each selected XML source.
These are two selected actual ways, not complete geographic region extracts.
They use a foot profile; this test supplies no DEM or invented turn conditions.

Center independently exports main, branch and original-source union packages.
The main package omits the shared interior point from its graph nodes; the union
creates the actual junction. Tests run all four integer algorithms in both
directions, using a cold process/cache and Busy every other physical read:

| Query target | Cases per cache profile | Expected and observed result |
| --- | ---: | --- |
| Naive graph-list union | 8 | NoPath, disconnected control |
| Center original-source union | 8 | Success, two continuous legal edges |
| Raw-seam materialized reference | 8 | Same successful edge sequence |
| Verified regional source/patch view | 8 | Same successful edge sequence |

Source packages are 12,288 and 11,264 B. The virtual/reference map is 12,288 B;
patch is 10,240 B, with 2,048 B reused and four extents. Retaining sources plus
patch costs 33,792 B, 2.75 times the materialized map, before manifest storage.
This experiment supports connectivity and bounded runtime ownership, not a
storage-saving claim. A single materialized map avoids those retained sources
when independently stored regions are unnecessary.

| Current host ABI, 32 search/output slots, retained ALT | Default cache | Compact cache |
| --- | ---: | ---: |
| Direct union/reference typed payload | 11,468 B | 5,324 B |
| Verified view, 3 sources/4 extents included | 12,300 B | 6,156 B |

The view adds 832 B of retained library controls/arrays in this configuration.
Cold verification takes 1,466 physical read attempts including Busy; verification
plus route takes 1,478–1,488. Direct Center union routes take 12–22 attempts.
These counts are deliberately injected retry work, not SD latency or energy.
File adapters, ownership pointers, host driver, allocator metadata, task stacks,
display/GPS and other services are excluded. No physical RAM/PSRAM peak is inferred.

Reports: [default real-way cases](real-raw-seam-native-evidence.json),
[compact real-way cases](real-raw-seam-small-evidence.json). They include original
selection, input/Center/package/probe/tool identities, all failures, virtual
layout and per-query resources. Reproduce with `prepare_real_seam_sources.py`,
the three resulting Center plans, then `benchmark_real_raw_seam.py`.

Separate Center synthetic sources also test a cross-source Monday turn closure,
Tuesday road closure, Wednesday gate closure and Thursday opening in both
directions/all integer algorithms: 32 cases per cache profile. See
[default conditional seam](raw-regions-center-native-evidence.json) and
[compact conditional seam](raw-regions-center-small-evidence.json).
No real conditional turn/barrier evidence is claimed from these synthetic tags.

The whole-country mixed-input overlap regression also succeeds: Center imports
the original PBF plus both selected original-way XML overlays under the original
foot/runtime epoch. Its 8,091,648-byte output is byte-for-byte identical to the
single-source package, with 20,432 nodes and 51,736 directed edges. See
[country identity comparison](real-raw-union-country-evidence.json).
Updated default/compact probes repeat all 144 conditional edge checks and 320
route queries each: 176 successful, 40 NoPath and 104 explicit BudgetExceeded,
unchanged from the original map. Reports are
[default country remeasurement](real-runtime-raw-union-native-evidence.json) and
[compact country remeasurement](real-runtime-raw-union-small-evidence.json).
This proves overlapping mixed-format inputs preserve this original map, not
that a second complete neighbouring geographic dataset was evaluated.

Seven raw-topology unit tests additionally cover source ordering, same-coordinate
grade separation, coordinate/tag/member conflicts, retained limits, mixed PBF/XML,
conditional group preservation and raw-patch provenance rejection. Default and
compact CTest contain 33 entries; Center's Tmap-filtered tests pass 21. Core ABI
and ESP compiler budgets remain unchanged because these topology changes occur
in the producer. Complete neighbouring geographic extracts, combinations with
real DEM/admin/conditions, and physical board execution still need their own
measurements.
