# TMAP 2.0 outdoor profile, schema 1

All integers are little endian. Coordinates are signed WGS84 E7, ordered
latitude/longitude in geometry records. Bounding boxes are west, south, east,
north. Antimeridian-spanning regions are not supported by this profile; split
them before packaging. Display zoom and storage topology are independent.

## Container

The first 256 bytes are a header, padded to physical page size P (1024/2048/4096).
Directory starts at P. Body starts at align_up(P+64*section_count,P); sections
are P-aligned. At most 64 sections. Default P4096 retains original offsets.
Reader directory storage defaults to 32 slots and is explicitly configurable;
a valid package exceeding the selected slot count returns BudgetExceeded.
Directory opening streams fixed-size chunks with caller-selected work budgets.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 8 bytes | `TMAP\r\n\x1a\n` |
| 8,10 | u16,u16 | major 2, minor 0 |
| 12,16,20 | u32 | header size 256, endian 0x01020304, physical page size P |
| 24 | u64 | required container flags, zero |
| 32,40 | u64 | file length, directory offset P |
| 48,52 | u32 | directory count, row stride 64 |
| 56,72,88 | 16 bytes each | package ID, build ID, series ID |
| 104 | u64 | revision |
| 120 | 4 i32 | bounds |
| 136 | u32 | informational display zoom mask |
| 140 | u32 | profile capabilities: DEM=1, contours=2, graph=4, administration=8 |
| 160 | u32 | CRC32C of header with this field zero |
| 164 | u32 | CRC32C of used directory bytes |

Remaining fields are reserved, written as zero. Unknown required capabilities
return Unsupported. v1 readers reject major 2; compatibility is explicit,
not based on assuming existing readers ignore new capability bits.

Directory rows retain the v1-shaped 64-byte layout, with a distinct v2 meaning:
type u32 at 0; schema u16=1 at 4; flags u16=2 at 6; ID u32=type at 8; owner=0
at 12; physical offset/length at 16/24; count at 32; root=0 at 40; logical
payload bytes at 48; reserved u64=0 at 56. Unknown optional section types may
be skipped; all their pages can still be integrity-validated. Types are unique.

Each physical section page has a 32-byte header and up to P-32 payload bytes:
magic `TPG2` at 0, type u32 at 4, zero-based page ordinal u32 at 8, used payload
bytes u32 at 12, whole-page CRC32C at 16 (zero during calculation), reserved
zero at 20..31. Zero padding completes the P-byte page. Logical reads map
through these pages; section metadata offsets are logical, not physical.

The draft codec is raw paged DEM, delta/zigzag/varint contours, raw shared road
shapes. No unverified compression ratio is promised. Page-level decompression
can be added under a new schema with a bounded output requirement.

## DEM

1000 Terrain metadata: 64 bytes. west/south/step_lon/step_lat i32 at 0..15,
columns/rows u32 at 16/20, block side u16 at 24 (2..64), units u16=1 meter at
26, source nominal resolution u32 at 28, datum u32 at 32, reserved 36..47,
source identity 16 bytes at 48. Origin denotes the southwest sample center.

1001 DemIndex: 32-byte rows, block-row-major order. origin column/row u32 at
0/4, width/height u16 at 8/10, reserved u32 at 12, offset u64 at 16, byte
length u32 at 24, reserved u32 at 28. Boundary blocks may be smaller.

1002 DemData: signed 16-bit meter samples, row major from south to north.
-32768 is missing. Storage block size does not change source resolution.
Bilinear interpolation refuses a missing neighbor with nonzero weight.
Exact grid points do not require irrelevant missing neighbors. No extrapolation.

Profile sampling follows caller-provided line segments at a maximum configured
spacing, including original vertices/endpoints. Distance uses spherical geodesic
segment lengths; intermediate coordinate interpolation is regional linear E7.
Elevation deltas below the configured threshold are retained against the last
accepted elevation; accepted positive/negative deltas accumulate ascent/descent.
Missing data resets the filter, breaks the profile and returns Incomplete.

## Spatial trees

