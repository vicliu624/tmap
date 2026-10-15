"""Reproduce persisted-route evidence against Center's SD delivery manifest.

The probe checks array/sink leg parity and complete geometry parity, releases
search storage before export, closes the map before archive reopen, validates
every persisted record/page/node and measures cold viewport/distance reads.
This runner hashes the real input maps and retains capacity failures.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import uuid


CASES = (
    ("london", 277222, 343580, 2048, 0, "ok"),
    ("london", 277222, 343580, 2048, 7, "ok"),
    ("kunming", 731, 438, 2048, 0, "ok"),
    ("kunming", 731, 438, 2048, 7, "ok"),
    ("kunming", 731, 4439, 8192, 0, "ok"),
    ("kunming", 731, 4439, 8192, 7, "ok"),
    ("london", 277222, 343580, 256, 0, "budget_exceeded"),
    ("london", (515009000, -1245000), (514036000, -3377000), 65536, 0, "ok"),
    ("london", (515009000, -1245000), (514036000, -3377000), 65536, 7, "ok"),
    ("kunming", 15664, 44486, 49152, 0, "ok"),
    ("kunming", 15664, 44486, 49152, 7, "ok"),
    ("london", (515009000, -1245000), (514036000, -3377000), 32768, 0, "budget_exceeded"),
    ("london", (515009000, -1245000), (514036000, -3377000), 49152, 0, "budget_exceeded"),
    ("kunming", 15664, 44486, 32768, 0, "budget_exceeded"),
)
FILES = {
    "london": "maps/tmap/osm/london-osm-z13-17.tmap",
    "kunming": "maps/tmap/osm/kunming-osm-z13-16.tmap",
}


def run(args):
    previous = json.loads(args.report.read_text()) if args.check_previous and args.report.exists() else None
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    entries = {item["path"]: item for item in manifest["files"]}
    report = {
        "schema": 1,
        "producer": "TrailMateCenter Core workflows and TmapTool",
        "host": platform.platform(),
        "measurement": "Typed library payload and successful logical I/O bytes; not physical MCU peaks or SD timing",
        "viewport_cache": "Cold before each query; header read reported separately",
        "files": [], "cases": [],
    }
    output = args.output_dir / ("run-" + uuid.uuid4().hex)
    output.mkdir(parents=True)
    for region, relative in FILES.items():
        entry = entries[relative]
        source = args.sd_root / relative
        digest = hashlib.sha256()
        print(f"Hashing {region}: {source.stat().st_size} bytes", flush=True)
        with source.open("rb") as stream:
            while block := stream.read(4 * 1024 * 1024):
                digest.update(block)
        if digest.hexdigest() != entry["sha256"] or source.stat().st_size != entry["bytes"]:
            raise ValueError(f"Input differs from Center delivery manifest: {region}")
        report["files"].append({"region": region, "path": relative, "bytes": entry["bytes"], "sha256": digest.hexdigest()})
    for number, (region, start, end, capacity, busy, expected) in enumerate(CASES):
        print(f"Probe {number}: {region} {start}->{end}, capacity={capacity}, busy={busy}", flush=True)
        waypoints = (["--coordinates", *(str(v) for v in (*start, *end))]
                     if isinstance(start, tuple) else [str(start), str(end)])
        command = [str(args.probe.resolve()), str((args.sd_root / FILES[region]).resolve()),
                   str((output / f"{number}-{region}.tmroute").resolve()), *waypoints, str(capacity), str(busy)]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode not in (0, 3):
            raise RuntimeError(result.stderr + result.stdout)
        row = json.loads(result.stdout)
        if (expected == "ok") != (result.returncode == 0):
            raise ValueError(f"Unexpected route outcome: {row}")
        if expected != "ok" and row.get("result") != expected:
            raise ValueError(f"Unexpected capacity outcome: {row}")
        row.update(region=region, expected=expected)
        row["request"] = waypoints + [str(capacity), str(busy)]
        row["role"] = "20-km acceptance" if number >= 7 else "short-route regression"
        if result.returncode == 0:
            archive = output / f"{number}-{region}.tmroute"
            row["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
            if number >= 7 and not 1800000 <= row["length_cm"] <= 2200000:
                raise ValueError(f"Acceptance route is not approximately 20 km: {row['length_cm']}")
            if len(row["windows"]) != 12:
                raise ValueError("Missing zoom/position window probes")
            if args.read_probe:
                for mode, extra in (("reader_owner", []), ("reader_external_refusal", ["--refuse-external"])):
                    owner = subprocess.run([str(args.read_probe.resolve()), str(archive.resolve()), *extra],
                                           capture_output=True, text=True, check=True)
                    row[mode] = json.loads(owner.stdout)
                if row["reader_owner"]["provider_peak_bytes"] != row["read_library_payload_bytes"]:
                    raise ValueError("Measured owner peak differs from the retained library payload")
        report["cases"].append(row)
    for left, right in ((0, 1), (2, 3), (4, 5), (7, 8), (9, 10)):
        a, b = report["cases"][left], report["cases"][right]
        if a["archive_sha256"] != b["archive_sha256"]:
            raise ValueError("Busy changed persisted route bytes")
    if previous:
        before = {tuple(row["request"]): row["archive_sha256"] for row in previous["cases"] if "archive_sha256" in row}
        for row in report["cases"]:
            if "archive_sha256" in row and tuple(row["request"]) in before:
                if row["archive_sha256"] != before[tuple(row["request"])]:
                    raise ValueError("Persisted bytes changed from the previous verified implementation")
        report["previous_archive_byte_parity"] = True
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(CASES)} cases; report: {args.report}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--read-probe", type=Path)
    parser.add_argument("--check-previous", action="store_true")
    parser.add_argument("--sd-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    run(parser.parse_args())
