"""Verify real Center coordinate route totals against packed-profile arithmetic.

This oracle validates clipping/aggregation, not surveyed elevation accuracy.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from tmap_pack import build, graph_sections, distance


def totals(sections, legs):
    result = [0.,0.,0.]
    for leg in legs:
        edge = sections[1102][leg["edge"]*48:(leg["edge"]+1)*48]
        flags = struct.unpack_from("<H",edge,10)[0]
        if not flags & 2: raise ValueError("route oracle requires complete elevation")
        length = struct.unpack_from("<I",edge,12)[0]
        shape,count = struct.unpack_from("<QI",edge,24)
        points = [struct.unpack_from("<2i",sections[1103],shape+i*8) for i in range(count)]
        if flags & 1: points.reverse()
        lengths = [distance(a,b) for a,b in zip(points,points[1:])]
        end = count-2 if leg["last_segment"] == 0xffffffff else leg["last_segment"]
        first = sum(lengths[:leg["first_segment"]])+lengths[leg["first_segment"]]*leg["first_fraction"]
        last = sum(lengths[:end])+lengths[end]*leg["last_fraction"]
        scale = length/sum(lengths)
        first *= scale; last *= scale
        offset,samples,_ = struct.unpack_from("<QII",sections[1108],leg["edge"]*16)
        profile = [struct.unpack_from("<III",sections[1109],offset+i*12) for i in range(samples)]
        positions = [p[0] for p in profile]
        def prefix(at,channel):
            if at <= 0: return 0
            if at >= length: return profile[-1][channel]
            upper = bisect.bisect_left(positions,at)
            a,b = profile[upper-1],profile[upper]
            return a[channel]+(b[channel]-a[channel])*(at-a[0])/(b[0]-a[0])
        result[0] += (last-first)/100
        for channel in (1,2): result[channel] += (prefix(last,channel)-prefix(first,channel))/100
    return result


def benchmark(artifact, inspector):
    manifest = json.loads((artifact/"manifest.json").read_text())
    doc = json.loads((artifact/"graph.json").read_text())
    package = artifact/manifest["output"]["file"]
    expected = manifest["output"]["sha256"]
    if hashlib.sha256(package.read_bytes()).hexdigest() != expected or hashlib.sha256(build(doc)[0]).hexdigest() != expected:
        raise ValueError("Center package/document identity mismatch")
    sections = {kind:raw for kind,raw,_ in graph_sections(doc["graph"],doc["dem"])}
    paths = [e["points"] for e in doc["graph"]["edges"] if len(e.get("points",[])) >= 4]
    if len(paths) < 3: raise ValueError("insufficient shaped roads")
    a = paths[0][len(paths[0])//4]
    b = paths[2][len(paths[2])*3//4]
    cases = []
    for start,end in ((a,b),(b,a)):
        for penalty in (0,5):
            row = json.loads(subprocess.run([str(inspector),str(package),"measure-route-coordinates",
                *map(str,start),*map(str,end),str(penalty)],check=True,capture_output=True,text=True).stdout)
            oracle = totals(sections,row["legs"])
            if row["result"] != "ok" or row["statistics_result"] != "ok": raise ValueError("route/statistics failed")
            for name,value in zip(("length_m","ascent_m","descent_m"),oracle):
                if abs(row[name]-value) > 0.05: raise ValueError("clipped statistics disagree with oracle")
            if abs(row["cost_m"]-(row["length_m"]+penalty*row["ascent_m"])) > 0.05:
                raise ValueError("coordinate cost disagrees with clipped statistics")
            cases.append({"start":start,"destination":end,"measurement":row,"packed_profile_oracle":oracle})
    return {"package_sha256":expected,"inspector_sha256":hashlib.sha256(inspector.read_bytes()).hexdigest(),
            "cases":cases,"tolerance_m":0.05,
            "scope":"Actual Center package and C++ coordinate route/elevation aggregation; host payload and reads, not MCU peaks. Oracle shares packed samples but independently computes clipping and summation. No surveyed elevation accuracy claim."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact",type=Path); parser.add_argument("inspector",type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args = parser.parse_args()
    report = benchmark(args.artifact.resolve(),args.inspector.resolve())
    args.output.write_text(json.dumps(report,indent=2)+"\n")
    print("PASS four real clipped coordinate routes and independent packed-profile totals")