1010 ContourIndex and 1105 EdgeSpatial use packed 48-byte tree rows, root row 0.
Bounding box at 0..15; first child row u32 at 16; count u16 at 20; level u8 at
22; reserved u8 at 23; leaf payload offset u64 at 24; payload length u32 at 32;
height mask u32 at 36 and reserved u64 at 40. Contour row flag bit0 at 23
announces the mask; otherwise it is zero. Bits 0..19 represent height residues
0,5,..95 modulo 100; bit31 means an unclassified residue, which cannot be pruned.
Inner masks OR their children. EdgeSpatial flags/mask remain zero. Inner rows
have nonzero level, zero payload,
contiguous children after their parent. Child level decreases exactly one.
Leaf rows have level/first-child zero; count is payload record count. Writer
uses fanout 8. Readers use at most 24 caller-owned traversal frames.

## Vector contours

1011 ContourData: leaf payload is consecutive line chunks. Each line header is
24 bytes: stable feature ID u64, elevation meters i32, point count u32,
encoded geometry bytes u32, flags u32 (bit0 per-vertex display level). At least
2 points. Coordinate
stream is alternating latitude/longitude zigzag-varint signed deltas, starting
from (0,0) separately for every chunk. Five bytes maximum per scalar. If flag
bit0 is set, each coordinate pair is followed by one minimum display-zoom byte
(12..18). Chunk endpoints always use 12. Delta
and reconstructed coordinate bounds are checked before callback publication.

Writer simplifies in projected space with the declared pixel tolerance and
keeps nested display levels in one detailed geometry. It splits at fixed
storage cells (default z16), then at most 128 vertices, sharing exact E7 seam
vertices. Leaves group at most 16 chunks in the same cell/height band. A chunk's
ID and elevation are retained.
Device callbacks receive first/last flags to separate chunks, then project and
clip screen segments. The renderer controls line widths/colors and text.

Default display policy: z0..11 off; z12..13 interval 100m; z14..15 interval
20m/major 100m; z16..18 interval 10m/major 50m; other levels off. Display
filtering is a modulo test on stored height, not duplicated zoom geometries.
One detailed geometry is stored. Optional 1012/1013 overview index/data use the
same record encoding, simplified to z14, storage cells z14, and retain heights
divisible by 20 or 50. Readers select it for z12..14. This optional small display
cache avoids fine-data I/O; it is not a complete repeated zoom pyramid.
Fixed-buffer rendering retries a bounded sequence of coarser intervals and
reports the actual interval; exhausted capacity/operation budget discards the
incomplete snapshot. All data are vector lines drawn at runtime.

## Routing

1100 Graph metadata: node count u32, edge count u32, supported modes u32=3,
flags u32 (bit0 explicitly declared topology epoch), topology identity 16 bytes.
Edge count must be < UINT32_MAX-1. Declared identity hashes the producer source
epoch and static routing profile; otherwise the identity is package-specific.

1101 Nodes: 32-byte rows, stable ID u64, latitude/longitude i32, first edge u32,
edge count u32, optional elevation i32, flags u32 (bit0 elevation present,
bit1 coverage boundary). A exhausted search that reached a boundary returns
OutsideCoverage, rather than claiming the complete graph has no path.
Outgoing directed edges are consecutive; graph node references are row indices.

1102 Edges: 48-byte rows:

| Offset | Type | Meaning |
|---:|---|---|
| 0,4 | u32 | source/target node row |
| 8,9,10 | u8,u8,u16 | access mask (foot=1, bicycle=2), class, flags |
| 12,16,20 | u32 | length/ascent/descent centimeters |
| 24 | u64 | shared shape logical offset |
| 32,36 | u32 | shape point count, opposite edge or UINT32_MAX |
| 40 | u64 | road name reference or zero |

Flag bit0 reverses shared geometry traversal; bit1 marks known ascent/descent.
Unknown terrain must have zero stored summaries, but is not interpreted as flat.
1103 Shapes holds E7 coordinate
pairs, 8 bytes each. Writer reuses identical opposite-direction shapes.
Producer samples DEM at an explicit interval/threshold and derives ascent/descent.
Missing coverage makes the edge unknown; weighted planning returns Incomplete.
Explicit producer summaries are allowed when both ascent/descent are present.

