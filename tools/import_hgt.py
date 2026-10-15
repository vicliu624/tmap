#!/usr/bin/env python3
"""Import standard 1201/3601-square HGT or HGT.gz into a cropped TMAP document.

HGT big-endian samples run north to south. Output is resampled onto a uniform
E7 grid with bilinear interpolation; integer-meter quantization is explicit.
NoData (-32768) is never filled across a gap. Dataset source/datum are required.
"""
import argparse
import gzip
import json
import math
from pathlib import Path
import re
import struct


def read_hgt(path, bounds, *, source, vertical_datum):
    path = Path(path)
    match = re.match(r"([NS])(\d{2})([EW])(\d{3})\.hgt(?:\.gz)?$", path.name, re.IGNORECASE)
    if not match: raise ValueError("HGT filename must encode southwest tile coordinate")
    south = int(match[2])*(-1 if match[1].upper() == "S" else 1)
    west = int(match[4])*(-1 if match[3].upper() == "W" else 1)
    raw = gzip.decompress(path.read_bytes()) if path.suffix.lower() == ".gz" else path.read_bytes()
    side = math.isqrt(len(raw)//2)
    if side not in (1201,3601) or side*side*2 != len(raw): raise ValueError("invalid HGT dimensions")
    left, bottom, right, top = bounds
    if not west <= left < right <= west+1 or not south <= bottom < top <= south+1:
        raise ValueError("crop must lie within a single HGT tile")
    samples = side-1
    dx = round(10_000_000/samples)
    origin_x, origin_y = round(left*1e7), round(bottom*1e7)
    columns = math.ceil((right*1e7-origin_x)/dx)+1
    rows = math.ceil((top*1e7-origin_y)/dx)+1
    if columns*rows > 20_000_000: raise ValueError("crop exceeds desktop importer sample budget")
    def at(x,y):
        if x < -1e-8 or y < -1e-8 or x > samples+1e-8 or y > samples+1e-8: return None
        x, y = min(samples,max(0,x)), min(samples,max(0,y))
        ix, iy = min(int(x),samples-1), min(int(y),samples-1)
        fx, fy = x-ix, y-iy
        weights = [(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy]
        value = 0
        for i,w in enumerate(weights):
            if w == 0: continue
            z = struct.unpack_from(">h",raw,((samples-(iy+i//2))*side+ix+i%2)*2)[0]
            if z == -32768: return None
            value += z*w
        return round(value)
    heights = [[at(((origin_x+x*dx)/1e7-west)*samples,((origin_y+y*dx)/1e7-south)*samples)
                for x in range(columns)] for y in range(rows)]
    return {"name": path.name+" cropped terrain", "contour_interval_m": 10,
            "dem": {"origin":[origin_x/1e7,origin_y/1e7], "step":[dx/1e7,dx/1e7],
                    "resolution_m":round(111320/samples), "source":source,
                    "vertical_datum":vertical_datum, "heights":heights}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path); parser.add_argument("output",type=Path)
    parser.add_argument("--bounds",nargs=4,type=float,required=True,metavar=("WEST","SOUTH","EAST","NORTH"))
    parser.add_argument("--source",required=True); parser.add_argument("--vertical-datum",type=int,required=True)
    args = parser.parse_args()
    document = read_hgt(args.input,args.bounds,source=args.source,vertical_datum=args.vertical_datum)
    args.output.write_text(json.dumps(document),encoding="utf-8")


if __name__ == "__main__": main()
