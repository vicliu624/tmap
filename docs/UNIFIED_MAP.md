# Native unified maps and elevation levels

Current source supports one TMAP 2.0 container holding raster styles, POIs,
names/search, optional numeric DEM, height-tagged contour vectors and routing.
The published 0.1.1 SDK predates this required extension. Its readers reject
these files; applications must update the SDK before accepting them.

## Native directory extension

The 256-byte v2 header retains major 2 / minor 0 and CRC32C fields. Required
container flags at byte 24 are `8`, physical page size is 4096, capability bit
`16` at byte 140 indicates native display sections, and the legacy display
capabilities are stored at byte 176. Bytes 168/172 contain display schema 1 and
normalization profile 2. Outdoor-only v2 flag 0 and original v1 remain readable.

The common 64-byte directory admits at most 128 rows. Display types below
1000 use their existing native encoding, IDs and owners; outdoor types 1000+
retain TPG2 pages. Numeric IDs can overlap between the two domains. Resolve
display references within the display domain. The readers stream directory rows
and retain only their own domain's entries. A large combined directory therefore
does not require increasing every embedded reader's section-slot array.

`tools/compose_map.py --raster display.tmap --outdoor terrain.tmap --output map.tmap`
streams native sections into the unified container. It does not decode or
re-encode pixels. One input owns display metadata/search/font references;
additional display inputs can contribute distinct raster styles. Font files
remain external, content-addressed dependencies. The SDK CLI publishes by an
exclusive hard link on the staging filesystem; use a supporting filesystem.
Center stages on the host, then copies and atomically renames into the SD layout.

## Numeric DEM levels

Optional sections 1020 (policy), 1021 (coarse block index) and 1022 (coarse
sample data) supplement the original finest DEM sections 1000–1002. The policy
has 30 records, one per display zoom 0–29. Each 96-byte record contains:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u64 | Index base in 32-byte records |
| 8 | u64 | Data base in bytes |
| 16 | 64 bytes | Terrain metadata, same schema as section 1000 |
| 80 | u32 | 0 selects coarse sections; 1 selects original finest sections |
| 84, 88 | u32, u64 | Reserved zero |

`TerrainReader::initialize()` selects the finest DEM for analysis. Explicit
`initializeForZoom(zoom)` selects a stored display level. Coarse levels decimate
source samples with their true coordinate spacing and preserve NoData. They do
not add precision. Switching level resets traversal and invalidates outstanding
terrain cursors. A profile should retain one analytic reader/level throughout.
Resolution metadata reports source/grid limitations independently of display
zoom. Polar/out-of-source coverage remains NoData.

## Resource evidence and limits

The host fixture suite passes 38 CTest cases, including installed-consumer,
configuration and compiler stack gates. In the measured default host ABI,
`Workspace` remains 14,024 bytes, `PackageWorkspace` remains 9,504 bytes and
`TerrainReader` is 152 bytes. These are separate typed objects, not a claim that
the whole application needs only one of them. Caller buffers, filesystem state,
route/plot storage and task stacks are additional. Reuse workspaces only under
the ownership rules of their APIs; do not share mutable storage concurrently.

ESP32-S3 compiler reports for this source give 144-byte frames for unified
display open and zoom-specific terrain initialization, below the 240-byte gate.
Host frames are 176 and 208 bytes respectively. Core-only and ESP32-S3 builds
pass. These are compiler/host results, not measured board RAM/PSRAM peaks,
watermarks, SD latency or power. The core introduces no implicit allocation;
caller-owned workspaces can be placed in PSRAM through the explicit owners.

Fixtures verify that combined directories exceeding 64 rows can be opened
without expanding retained reader slots, native pixel payloads remain identical,
three actual Center-generated raster styles and POI search survive composition,
and stale DEM cursors are canceled after level changes. Production map resource
results must be measured on the final files separately; old outdoor-only reports
do not establish acceptance for every new region or device.
