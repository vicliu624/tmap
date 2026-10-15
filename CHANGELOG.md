# Changelog

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
