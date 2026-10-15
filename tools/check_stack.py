"""Check GCC .su reports for bounded automatic storage in the portable core.

This is a host-ABI static check, not a measurement of an ESP task's call chain.
Legacy source reports are listed separately: its copied implementation is not
changed or exempted by adding exceptions to new code.
"""
import argparse
import json
from pathlib import Path
import re


def check(root, limit=256):
    entries, failed = [], []
    reports = list(Path(root).rglob("*.su"))
    if not reports:
        raise ValueError("no stack reports; build with TMAP_EMBEDDED_CHECKS=ON")
    for report in reports:
        legacy = report.name in ("reader.cpp.su", "search.cpp.su")
        for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = line.split("\t")
            if len(fields) != 3:
                continue
            name, size, kind = fields
            size = int(size)
            entry = {"symbol": name, "bytes": size, "kind": kind, "legacy": legacy}
            entries.append(entry)
            if not legacy and (size > limit or kind not in ("static", "dynamic,bounded")):
                failed.append(entry)
    core = [x for x in entries if not x["legacy"]]
    report = {"new_core_max_frame": max(x["bytes"] for x in core), "limit": limit,
              "legacy_max_frame": max((x["bytes"] for x in entries if x["legacy"]), default=0), "violations": failed}
    if failed:
        raise ValueError(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--limit", type=int, default=256)
    args = parser.parse_args()
    print(json.dumps(check(args.build, args.limit), indent=2))
