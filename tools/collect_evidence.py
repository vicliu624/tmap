"""Collect reproducible local build evidence without claiming hardware validation.

Run the build first. Core-only archives can optionally be inspected with their
target nm/size tools; desktop tests are collected only with --ctest.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from check_stack import check


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--limit", type=int, default=256)
    parser.add_argument("--ctest", action="store_true")
    parser.add_argument("--nm", type=Path)
    parser.add_argument("--size", type=Path)
    args = parser.parse_args()
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8")
    configuration = {}
    for name in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_MINSIZEREL",
                 "TMAP_NATIVE_FILE", "TMAP_BUILD_EXAMPLES", "TMAP_BUILD_TESTS", "TMAP_EMBEDDED_CHECKS",
                 "TMAP_CACHE_PAGE_BYTES","TMAP_PACKAGE_CACHE_PAGES","TMAP_PACKAGE_SECTION_SLOTS"):
        match = re.search(r"^" + name + r":[^=]+=(.*)$", cache, re.M)
        configuration[name] = match[1] if match else None
    report = {"label": args.label, "date": args.date, "configuration": configuration,
              "stack": check(args.build, args.limit),
              "limitations": "Local compiler/test evidence; no MCU execution, linked firmware flash delta, task stack high-water or device latency measurement."}
    if args.ctest:
        output = run(["ctest", "--test-dir", str(args.build), "--output-on-failure"])
        output = output.replace(str(args.build.resolve()).replace("\\", "/"), "BUILD")
        report["ctest"] = {"command": "ctest --test-dir BUILD --output-on-failure", "output": output}
    archive = args.build / "libtmap.a"
    if args.nm:
        output = run([str(args.nm), "-u", str(archive)])
        symbols = sorted(set(re.findall(r"\bU\s+(\S+)", output)))
        forbidden = [s for s in symbols if re.search(r"(^_?malloc$|^_?calloc$|^_?realloc$|^_?free$|^_Zn|__cxa_|__gxx_personality|^_ZT[IVS])", s)]
        if forbidden:
            raise RuntimeError("Forbidden core dependencies: " + str(forbidden))
        report["undefined_symbols"] = symbols
        report["forbidden_dependencies"] = forbidden
        report["nm_tool"] = args.nm.name
    if args.size:
        output = run([str(args.size), str(archive)])
        objects = []
        for line in output.splitlines():
            match = re.match(r"\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+[0-9a-f]+\s+(\S+)", line)
            if match:
                objects.append({"object": match[4], "text": int(match[1]), "data": int(match[2]), "bss": int(match[3])})
        if not objects:
            raise RuntimeError("No object sizes parsed")
        report["archive_objects"] = objects
        report["size_tool"] = args.size.name
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS evidence", args.label, "->", args.output)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
