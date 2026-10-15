# Bounded DEM elevation profiles

The library samples a caller-supplied geographic polyline: two points define a
straight E7-interpolated terrain section, while a route geometry defines a route
profile. It does not infer a walkable path between two points. Supply actual
routing geometry when ascent along a trail is required. Dateline-crossing
segments are explicitly unsupported.

`beginProfile` / `profileStep` are the allocation-free streaming primitives.
They deliver distance, elevation validity and gap flags through `ProfileVisitor`.
`ProfileMetrics` retains cumulative ascent/descent, extrema, sample counts and
valid covered distance. Missing DEM coverage and no-data values break the
statistics filter; heights are never joined across unknown terrain. Terminal
`Incomplete` remains `Incomplete` on repeat calls.

`ProfilePlot` is an optional fixed-capacity reducer over caller-owned columns.
It retains each column's first/last height and minimum/maximum envelope. A gap
marks the entire column disconnected conservatively. Renderers must honour
`gap`/`break_before` and retain empty-column gaps. Statistics come from the full
sampling stream, not the reduced plot. This prevents display resolution from
changing ascent totals. At most the configured number of samples is accepted;
overflow is `BudgetExceeded`, never silent truncation.

`ProfileJob` combines package opening, a terrain reader, incremental sampling
and the plot reducer. It requires no graph reader or routing workspace. Its
explicit MemoryProvider owns five bounded allocations: package workspace,
package reader, terrain reader, profile cursor and display columns. The default
policy is PreferExternal: available PSRAM is requested first and an allocation
failure does not fall back into internal RAM. Without external memory the caller
can use internal storage or explicitly require external memory. The provider and
file outlive the job; supplied points remain immutable until reset/cancel.

```cpp
#include <tmap/profile_job.h>

// provider and file are application adapters; keep the job off tiny task stacks.
tmap::ProfileJob job(provider);
tmap::GeoPoint path[] = {{471450000, 95160000}, {471450000, 95290000}};
tmap::ProfileJobOptions options;
options.columns = 96;
options.maximum_samples = 8192;
options.maximum_bytes = 16 * 1024;
auto state = job.begin(file, path, 2, options);
// Schedule calls cooperatively; Busy/More must be retried without restarting.
while (state == tmap::Result::More || state == tmap::Result::Busy)
    state = job.step(1);
if (state == tmap::Result::Ok || state == tmap::Result::Incomplete) {
    // Read job.metrics(), job.columns() and job.columnCount(). Incomplete means
    // the curve and totals have missing coverage; show that explicitly.
}
job.reset(); // cancel() also releases buffers and revokes published pointers.
```

Defaults are 30 m spacing, a 3 m noise threshold, 96 columns, 8192 samples and
1024 input vertices. Columns are capped at 4096. Input validation scans at most
the caller's explicit maximum_points; step work is bounded by the supplied
budget. A storage operation can still block in the adapter: this is cooperative
work accounting, not a hard real-time deadline. `requiredBytes(columns)` excludes
the small owner, borrowed points, allocator headers, adapter, task stack and UI.
Applications must account for those separately and choose a finite maximum_bytes.

The noise filter accumulates change against the last accepted elevation, then
accepts a new baseline only when the threshold is reached. It resets at a gap.
Sub-threshold endpoint residue is not forced into totals. Zero disables this
filter. This policy is deterministic but is not a surveyed terrain measurement;
DEM resolution, sampling spacing and noise policy affect results.

## Real-file evidence

`real-profile-evidence.json` records 48 C++ cases using the actual Center
GUI-produced OSM/Skadi map. Default and compact builds use 16, 96 and 240 columns,
forward/reverse sections, partial coverage and sample exhaustion. Every case is
repeated with Busy every second read, preserving the semantic result. Six DEM
statistics agree within 1e-6 m with the separate Center implementation and its
recorded reference section. Source package, probes and reference hashes are
retained. This small section is not a large-region or physical MCU benchmark.

The inner real section is 983.173 m long, rises from 477.598 to 739.756 m and
reports 262.158 m ascent, zero descent and 34 samples. The 96-column default
probe records 13,872 B provider payload plus a 160 B owner and 16 B borrowed
endpoints on the tested 32-bit host ABI. Compact two-page 1 KiB storage reduces
the provider payload by 6144 B to 7728 B. Increasing sample count within the bound
does not grow storage. Display columns do: 16/96/240 columns cost 640/3840/9600 B
on that ABI. These figures exclude file/provider objects, allocator overhead,
application/UI and cumulative task stacks. A few-KiB device must not infer that
the complete service fits from the cursor size.

`tests/profile_compile.cpp` instantiates the owner on the actual ESP cross
compilers. GCC14 -Os -fno-exceptions -fno-rtti -fstack-usage reports a maximum
new owner-instantiation frame of 80 B on S3 and 160 B on C3 (1 KiB cache), below
the 240 B gate. These are individual static frames, not call-chain watermarks.
Core-only and installed-consumer gates remain separate tests.
The retained owner-frame entries are in `profile-stack-evidence.json`. Default
and compact Release CTest each pass 38/38, including installed consumers. Center
passes 133/133 tests; GUI/native file-picker manual acceptance remains separate.

Reproduce with `tools/benchmark_profile.py PACKAGE DEFAULT_PROBE COMPACT_PROBE
CENTER_PROFILE_JSON REPORT`. The probe arguments are file, start latitude E7,
start longitude E7, end latitude E7, end longitude E7, spacing, columns, sample
limit and Busy period. The tests exercise external-only allocation rollback,
internal-only availability, bounded output, cancellation/restart and missing DEM.

Center's finished outdoor preview offers an English/Chinese elevation profile
dialog with numeric endpoints, configurable spacing/noise, axes, cumulative
statistics, coverage and cancellation. Wiki screenshots use English. Device
firmware integration is a separate task; this capability does not modify or
upgrade Trail Mate firmware.
