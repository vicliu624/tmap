# Center production with installed SDK tools

Center's actual CLI ran `navigation` using the SDK's installed
`share/tmap/tools` directory, with no source-tools path in the plan. Inputs are
the two selected original Liechtenstein OSM way extracts documented in
RAW_REGION_SEAMS.md (ways 6065793 and 1324063797). These are real source tags,
coordinates and stable references, not complete geographic extracts.

The output contains four nodes and six directed edges, occupies **12,288 B**,
and is **byte-for-byte identical** to the earlier Center artifact produced by
source-directory tools under the same topology/profile settings. The manifest
retains original source hashes and installed SDK script hashes.

Default and compact C++ probes each route both directions with integer FIFO,
linear Dijkstra, heap Dijkstra, ALT and queue-free label-scan Dijkstra:
**20/20 successful queries**. Probes inject Busy, verify each returned edge is
connected and foot-accessible, and verify the two-edge route reaches the
selected destination. This validates the installation/production/consumption
chain; it does not establish large-region capacity, MCU timing or physical
PSRAM behavior. Whole-country joint terrain/admin/contour evidence is separate
in REAL_OUTDOOR_JOINT.md.

The local Center plan is
`artifacts/navigation-source/installed-sdk-real-seam-plan-v1.json`; output is
`artifacts/navigation/installed-sdk-real-seam-v1`.
The versioned report is
[installed Center artifact evidence](installed-center-real-artifact-evidence.json).
Recheck with `tools/check_installed_center_artifact.py --help`.

Configure a host build with `TMAP_INSTALL_TOOLS=ON`, build and install it, then
set Center's ToolsDirectory to `<prefix>/share/tmap/tools`. Supply the Python
interpreter explicitly. Optional producer dependencies are host-side only;
this selected XML/graph production uses the standard library.
