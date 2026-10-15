"""Optional host evidence: compare every snapshot entity with pyosmium.

pyosmium is a verification dependency only, never a library/import requirement.
Run with its installation directory on PYTHONPATH. Input data is not bundled.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import osmium
from osm_pbf import iter_pbf


def verify(path):
    remaining = {}
    counts = {"node":0,"way":0,"relation":0}
    for e in iter_pbf(path):
        identity = (e.tag,int(e.attrib["id"]))
        value = {"tags":sorted((t.attrib["k"],t.attrib["v"]) for t in e.findall("tag"))}
        if e.tag == "node": value["point_e7"] = [round(float(e.attrib[k])*1e7) for k in ("lon","lat")]
        elif e.tag == "way": value["refs"] = [int(n.attrib["ref"]) for n in e.findall("nd")]
        else: value["members"] = [(m.attrib["type"],int(m.attrib["ref"]),m.attrib["role"]) for m in e.findall("member")]
        if identity in remaining: raise ValueError("duplicate snapshot entity")
        remaining[identity] = value
        counts[e.tag] += 1
    class Oracle(osmium.SimpleHandler):
        def check(self,kind,entity,value):
            value["tags"] = sorted((t.k,t.v) for t in entity.tags)
            expected = remaining.pop((kind,entity.id),None)
            if expected != value: raise ValueError("oracle mismatch: "+str((kind,entity.id,expected,value)))
        def node(self,n): self.check("node",n,{"point_e7":[round(n.location.lon*1e7),round(n.location.lat*1e7)]})
        def way(self,w): self.check("way",w,{"refs":[n.ref for n in w.nodes]})
        def relation(self,r): self.check("relation",r,{"members":[({"n":"node","w":"way","r":"relation"}[m.type],m.ref,m.role) for m in r.members]})
    Oracle().apply_file(str(path))
    if remaining: raise ValueError("entities missing in oracle")
    return {"file_bytes":path.stat().st_size,"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "oracle":"pyosmium "+importlib.metadata.version("osmium"),"counts":counts,
            "result":"all IDs, E7 coordinates, tags, ordered way references and relation members match",
            "scope":"host PBF decoding; not MCU resource measurement or routing-rule coverage"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path); parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--source",required=True)
    args = parser.parse_args()
    report = verify(args.input); report["source"] = args.source
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,ensure_ascii=False))