1108 EdgeTerrainIndex optionally holds one 16-byte row per directed edge:
logical sample offset u64, sample count u32, reserved u32=0. Unknown/unavailable
profiles use zero count. 1109 EdgeTerrainData holds cumulative samples: distance,
ascent and descent u32 centimeters, 12 bytes each. Distances strictly increase;
first sample is (0,0,0), last agrees with edge length/summary. The reader binary
searches/interpolates prefix ascent to weight clipped start/end roads, preserving
a small continuation across Busy. 1110 EdgeTerrainPolicy stores spacing/threshold
u32 centimeters, datum u32, reserved u32=0, source identity 16 bytes. All three
sections must appear together. Partial costs without a usable profile return
Incomplete instead of distributing total ascent uniformly along an edge.

1104 Restrictions: sorted 16-byte rows: incoming edge u32, outgoing edge u32,
mode mask u8, kind u8 (0 prohibited, 1 only allowed exit), reserved u16/u32.
At most 64 entries per incoming edge. This is a static node-via profile, not
a claim of complete conditional/via-way OSM routing support.

1105 leaf payload points into 1106 EdgeRefs: u32 edge row indices. Reader
checks access before closest-point matching. 1107 RoadNames is optional:
u16 byte length + UTF-8 bytes; edge reference is logical offset+1, zero means
unnamed. Names are at most 512 bytes, excluding terminator.

Sparse route keys are arrival directed edge+1, with UINT32_MAX reserved for
the initial state. The caller supplies an open-addressed label table and an
indexed binary heap of equal fixed capacity. No state table proportional to
the complete graph is allocated. Cost is distance plus caller-selected ascent
penalty. Algorithms are bounded Dijkstra or directed landmark A*; hierarchy
shortcuts are not yet implemented. Initialization clears a bounded number of label slots per
step. Coordinate routing evaluates up to four legal direction pairs while
reusing the same arrays; initial/final arrival edges enforce turn rules.
`RouteLeg` output is 20 bytes: edge/first-segment/last-segment u32 and first/last
fractions float. Geometry callbacks include interpolated clipped endpoints.
No route geometry is secretly allocated inside the library.

Producer-side regional merging requires a common declared epoch and verifies
shared stable node coordinates, geometry and attributes. This yields one paged
graph connected across seams. Runtime graph compatibility is metadata-level;
it does not automatically build a cross-file graph or resolve stable IDs.

1111 LandmarkPolicy: 32 bytes, u32 K (1..8), node count u32, metric u32=1
(relaxed directed centimeter length), reserved u32=0, producer edge digest16.
1112 LandmarkDistances: node-major rows of K*(from_landmark u32,to_landmark u32),
8*K bytes per node, UINT32_MAX unknown/overflow. Sections occur together.
Relaxed graph permits all access and ignores restrictions/ascent. These are
lower-bound data, not replacement route geometry. The producer generates exact
integer distances; CRC protects bytes, not the truth of third-party lower bounds.

Optional administration capability bit8 uses sections1200..1203. 1200 is the
same 48-byte spatial-tree structure with no height mask. Leaf offset/count
addresses 1201 records (48 bytes each): bounds4*i32, geometry_offset u64,
ring_count u32, total_points u32, path_reference u64 (logical offset+1), path
depth u8, seven reserved zero bytes. 1202 geometry is ring_count repetitions of
point_count u32 followed by point_count*(latitude i32,longitude i32); every ring
is explicitly closed and contains >=4 points. Even-odd membership handles holes
and disjoint components; input is required to be valid polygonal data. No
antimeridian-crossing segments. 1203 paths: byte_length u16 + <=512 UTF-8 bytes,
without terminator. Hierarchy segments separated by ` / `, maximum32 levels.
Producer JSON administration is a list of {path:[segments],rings:[coordinate
arrays]}; input coordinates remain [longitude,latitude]. Exact containment uses
integer predicates without floating division. Boundary matches are inclusive;
equal deepest matches reduce to complete common segments and report boundary.
