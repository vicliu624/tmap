# Real regional navigation evidence

The benchmark verifies the existing Center Everest OSM/HGT artifact's package
SHA and document reproduction, then partitions its actual DEM columns into two
overlapping crops and directed edges into two groups. This is SDK benchmark
partitioning of a Center artifact, not two independently exported Center regions.
Regional road geometry can extend beyond its cropped DEM; the final mosaic
restores complete terrain coverage. The source input is restriction-free; the
benchmark explicitly rejects inputs whose restriction operands could be lost.
Separate original-source seam evidence is now in RAW_REGION_SEAMS.md: actual
selected OSM ways are independently Center-exported, then their original sources
rebuild a junction omitted by the main-only graph. Verified source/patch routing
and the naive disconnected control are measured without adding MCU topology state.
Those samples are selected real ways; this mountain experiment remains a partition
of one Center artifact and does not inherit a broader geographic coverage claim.

Production writes two regional packages, a patch, a binary trusted manifest,
extent count and a materialized reference in a new ignored directory. Python
reconstructs every virtual byte and checks packed-profile arithmetic. The C++
probe uses VirtualMapCursor/VerifiedVirtualFile to verify the manifest, every
source and patch, and the whole virtual digest before opening the package. It
then runs integer coordinate search, clipped elevation statistics and maneuvers
with 128 state and leg slots. Both directions and normal/every-second-read Busy
are measured, each with a cold verified open. Every output equals the direct
materialized-reference route, including leg intervals and maneuver events.

Current raw evidence:

- `real-everest-regional-navigation-report.json`: source production and arithmetic check.
- `real-everest-regional-native-remeasure-report.json`: current default-cache probe.
- `real-everest-regional-small-remeasure-report.json`: current small-cache probe.

Remeasurements recheck immutable source/reference/manifest identities and compare
outputs with the checked production baseline. Reports record executable and
baseline/source hashes. Source mutation is not exercised here; synthetic epoch
change tests remain separate. These desktop files are treated as immutable for
the duration of each measurement.

## Resource and storage results

At three sources and twelve extents, retained virtual verification/view control
and arrays add 1,088 bytes on the current host ABI. Total concurrent library
payload is 17,760 bytes default or 11,616 bytes small, including the loader,
128-state/128-leg coordinate pipeline, elevation and maneuver controls. Direct
reference payload is 16,672 / 10,528 bytes. ResourceOptions.virtual_sources and
virtual_extents now include the actual target ABI arrays and controls. No source
file adapter, stdio buffers, probe event array, allocator or application overhead
is inferred. These are retained payloads, not MCU peak RAM/PSRAM or task stacks.

The virtual map is 1,363,968 bytes. Regional sources are 736,256 and 657,408 bytes;
the patch is 1,348,608 bytes. Only 15,360 bytes (about 1.13%) are reused. Retaining
both regions and patch totals 2,742,272 bytes, roughly twice a materialized map.
This partition/repacking example does not justify a storage-saving claim. A
materialized map is the lower-storage choice when retaining independent regional
files is unnecessary. Other regional layouts require their own reuse measurements.

Cold verification makes 64,185 source/manifest adapter read attempts normally or
128,366 with Busy. Verification plus query totals 64,258–64,260 attempts default
or 64,259–64,261 small normally; Busy totals 128,516–128,520 / 128,518–128,522.
Successfully transferred adapter bytes range from 4,181,680 to 4,185,776 across
the measured directions/retry cases. The low-memory SHA continuation uses small
reads; verification dominates this tiny graph's query work.

The report's `read_attempts`/historical `verification_physical_reads` naming
means source-level RandomAccessFile.readAt attempts rather than composite-view
requests. It does not measure OS disk operations, SD sectors, latency or energy;
stdio and OS caching remain in the desktop path. `virtual_read_attempts` is
reported separately. Do not re-verify on each route if the application can retain
the verified immutable view; changed sources require the identity/epoch contract.
No physical board evidence or independent survey accuracy is claimed.

Default/small full CTest pass 29/29 and ESP32-S3/C3 core cross-builds pass after
virtual-array budget integration. The resource budget function's C3 compiler
frame is 64 bytes. This does not establish peak call-chain stack usage.

Reproduction uses `benchmark_real_regional_navigation.py` with a Center artifact,
a new output directory and the SDK integer coordinate probe. Remeasure another
cache build with `remeasure_real_regional_navigation.py` using the unchanged
directory and production report. Reports clearly separate cold verification
from subsequent navigation and preserve the unfavorable storage/I/O results.
