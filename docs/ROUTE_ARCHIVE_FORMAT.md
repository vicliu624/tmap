# TMRT route archive wire format

This is the technical format revision for a saved route, independent of the TMAP
map container. Files conventionally use `.tmroute`. All integer fields are
little-endian; coordinates use signed E7 degrees. No C++ struct layout is written.
The currently implemented TMRT wire revision is 1.

## File layout

1. 192-byte header.
2. `legs` records, 16 bytes each.
3. Padding to a 1,024-byte boundary.
4. `chunks` geometry pages, 1,024 bytes each.
5. A bottom-up bounding tree, 64 bytes per node, with the root last.

The final declared length must equal the actual file length. The writer requires
an exclusive empty file, never extends beyond its explicit file budget, and
writes the complete header only after all data and index output succeeds.

## Header

| Offset | Bytes | Value |
| ---: | ---: | --- |
| 0 | 4 | ASCII `TMRT` |
| 4 | 4 | Wire revision, 1 |
| 8 | 4 | Header bytes, 192 |
| 12 | 4 | Complete flag, 1 |
| 16 | 16 | Source map ID |
| 32 | 16 | Source map build |
| 48 | 16 | Source map series |
| 64 | 8 | Source map revision |
| 72 | 8 | Integer route cost, in units of 1/ascent-denominator centimeters |
| 80 | 8 | Cumulative geometry distance, centimeters |
| 88 | 8 | Unique geometry point count |
| 96 | 4 | Leg count |
| 100 | 4 | Geometry page count |
| 104 | 8 | Geometry-page start offset |
| 112 | 8 | Index start offset |
| 120 | 8 | Root node offset |
| 128 | 8 | Complete file length |
| 136 | 4 | CRC32C of all 192 bytes, this field zeroed |
| 140 | 4 | Travel mode: foot 1, bicycle 2 |
| 144 | 4 | Ascent-cost numerator |
| 148 | 4 | Ascent-cost denominator, nonzero |
| 152 | 40 | Reserved, zero |

An uncommitted file begins with a zeroed header. A header-only open establishes
layout and metadata validity. Subsequent reads validate visited records/pages.
The optional validation cursor scans all stored records, pages and nodes.

## Leg records

Each 16-byte row holds edge ID at offset 0, first centimeter offset at 4, last
centimeter offset at 8 and CRC32C at 12. The CRC covers the whole row with its CRC
field zeroed. `last_cm == UINT32_MAX` means the end of the directed edge.
The first offset must not exceed the last offset. Edge `UINT32_MAX` is invalid.
Rows use the original graph's IDs and require an exact source-map match before
being returned through the archive reader.

The temporary planner spool has a different contract: raw 12-byte rows, no
committed header, and potentially overwritten tentative candidates. It is never
opened as a route archive.

## Geometry page

| Offset | Bytes | Value |
| ---: | ---: | --- |
| 0 | 4 | ASCII `RCHK` |
| 4 | 4 | CRC32C of the entire 1,024-byte page, this field zeroed |
| 8 | 4 | Page ordinal |
| 12 | 4 | Stored point count, 1..40 |
| 16 | 16 | Bounding box: west, south, east, north, four signed E7 values |
| 32 | 8 | First point's cumulative distance |
| 40 | 8 | Last point's cumulative distance |
| 48 | 16 | Reserved, written as zero |
| 64 | Up to 960 | Up to 40 point records |

Each point is 24 bytes: latitude `i32`, longitude `i32`, cumulative centimeters
`u64`, leg ordinal `u32`, flags `u32`. Flag bit 0 means break before this point;
other bits are unsupported. The first distance is zero and later distances are
nondecreasing. With no legs, ordinal zero is used for geometry-only tracks.

All pages except the final one hold 40 records. Each page after the first repeats
the preceding page's final record. Logical point count excludes these overlaps.
For `P > 1`, page count is `ceil((P - 1) / 39)`; a one-point route uses one page.
The overlap provides a complete segment at every seam without duplicate emission.
Unused bytes in the final page are written as zero and covered by its CRC.

Bounding boxes use ordered longitude. A segment crossing the date line gives its
page a conservative full-longitude box. Per-segment candidate checks unwrap the
short longitude interval and support wrapped query bounds.

## Bounding tree

| Offset | Bytes | Value |
| ---: | ---: | --- |
| 0 | 16 | Ordered geographic bounding box |
| 16 | 8 | First cumulative distance |
| 24 | 8 | Last cumulative distance |
| 32 | 8 | Left child file offset, or zero for a leaf |
| 40 | 8 | Right child offset, or zero for a leaf/unary parent |
| 48 | 4 | Page ordinal for a leaf; `UINT32_MAX` for a parent |
| 52 | 8 | Reserved, written as zero |
| 60 | 4 | CRC32C of all 64 bytes, this field zeroed |

Leaves appear first in route order. Each higher level groups adjacent pairs of
the preceding level; an odd final child gets a unary parent. Levels are appended
until one root remains. Child offsets are aligned node locations earlier than
their parent. A parent's box and distance interval enclose its children.

For `N` pages, exact node count is `N + ceil(N/2) + ceil(N/4) + ... + 1`, stopping
at the first 1. Page count is limited to `2^30`, keeping the depth within the fixed
32-entry traversal stack. Storage-budget checks include the exact node count.
Queries traverse left before right, preserving route order even across loops.

CRC32C uses the Castagnoli polynomial. It is evaluated with a 16-entry, 64-byte
table in the archive module. Tests cross-check the header, leg and page checksums
against the existing 256-entry implementation and retain byte parity with the
previous verified archive writer.

## Continuation and ownership contract

The library performs no allocation. The caller provides `RouteArchiveWorkspace`
and independent query/validation/export cursors. One mutable workspace belongs
to one reader or one active writer pipeline on one owner thread. Independent
reader instances must not share it. A cursor belongs to the reader that began
it and must not outlive that reader. Query cursors can be interleaved on the
same reader; a page is reloaded
when another operation invalidates its cache. A cursor never holds a borrowed
pointer into the shared wire scratch across a yield.

Each work unit processes a bounded record, node or page. `Busy` and `More` from
an I/O adapter do not advance over unconsumed data. Closing/reopening a reader
increments its generation and invalidates previous cursors. Full validation is
optional during normal viewing and can be scheduled separately before publication.

The codec does not implement filesystem synchronization or atomic rename. See
[the persistence workflow](PERSISTENT_ROUTES.md) for application responsibilities
and [the evidence report](ROUTE_ARCHIVE_EVIDENCE.md) for tested resource bounds.
