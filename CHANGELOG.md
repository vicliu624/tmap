# Changelog

## 0.2.0 — 2026-10-15

- Persistent route archives with checked leg records, fixed 1 KiB geometry pages,
  on-file spatial/distance trees, bounded queries and full integrity scans.
- Integer coordinate/prepared routes can emit directly to an idempotent spool
  sink. Existing array APIs retain their behavior; summary APIs publish only
  completed routes. Search arrays can be released before geometry export.
- Separate route-only reader linkage and a 64-byte CRC table; explicit external-
  first owners do not fall back to internal RAM after allocation failure.
- Center-generated London/Kunming acceptance routes measure 20.78/20.27 km.
  Retained evidence includes capacity failures, Busy byte parity, twelve zoom/
  position windows per successful case, external-owner peaks and array parity.
- Native unified v2 containers combine display/search/raster sections with
  outdoor sections, retaining existing pixel encodings and bounded reader
  directory storage. Older readers reject the required extension explicitly.
- Streaming host composition and section-file writers; multi-style display
  inputs share one metadata/search domain instead of nesting map files.
- DEM display-level policy and `TerrainReader::initializeForZoom`; analytic
  initialization retains the finest DEM. Level changes invalidate old cursors.
- Unified-container tests cover directories larger than one reader's slot
  budget, native tile parity, multiple display styles, POI search, terrain
  selection and cursor invalidation. No implicit core allocation added.

## 0.1.1 — 2026-10-15

Experimental public release following the initial 0.1.0 release candidate.

- Fix test counter deduction and formatting on 64-bit Linux hosts.
- Split the integer coordinate-routing prepared phase to keep Clang dispatcher
  stack frames within the existing host limit; retain the ESP stack gates.
- Clarify installation, memory evidence, test counts and pre-release boundaries.

## 0.1.0 — 2026-10-15

First independent experimental pre-release.

- Portable C++17 v1 raster/POI/search reader with preserved source provenance.
- Outdoor-v2 paged containers, integrity checks and bounded I/O continuations.
- DEM queries, streaming elevation profiles, ascent/descent and fixed-capacity
  plot reduction with explicit sample/storage limits and PSRAM-first ownership.
- Height-tagged vector contours with spatial/zoom filtering and bounded drawing.
- Hiking/cycling routing, coordinate endpoints, optional ALT heuristics, integer
  alternatives, runtime access/barrier/turn rules and compatible regional data.
- Administrative queries, source identity and conservative host production tools.
- Default/compact real-map evidence, installed-consumer/core-only gates and
  ESP32-S3/C3 compiler stack checks.

This release provides library and producer code. Application/firmware adapters,
physical board memory/power/storage acceptance, global terrain downloads and
guaranteed nationwide routing are outside its validated scope. See README and
the evidence reports for capacities, explicit failures and source attribution.
