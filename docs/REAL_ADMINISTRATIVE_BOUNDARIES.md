# Real administrative component and boundary checks

The actual Center-produced Liechtenstein outdoor package contains 14 selected
OSM relations represented by 33 polygon components. The C++ `tmap_inspect region`
command is checked against GEOS using the original imported E7 polygon rings,
without loading a replacement geometry into the SDK.

For **each of all 33 components**, the test selects a representative interior
point, the first outer-ring vertex and four points one E7 unit east/west/north/
south of that vertex. Thus each cache configuration runs **198 cold-process
queries**, including deliberate outside positions. The same location may appear
more than once when components share a vertex; these are query observations,
not 198 distinct geographic locations.

| Result | Default | Compact |
|---|---:|---:|
| Successful hierarchy | 179 | 179 |
| Expected Missing | 19 | 19 |
| Boundary flag true | 33 | 33 |
| More than one deepest component matches | 32 | 32 |
| Oracle mismatches | 0 | 0 |

The oracle finds all covering polygons, retains the deepest hierarchy, computes
its common path across matches and checks the boundary flag. The returned
hierarchy depths span 0 (outside), 1 (country), 2 (district) and 3 (municipality).
This tests shared-boundary/common-hierarchy behavior as well as disconnected
components and points next to boundaries. No surveyed positional accuracy is
implied: the contract being verified is containment in supplied OSM geometry.

These administrative-only queries do not inject Busy. Separate
`shared_cache_progress` and the real joint outdoor workload cover retained
progress under Busy and cache eviction. The experiment adds no MCU storage or
geometry-engine dependency. Shapely/GEOS are optional desktop oracle tools.

Evidence, including every query, I/O counts, package/document/probe/source
SHA-256 and geometry-engine versions:
[default](real-administrative-boundaries-native-evidence.json),
[compact](real-administrative-boundaries-small-evidence.json).

Reproduce after building the SDK inspector:

```sh
python tools/check_real_administrative_boundaries.py CENTER_ARTIFACT TMAP_INSPECT \
  --geometry-python-path HOST_SHAPELY_DIRECTORY --output evidence.json
```

Scope is explicitly sampled: it does not exhaust all 14,374 vertices, every
edge/intersection, every possible coordinate, or arbitrary other countries.
Synthetic fixtures separately cover holes and malformed geometry. The oracle
and SDK use different point-in-polygon implementations but share the supplied
source geometry; this cannot detect incorrect OSM boundaries.
