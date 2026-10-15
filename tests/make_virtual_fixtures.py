"""Border-overlap graph fixture: restriction operands exist in source regions."""
import json
import hashlib
from tmap_pack import build
from virtual_regions import prepare,binary_manifest


def generate(directory,page_bytes):
    nodes = [{"id":i+1,"point":[i*.001,0]} for i in range(4)]
    edges = [{"id":chr(97+i),"from":i+1,"to":i+2,"name":"trail"} for i in range(3)]
    a = {"name":"region-a","page_bytes":page_bytes,"graph":{"topology_epoch":"virtual-test-v1",
         "nodes":nodes[:3],"edges":edges[:2],"restrictions":[{"from":"a","to":"b","access":2,"kind":"no"}]}}
    b = {"name":"region-b","page_bytes":page_bytes,"graph":{"topology_epoch":"virtual-test-v1",
         "nodes":nodes[1:],"edges":edges[1:]}}
    a['dem'] = {'origin':[0,0],'step':[.001,.001],'resolution_m':100,
                'heights':[[0,10,20],[20,30,40]],'source':'region-a-dem'}
    b['dem'] = {'origin':[.001,0],'step':[.0005,.0005],'resolution_m':100,
                'heights':[[10,15,20,25,30],[20,25,30,35,40],[30,35,40,45,50]],'source':'region-b-dem'}
    a['administration'] = [{'path':['country','west'],'rings':[[[0,0],[.001,0],[.001,.001],[0,.001],[0,0]]]}]
    b['administration'] = [{'path':['country','east'],'rings':[[[.001,0],[.003,0],[.003,.001],[.001,.001],[.001,0]]]}]
    packages = [build(d)[0] for d in (a,b)]
    for suffix,raw in zip(("a","b"),packages): (directory/f"virtual-{suffix}.tmap").write_bytes(raw)
    target = {'origin':[0,0],'step':[.0005,.0005],'columns':7,'rows':3,'resolution_m':100}
    patch,manifest,merged = prepare([a,b],packages,page_bytes,dem_resampling=target)
    (directory/"virtual-patch.bin").write_bytes(patch)
    (directory/"virtual-reference.tmap").write_bytes(merged)
    (directory/"virtual-reference.sha256").write_text(manifest["virtual_sha256"]+"\n")
    binary = binary_manifest(manifest)
    (directory/"virtual-map.bin").write_bytes(binary)
    (directory/"virtual-map.sha256").write_text(hashlib.sha256(binary).hexdigest()+"\n")
    (directory/"virtual-manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    (directory/"virtual-extents.txt").write_text("".join(f'{e["source"]} {e["virtual_offset"]} {e["source_offset"]} {e["bytes"]}\n' for e in manifest["extents"]))
