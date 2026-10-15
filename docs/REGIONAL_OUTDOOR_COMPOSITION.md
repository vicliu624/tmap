# Outdoor data in virtual region packages

Current revalidation: the six optional geometry tests pass against both current
default and compact SDK inspectors. Reports are
current-regional-geometry-native-evidence.json and
current-regional-geometry-small-evidence.json. They are now included in CTest
when TMAP_TEST_GEOMETRY=ON, and CI installs the host requirements and enables
the gate. Both current profiles pass 36/36 entries with optional geometry/codecs
available. The standard-library-only default remains 35 entries. Installed SDK
tools now include requirements-geometry.txt as well as the Python scripts.

`virtual_regions.py` now retains outdoor layers while remapping regional graphs.
The producer combines compatible aligned DEM grids into one grid, retains and
deduplicates complete regional contour geometries and administrative features,
and rebuilds the paged spatial indexes. Missing contour geometry can be generated
from each source DEM using its declared interval. Explicit regional contours are
authoritative; exact matching chains can join, while missing spans are never extrapolated.
Available landmark tables are regenerated for the combined graph.

DEM step, resolution, vertical datum, profile spacing and threshold must agree.
Origins must align on the same E7 grid. Overlapping present samples must agree;
missing samples can be filled by another region. Gaps remain missing. Unaligned
grids, differing policies/datums and conflicting heights are rejected, requiring
explicit source reconciliation/resampling. Datum zero remains unknown; matching
zero does not independently establish a common measured vertical reference.

`--max-dem-cells` bounds the host mosaic before allocating it (default 4,000,000).
The bounding rectangle includes gaps, so distant regions can consume substantial
file space and host memory. This is not a sparse multi-grid DEM representation.
The composite source identity derives from all regional DEM document hashes.
Package/source reproduction and SHA checks remain required before runtime use.

Device terrain, contour and administrative readers consume the existing single
package sections through VerifiedVirtualFile. No region count, per-region cache,
resampling engine or new allocation is introduced into the MCU core. Required
virtual source/extent arrays are still caller-owned and separately budgeted.
Remapped and repacked pages can require a large patch; byte reuse is measured,
not assumed. Contour fragments and administrative polygons retain their source
coverage rather than inferring boundaries outside it.

Host tests reconstruct the byte-identical outdoor virtual package and verify
overlap, order-independent DEM data/source identity, retained layers, gaps,
incompatible grids/policies/datums, conflicting samples and cell limits.
C++ tests load its verified binary manifest with Busy storage, query a 25 m
interpolated elevation in the regional overlap, stream contours, resolve the
second region's administrative hierarchy and perform cross-region routing.
These are synthetic regional fixtures, not a new real regional survey or physical
MCU RAM/PSRAM measurement. Real Center single-region mountain/navigation evidence
remains separate. Exact contour stitching and optional administrative polygon
union are now available on the producer. Different grid origins and resolutions
can use explicit host resampling.

## Contour continuity and administrative union

When all terrain sources request automatic contours with the same interval,
contours are regenerated from the composed DEM. Their surface then agrees with
the resampled terrain and lacks artificial regional grid seams. Explicit contour
geometry remains authoritative; mixed policies retain regional geometry instead.
The stitcher quantizes to E7, removes duplicate undirected segments by elevation,
joins degree-two chains and preserves junction branches and closed loops. IDs are
regenerated deterministically. Different elevations and nonmatching endpoints
are not joined, and no missing span is extrapolated. Differently segmented
overlapping segments can optionally be noded/intersected as described below. The input segment
budget defaults to 4,000,000.

Set `administration_union: true` in the virtual configuration to union polygons
with identical normalized hierarchy paths. Different hierarchy paths remain
separate. Ring parity preserves holes and nested islands. GEOS performs union on
E7 coordinates; output is quantized back to E7 and invalid/collapsed results are
rejected. Internal seams disappear, so the SDK no longer reports a shared seam
as an administrative boundary. Ring/polygon ordering is deterministic, and the
manifest records Shapely/GEOS versions. The input/output vertex cap defaults to
4,000,000; it does not bound GEOS's native intermediate peak memory.

Polygon union is an optional host dependency, installed with
`python -m pip install -r tools/requirements-geometry.txt` in the producer's own
environment. It adds no MCU dependency or runtime workspace. Missing dependency
is an explicit error when union is requested, rather than silently preserving
seams. Invalid rings are rejected without automatic repair. Large GEOS workloads
can require significant host memory; no host peak-resource claim is made here.

Run `python tests/test_regional_admin_union.py --inspector <tmap_inspect executable>`
in that environment. Five optional tests pass with both default and small SDK
inspectors: adjacent/overlapping/disjoint regions, hierarchy separation, holes,
nested islands, validity/capacity, verified virtual-byte reconstruction and an
actual SDK query on the former internal seam with `boundary=0`. Evidence is
`regional-geometry-evidence.json` and `regional-geometry-small-evidence.json`;
these tests are separate from the dependency-free 29-entry CTest suite. The
updated evidence now contains six tests after adding contour noding.

Set `contour_noding: true` to enable general same-elevation segment intersection
and overlap removal with the same optional Shapely/GEOS producer dependency.
Different segment subdivisions are reconciled before degree-two stitching.
Intersections remain junctions; different elevations are processed separately.
GEOS intersection coordinates are rounded back to E7, duplicate/zero-length
segments are removed and the output segment count is capped. Sub-E7 detail can
collapse during rounding. This is a quantized geometric model, not preservation
of arbitrary sub-centimetre topology. The manifest records the flag and engine
versions. Input/output caps do not bound native GEOS intermediate peak memory.

## Explicit host resampling

The virtual-region configuration can include `dem_resampling`:

```json
{
  "dem_resampling": {
    "origin": [0, 0],
    "step": [0.0005, 0.0005],
    "columns": 7,
    "rows": 3,
    "resolution_m": 100,
    "overlap_tolerance_cm": 0,
    "max_sample_evaluations": 32000000
  }
}
```

This matches the synthetic regional fixture. The target must cover all input
grids. Different steps/origins/resolutions are allowed, but vertical datums must
match; zero remains unknown. Target resolution cannot overstate the coarsest
source resolution. Profile spacing/threshold can be set explicitly; differing
source thresholds require an explicit common threshold.

Bilinear interpolation uses rational E7 weights and packed signed-metre source
samples, followed by nearest-metre half-even rounding. Missing contributing
corners yield a missing sample; zero-weight corners do not invalidate an exact
node/edge sample. No extrapolation fills uncovered locations. An available source
can fill another source's hole. Overlaps must agree within the explicit centimetre
tolerance (default exact). Finest declared source resolution wins; grid area and
source digest deterministically break ties. There is no blending or automatic
vertical-reference correction.

Cell and target-cell × source-count work budgets are checked before allocation
and sampling. Source documents, target configuration and method enter the composed
identity; the manifest records configuration. Resampling changes the stored
elevation model and can change climb costs; it cannot create measured detail.
The computation and rectangular storage costs occur on the producer, without
additional MCU resampling workspace.

Tests cover planar interpolation, unaligned origins, different resolutions,
ordering, holes, overlap tolerance, datum/detail rejection and both budgets.
The verified C++ virtual-file fixture now combines different grid steps with an
explicit target and passes terrain/contour/admin/routing queries with Busy.
