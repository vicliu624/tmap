#!/usr/bin/env python3
"""Convert WGS84 ESRI ASCII Grid to a TMAP writer document.

Coordinates MUST be longitude/latitude degrees. Projected grids must first be
warped to EPSG:4326 by a GIS tool. This utility never guesses coordinate units.
"""
import argparse
import json
import math
from pathlib import Path


def read_ascii(path, *, resolution_m, source, vertical_datum, geographic=False):
    if not geographic:
        raise ValueError("explicit --wgs84 confirmation is required")
    with Path(path).open(encoding="utf-8") as stream:
        header = {}
        line = stream.readline()
        while line:
            tokens = line.split()
            if not tokens:
                line = stream.readline(); continue
            if tokens[0].lower() not in ("ncols", "nrows", "xllcorner", "yllcorner", "xllcenter", "yllcenter", "cellsize", "nodata_value"):
                break
            if len(tokens) != 2 or tokens[0].lower() in header:
                raise ValueError("invalid or duplicate ASCII grid header")
            header[tokens[0].lower()] = float(tokens[1])
            line = stream.readline()
        nx, ny = int(header["ncols"]), int(header["nrows"])
        if header["ncols"] != nx or header["nrows"] != ny or nx < 2 or ny < 2:
            raise ValueError("invalid raster size")
        step = header["cellsize"]
        if not math.isfinite(step) or step <= 0:
            raise ValueError("invalid cellsize")
        west = header.get("xllcenter", header.get("xllcorner", float("nan"))+step/2)
        south = header.get("yllcenter", header.get("yllcorner", float("nan"))+step/2)
        nodata = header.get("nodata_value", -9999)
        values = [float(x) for x in (line+stream.read()).split()]
        if len(values) != nx*ny:
            raise ValueError("ASCII grid sample count mismatch")
        rows = [[None if z == nodata else z for z in values[y*nx:(y+1)*nx]] for y in range(ny)]
        rows.reverse()
    return {"name": Path(path).stem, "contour_interval_m": 10,
            "dem": {"origin": [west, south], "step": [step, step], "heights": rows,
                    "resolution_m": resolution_m, "source": source, "vertical_datum": vertical_datum}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path); parser.add_argument("output", type=Path)
    parser.add_argument("--wgs84", action="store_true")
    parser.add_argument("--resolution-m", required=True, type=int)
    parser.add_argument("--source", required=True)
    parser.add_argument("--vertical-datum", required=True, type=int, help="0 unknown, 1 EGM96, 2 EGM2008, 3 ellipsoidal")
    args = parser.parse_args()
    doc = read_ascii(args.input, resolution_m=args.resolution_m, source=args.source,
                     vertical_datum=args.vertical_datum, geographic=args.wgs84)
    args.output.write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__": main()
