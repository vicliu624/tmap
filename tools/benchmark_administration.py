"""Record actual cold-query I/O on an external administrative data package."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inspector",type=Path); parser.add_argument("document",type=Path); parser.add_argument("package",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args = parser.parse_args()
    document = json.loads(args.document.read_text(encoding="utf-8"))
    report = {"source":document["administrative_source"],"package_bytes":args.package.stat().st_size,
              "features":len(document["administration"]),"queries":[],
              "limits":"Cold desktop file queries; no MCU timing/power/RAM peak proof. Coarse Natural Earth source is not a legal boundary or detailed district dataset."}
    for lon,lat,expected in ((102.7,25.04,"China"),(116.4,39.9,"China"),(-0.12,51.5,"")):
        result = subprocess.run([str(args.inspector),str(args.package),"region",str(lon),str(lat)],capture_output=True,text=True)
        if result.returncode != (0 if expected else 1): raise ValueError(result.stdout+result.stderr)
        match = re.search(r"region=(.*?) depth=",result.stdout)
        if not match or match[1] != expected: raise ValueError(result.stdout)
        report["queries"].append({"longitude":lon,"latitude":lat,"path":match[1],"output":result.stdout.strip()})
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__ == "__main__": main()
