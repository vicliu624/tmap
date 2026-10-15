# Rejected contour decode window experiment

2026-10-15: the bounded 48-byte cursor scratch was experimentally reused as a
decode window across vertices instead of reading at most 10/11 bytes per
vertex. Object sizes were unchanged. Both default/compact 35-test suites and
55-case real joint workloads passed; target compiler/dependency gates passed.
Nevertheless the change was **reverted** because real joint I/O increased.

| Sum over 55 cold joint cases | Baseline | Experimental window |
|---|---:|---:|
| Default read attempts including Busy | 528,125 | 547,277 |
| Compact read attempts including Busy | 528,235 | 547,387 |
| Successful transferred bytes, both profiles | 270,428,160 | 280,233,984 |
| Round-robin rounds, both profiles | 372,613 | 372,405 |

The window changes interleaved cache access, so fewer decode refills did not
translate to fewer media reads. Approximately 3.6% more transferred bytes is
not justified by 208 fewer rounds. This is a workload result, not a universal
claim that prefetching is harmful. RAM is unchanged; no hardware time/power
claim is made.

Baseline reports are `real-outdoor-joint-{native,small}-evidence.json`.
Experimental reports are `real-outdoor-window-{native,small}-evidence.json`,
with source/probe hashes and all cases. Compiler reports named
`evidence-*-window.json` apply only to the rejected experiment; the retained
implementation is represented by `evidence-*-joint.json`.
