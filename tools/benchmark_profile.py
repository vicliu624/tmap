"""Compare default/compact C++ profile owners against a real Center DEM section.

benchmark_profile.py PACKAGE DEFAULT_PROBE COMPACT_PROBE CENTER_PROFILE_JSON REPORT
The reference coordinates match NavigationMapSmoke's recorded inner section.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    package, default, compact, reference, report = map(Path, sys.argv[1:])
    expected = json.loads(reference.read_text(encoding="utf-8"))
    cases = []
    for profile, probe in (("default", default), ("compact", compact)):
        for columns in (16, 96, 240):
            for name, coordinates, samples, status in (
                ("forward", [471450000, 95160000, 471450000, 95290000], 8192, 0),
                ("reverse", [471450000, 95290000, 471450000, 95160000], 8192, 0),
                ("partial", [471450000, 95000000, 471450000, 95290000], 8192, 10),
                ("sample_limit", [471450000, 95160000, 471450000, 95290000], 2, 9),
            ):
                baseline = None
                for busy in (0, 2):
                    result = subprocess.run([str(probe.resolve()), str(package.resolve()), *map(str, coordinates),
                                             "30", str(columns), str(samples), str(busy)],
                                            capture_output=True, text=True, check=True)
                    row = json.loads(result.stdout)
                    assert row["result"] == status and row["live_after_reset"] == 0, row
                    keys = ("result", "distance_m", "ascent_m", "descent_m", "minimum_m", "maximum_m", "samples", "gaps", "valid_distance_m")
                    semantic = {key: row[key] for key in keys}
                    if baseline is None:
                        baseline = semantic
                    else:
                        assert baseline == semantic, (baseline, semantic)
                    if name == "forward":
                        for cpp, center in (("distance_m", "DistanceMeters"), ("ascent_m", "AscentMeters"),
                                            ("descent_m", "DescentMeters"), ("minimum_m", "MinimumMeters"),
                                            ("maximum_m", "MaximumMeters"), ("valid_distance_m", "ValidDistanceMeters")):
                            assert abs(row[cpp] - expected[center]) < 1e-6, (cpp, row[cpp], expected[center])
                    cases.append({"profile": profile, "case": name, "busy_every": busy, **row})
    report.write_text(json.dumps({
        "scope": "Real GUI-produced DEM; host typed payload plus provider requests, not physical MCU peaks. External region simulated by malloc; no board PSRAM claim.",
        "sha256": {"package": digest(package), "default_probe": digest(default), "compact_probe": digest(compact), "center_reference": digest(reference)},
        "reference_coordinates_e7": [471450000, 95160000, 471450000, 95290000],
        "cases": cases, "passed": len(cases)
    }, indent=2) + "\n", encoding="utf-8")
    print(f"{len(cases)} cases passed; report: {report}")


if __name__ == "__main__":
    main()
