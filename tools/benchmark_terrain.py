"""Record cold-view resource checks on an externally sourced mountain package.

No device timing claims. Cross-language elevation agreement checks the library
against the resampled input; it does not independently validate ground truth.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from tmap_pack import elevation_at, point


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inspector",type=Path); parser.add_argument("document",type=Path); parser.add_argument("package",type=Path)
    parser.add_argument("--source-file",type=Path); parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--longitude",type=float,required=True); parser.add_argument("--latitude",type=float,required=True)
    args = parser.parse_args(); document = json.loads(args.document.read_text(encoding="utf-8"))
    report = {"source":document["dem"]["source"],"date":"2026-10-14",
              "attribution":"Mapzen Terrain Tiles; https://github.com/tilezen/joerd/blob/master/docs/attribution.md",
              "vertical_datum":document["dem"]["vertical_datum"],"package_bytes":args.package.stat().st_size,
              "columns":len(document["dem"]["heights"][0]),"rows":len(document["dem"]["heights"]),"views":[]}
    if args.source_file: report["source_sha256"] = hashlib.sha256(args.source_file.read_bytes()).hexdigest()
    base = [str(args.inspector),str(args.package)]
    for zoom in (12,14,16,18):
        result = subprocess.run(base+["measure-contours",str(args.longitude),str(args.latitude),str(zoom)],capture_output=True,text=True,check=True)
        view = json.loads(result.stdout); view["zoom"] = zoom
        if view["segments"] > 512 or view["operations"] > 100000: raise ValueError("resource budget violated")
        report["views"].append(view)
    output = subprocess.run(base+["elevation",str(args.longitude),str(args.latitude)],capture_output=True,text=True,check=True).stdout
    elevation = float(re.search(r"elevation=([0-9.\-]+)",output)[1])
    reference = elevation_at(document["dem"],point([args.longitude,args.latitude]))
    if reference is None or abs(reference-elevation) > 0.011: raise ValueError("elevation differs from source document")
    report["elevation_check"] = {"longitude":args.longitude,"latitude":args.latitude,"reader_m":elevation,"input_m":reference}
    report["limitations"] = "Desktop ABI; no ESP latency/stack-watermark claim. Source datum unknown; source terrain accuracy not independently verified."
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__ == "__main__": main()
