"""Import explicit GeoJSON administrative hierarchy fields for the host writer.

Polygon/MultiPolygon WGS84 data only. No silent projection, name guessing,
polygon repair or antimeridian splitting. Source digest/provenance is retained
in the producer document; series identity includes that source digest.
"""
import argparse
import hashlib
import json
from pathlib import Path


def convert(document, fields, where=None):
    if document.get("type") != "FeatureCollection": raise ValueError("GeoJSON FeatureCollection required")
    if document.get("crs"):
        name = document["crs"].get("properties",{}).get("name","")
        if name not in ("urn:ogc:def:crs:OGC:1.3:CRS84","EPSG:4326","urn:ogc:def:crs:EPSG::4326"):
            raise ValueError("only explicit WGS84 longitude/latitude data supported")
    result = []
    for feature in document["features"]:
        properties = feature.get("properties") or {}
        if where and str(properties.get(where[0])) != where[1]: continue
        path = []
        for field in fields:
            value = properties.get(field)
            if not isinstance(value,str) or not value.strip(): raise ValueError("missing hierarchy field: "+field)
            path.append(value.strip())
        geometry = feature.get("geometry") or {}
        kind = geometry.get("type")
        if kind == "Polygon": polygons = [geometry["coordinates"]]
        elif kind == "MultiPolygon": polygons = geometry["coordinates"]
        else: raise ValueError("only Polygon/MultiPolygon administrative features supported")
        if not polygons: raise ValueError("empty polygon")
        # Components remain separate records with the same full hierarchy.
        for rings in polygons:
            if not rings or any(any(len(p) != 2 for p in ring) for ring in rings):
                raise ValueError("two-dimensional rings required")
            result.append({"path":path,"rings":rings})
    if not result: raise ValueError("no selected administrative features")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path); parser.add_argument("output",type=Path)
    parser.add_argument("--fields",required=True,help="comma-separated hierarchy property fields, broad to narrow")
    parser.add_argument("--where",help="optional exact FIELD=VALUE filter")
    parser.add_argument("--source",required=True,help="source URI/version/attribution identity")
    args = parser.parse_args()
    where = args.where.split("=",1) if args.where else None
    if where and len(where) != 2: parser.error("--where must be FIELD=VALUE")
    raw = args.input.read_bytes(); digest = hashlib.sha256(raw).hexdigest()
    administration = convert(json.loads(raw),args.fields.split(","),where)
    result = {"name":"administration:"+str(where or args.fields),"series":"administration:"+args.source+":"+digest,
              "administrative_source":{"identity":args.source,"sha256":digest},"administration":administration}
    args.output.write_text(json.dumps(result,ensure_ascii=False)+"\n",encoding="utf-8")
    print(json.dumps({"features":len(administration),"rings":sum(len(f["rings"]) for f in administration),
                      "vertices":sum(len(r) for f in administration for r in f["rings"]),"sha256":digest}))


if __name__ == "__main__": main()
