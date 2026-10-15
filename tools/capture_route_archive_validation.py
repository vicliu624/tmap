"""Run and retain current route-archive host, core-only, cross-compiler and linkage gates."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

from check_stack import check


def execute(command):
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def cache(directory):
    result = {}
    for line in (directory / "CMakeCache.txt").read_text().splitlines():
        if not line or line.startswith(("/", "#")) or "=" not in line or ":" not in line:
            continue
        key, value = line.split("=", 1)
        result[key.split(":", 1)[0]] = value
    return result


def run(args):
    for directory in (args.host, args.core, args.esp):
        execute(["cmake", "--build", directory, "-j", "6"])
    xml = args.host.resolve() / "route-archive-validation.xml"
    execute(["ctest", "--test-dir", args.host, "--output-on-failure", "-j", "6", "--output-junit", xml])
    tests = ET.parse(xml).getroot()
    entries = tests.findall(".//testcase")
    if len(entries) < 40 or any(row.find("failure") is not None or row.find("skipped") is not None for row in entries):
        raise ValueError("Incomplete host verification")
    host, core, esp = (cache(directory) for directory in (args.host, args.core, args.esp))
    for key in ("TMAP_BUILD_EXAMPLES", "TMAP_BUILD_TESTS", "TMAP_NATIVE_FILE"):
        if core[key] != "OFF" or esp[key] != "OFF":
            raise ValueError("Core/cross build included desktop features")
    suffix = ".exe" if sys.platform == "win32" else ""
    execute([sys.executable, "tests/test_route_archive_link.py", host["CMAKE_NM"], args.host / ("tmap_route_archive_read_probe" + suffix)])
    sources = sorted(Path("include/tmap").glob("*route*.h")) + sorted(Path("src").glob("*route*.cpp"))
    sources += [Path("src/route_archive_wire.h"), Path("examples/route_archive_probe.cpp"), Path("examples/route_archive_read_probe.cpp"), Path("tests/test_route_archive.cpp")]
    sources += [Path("tests/test_route_archive_link.py"), Path("tools/route_archive_evidence.py"), Path("tools/capture_route_archive_validation.py")]
    report = {
        "host_compiler": execute([host["CMAKE_CXX_COMPILER"], "--version"]).splitlines()[0],
        "esp_compiler": execute([esp["CMAKE_CXX_COMPILER"], "--version"]).splitlines()[0],
        "host_build_type": host["CMAKE_BUILD_TYPE"],
        "host_cache_profile": {key: host[key] for key in ("TMAP_CACHE_PAGE_BYTES", "TMAP_PACKAGE_CACHE_PAGES", "TMAP_PACKAGE_SECTION_SLOTS")},
        "host_ctest": {"passed": len(entries), "failed": 0, "tests": [row.attrib["name"] for row in entries]},
        "host_stack": check(args.host, 256),
        "esp_stack": check(args.esp, 240),
        "core_only_build": "passed",
        "esp_core_build": "passed",
        "core_and_esp_desktop_features": "disabled",
        "route_only_linkage": "passed: no raster, package, graph, search, writer, geometry export or projection implementation",
        "source_hash_normalization": "UTF-8 text with LF line endings",
        "source_sha256": {path.as_posix(): hashlib.sha256(path.read_text(encoding="utf-8").encode()).hexdigest() for path in sources},
        "physical_board_ram_psram_stack_latency_energy": "not measured",
    }
    if args.baseline:
        baseline = args.host.resolve() / ("route-layout-baseline-" + hashlib.sha256(args.baseline.encode()).hexdigest()[:12])
        baseline.mkdir(exist_ok=True)
        archive = subprocess.run(["git", "archive", "--format=tar", args.baseline, "include"], capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
            stream.extractall(baseline, filter="data")
        source = baseline / "layout.cpp"
        source.write_text('#include "tmap/integer_coordinate_route.h"\n#include <cstdio>\nint main() { std::printf("%zu %zu\\n", sizeof(tmap::IntegerPreparedRouteCursor), sizeof(tmap::IntegerCoordinateRouteCursor)); }\n')
        sizes = {}
        for name, include in (("baseline", baseline / "include"), ("current", Path("include").resolve())):
            output = baseline / (name + suffix)
            execute([host["CMAKE_CXX_COMPILER"], "-std=c++17", "-I" + str(include), source, "-o", output])
            sizes[name] = dict(zip(("prepared", "coordinate"), map(int, execute([output]).split())))
        if any(sizes["current"][key] > sizes["baseline"][key] for key in sizes["baseline"]):
            raise ValueError("Existing array cursor storage increased")
        report["array_cursor_layout_comparison"] = {"baseline_ref": args.baseline, **sizes, "no_increase": True}
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"tests_passed": len(entries), "core": "passed", "esp": "passed", "host_stack": report["host_stack"]["new_core_max_frame"], "esp_stack": report["esp_stack"]["new_core_max_frame"]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--esp", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--baseline", help="Published source ref for a compiled array-cursor size comparison")
    run(parser.parse_args())
