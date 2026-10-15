# Queue-free integer label-scan Dijkstra

`IntegerRouteAlgorithm::LabelScanDijkstra` scans existing sparse labels instead
of retaining a frontier queue. Labels remain 24 B and the cursor 184 B on the
measured host ABI. The existing final label word retains frontier membership;
the variant removes four bytes per search slot at identical output capacity.

Each work item examines one slot, including empty/closed slots. The selected
minimum survives Busy node/turn reads. Exact costs and arrival-key ties match
queued linear Dijkstra. Node/boundary and prepared/clipped coordinate pipelines
share cancellation and fixed rule clocks. Existing defaults remain unchanged.

```cpp
tmap::IntegerRouteOptions options;
options.algorithm = tmap::IntegerRouteAlgorithm::LabelScanDijkstra;
tmap::IntegerRouteWorkspace workspace{labels, nullptr, label_capacity};
```

Retain labels/output while advancing with explicit work budgets.
`IntegerRouteStorage::beginLabelScan(states, outputs, max_payload_bytes)` reserves
only labels/output, preferring PSRAM without silent internal fallback. Staged
tests prove two external requests and rollback at either failure. Floating
storage cannot use this method. The owner adds 76 B on this host ABI; include
owner/provider controls in `ResourceOptions.application_bytes`. Set
`ResourceOptions.integer_label_scan=true` to omit queue storage from the budget;
it requires integer states and rejects simultaneous ALT storage.

## Actual Center map measurements

Monaco uses eight deterministic endpoint pairs, capacities 2,048/10,000 and five
methods, yielding 80 default/normal cases and 80 compact/alternating-Busy cases.
Outcomes, exact costs, output counts, states and expansions match across cache/
retries. Label scan matches queued linear Dijkstra at identical capacities.

| Tested all-query configuration, default-cache host ABI | Typed payload | Continuation calls |
| --- | ---: | ---: |
| Queued integer linear Dijkstra, 10,000 states | 330,184 B | 4,946–226,969 |
| Integer heap Dijkstra, 10,000 states | 330,184 B | 1,750–8,188 |
| Queue-free label scan, 10,000 states | 290,184 B | 631,750–13,769,617 |
| Integer ALT4, 2,048 states | 84,044 B | 307–1,451 |

Label scan saves 40,000 B (12.1%) but scans every slot. It does not reduce
expansions or I/O versus the same queued order: successfully read bytes range
874,432–20,387,776. Only ALT completes all eight queries at 2,048 slots; other
methods complete two. ALT has the smaller tested all-query RAM/work configuration
here while needing landmark tables/bounds. Label scan is an option when queue
storage is the relevant constraint, not a universal default. Continuation counts
measure scheduling work, not equal CPU cycles, time or energy.

Liechtenstein retains 32 state/output slots. Each cache profile checks 144 real
conditional edge permissions and 160 routes, 80 per method. Both methods return
42 successes, 10 NoPath and 28 BudgetExceeded, with equal edges/costs/states/
expansions. Label-scan payload is 11,665 B default / 5,521 B compact. This probe
retains ALT scratch for older methods: the 388 B saving from 12,053 / 5,909 B
includes 128 B queue, 128 B bounds and 132 B ALT control. That differs from the
queue-only saving in the Monaco table.

Everest/HGT adds clipped coordinates, climb weighting, integer elevation totals
and maneuver oracles: 40 cases per cache profile across five algorithms, both
directions, penalties zero/five and normal/Busy. Compact label scan uses 10,016 B
versus heap 10,528 B at 128 states, exactly 512 B saved. All checks pass; this is
packed-data consistency, not surveyed elevation accuracy.

Also passed: 24 independent reference cases, 16 combined road/barrier/turn node/
clipped cases with null queue, and default/compact/37-slot CTest 33/33. S3/C3
core and owner-template cross-compilation pass. Largest new individual frames
remain 192/240 B. Archive object-text sums grow 299/260 B versus the previous
recorded 64-slot core, without data/bss or forbidden allocation/exception/RTTI
dependencies. This does not measure final linked firmware Flash.

Evidence: [integration/source identities](label-scan-integration-evidence.json),
[Monaco default](real-monaco-label-scan-native-evidence.json)/[compact](real-monaco-label-scan-small-evidence.json),
[Liechtenstein default](real-liechtenstein-label-scan-native-evidence.json)/[compact](real-liechtenstein-label-scan-small-evidence.json),
[Everest default](real-everest-label-scan-native-evidence.json)/[compact](real-everest-label-scan-small-evidence.json),
[S3 compiler](evidence-esp32s3-label-scan.json)/[C3 compiler](evidence-esp32c3-label-scan.json).

Payload excludes native adapters, host ownership pointers, allocator metadata,
task stacks, display/GPS and other services. PSRAM tests simulate policy/failure.
Physical RAM/PSRAM peaks, full stack watermarks and SD latency/power remain
unmeasured. Few-KiB devices cannot infer arbitrary routing support from a
queue-free name or small control object.
