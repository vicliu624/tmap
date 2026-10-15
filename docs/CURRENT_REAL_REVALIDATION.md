# Current-reader real-map revalidation

The current C++ probes were rerun after the shared-cache continuation fixes,
resource-budget overflow correction and ownership/installation test additions.
This separates fresh evidence from historical reports produced by older probes.

| Workload, per default/compact profile | Current result |
|---|---|
| Real Everest/HGT integer coordinate routing | 40/40 cases pass across five algorithms, both directions, climb penalties 0/5, normal/Busy |
| Real Liechtenstein conditional edge permissions | 144/144 observations agree with supported host calendar policy |
| Real Liechtenstein conditional routes, 32 slots | 400 checked outcomes: 218 Ok, 50 NoPath, 132 BudgetExceeded |
| Legacy world/China | Exact output equality with the Trail Mate baseline probe, five repetitions each |

Mountain checks include snapped/clipped legs, integer distance/ascent/descent,
cost, maneuver kind/name/location/distance, and cross-algorithm agreement.
Country checks include host reachability for completed outcomes and active-road
permissions/continuity. Default and compact profiles agree on result, route,
cost and relevant state/expansion counts. Capacity failures remain failures to
find a route under that budget; they are never counted as successful navigation.
Agreement among algorithms is not an independent shortest-path proof; the
separate synthetic Bellman-reference suite provides that coverage.

Reports:

- `final-everest-coordinate-{native,small}-evidence.json`
- `final-real-runtime-{native,small}-evidence.json`
- `final-baseline-world.json`, `final-baseline-china.json`
- [cross-profile reconciliation and current core source hashes](final-real-profile-reconciliation.json)

Reconcile them with `tools/check_final_real_reports.py --output REPORT`.
The individual benchmark scripts reproduce the probe/oracle runs.

The exact mountain artifact is
`everest-real-complete-dem-1k-alt4-shared-terrain-v1`, SHA-256
`65ed9ebb4588cfd64b468348d5c9f86da24ece2cce7d491041d27696f6f5d151`.
An initial rerun selected the earlier non-shared-terrain artifact; its stored
hash still matches its manifest, but the current producer cannot reproduce it
byte-for-byte, so the benchmark correctly rejected it. The successful reruns
use the artifact identified by the earlier integer-coordinate reports and keep
both package hash and current-producer reconstruction checks enabled. Historical
producer layout should not be conflated with current reproducibility.

The China legacy checkpoint reads at offset 21,634,830,336 in a 29,837,557,760 B
file, exercising 64-bit offsets. Equality includes tile CRC, annotations, search
and page counts. Desktop process times are recorded, not interpreted as MCU
performance or SD latency. Physical RAM/PSRAM peaks, final linked firmware
Flash, task stack watermarks and power remain unmeasured.
