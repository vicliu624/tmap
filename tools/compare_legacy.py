"""Run identical real-map probes against current Trail Mate and standalone core.

Exact output comparison checks lookup, pixel CRC, annotations, POI identity,
search results, index page reads and storage sizes. Wall time is reported only;
it is not a hardware benchmark or a strict pass/fail performance assertion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time


def run(executable, arguments):
    start = time.perf_counter()
    result = subprocess.run([str(executable), *arguments], capture_output=True, text=True, encoding="utf-8", check=True)
    return result.stdout, (time.perf_counter()-start)*1000


def compare(baseline, standalone, arguments, repeat=5):
    a_times, b_times = [], []
    for _ in range(repeat):
        a, ta = run(baseline, arguments)
        b, tb = run(standalone, arguments)
        if a != b: raise ValueError(f"baseline mismatch\n{a}\n{b}")
        a_times.append(ta); b_times.append(tb)
    return {"arguments": arguments, "output": a, "identical": True,
            "baseline_median_ms": statistics.median(a_times), "standalone_median_ms": statistics.median(b_times),
            "note": "Host process timing includes startup and file cache; does not establish ESP latency.", "runs": repeat}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path); parser.add_argument("standalone", type=Path)
    parser.add_argument("file"); parser.add_argument("zoom"); parser.add_argument("longitude"); parser.add_argument("latitude")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = compare(args.baseline, args.standalone, [args.file, args.zoom, args.longitude, args.latitude])
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output: args.output.write_text(text+"\n", encoding="utf-8")
    print(text)
