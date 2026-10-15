import copy
import json
from pathlib import Path
import random
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from tmap_pack import write, build, point, distance, crc32c
from merge_regions import merge_graphs
import tmap_pack


def reference(graph,start,destination,mode,penalty):
    nodes = {n["id"]:i for i,n in enumerate(graph["nodes"])}
    edges = sorted(graph["edges"],key=lambda e:(nodes[e["from"]],str(e["id"])))
    ids = {e["id"]:i for i,e in enumerate(edges)}
    weights = [round(distance(point(graph["nodes"][nodes[e["from"]]]["point"]),point(graph["nodes"][nodes[e["to"]]]["point"]))*100)/100+e.get("ascent_m",0)*penalty for e in edges]
    def allowed(a,b):
        for rule in graph.get("restrictions",[]):
            if a == ids[rule["from"]] and rule.get("access",3)&mode:
                if rule.get("kind","no") == "no" and b == ids[rule["to"]]: return False
                if rule.get("kind","no") == "only" and b != ids[rule["to"]]: return False
        return True
    # Bellman-Ford on arrival-edge states, independent of the library's heap.
    costs = [float("inf")]*len(edges)
    for _ in range(len(edges)+1):
        changed = False
        for b,e in enumerate(edges):
            if not e.get("access",3)&mode: continue
            best = 0 if nodes[e["from"]] == start else float("inf")
            for a,p in enumerate(edges):
                if p["to"] == e["from"] and allowed(a,b): best = min(best,costs[a])
            candidate = best+weights[b]
            if candidate < costs[b]: costs[b] = candidate; changed = True
        if not changed: break
    return min([0 if start == destination else float("inf")]+[costs[i] for i,e in enumerate(edges) if nodes[e["to"]] == destination])


def mutate(raw,section,offset,value,format):
    raw = bytearray(raw)
    count = struct.unpack_from("<I",raw,48)[0]
    for i in range(count):
        page_bytes = struct.unpack_from("<I",raw,20)[0]
        payload = page_bytes-32
        row = struct.unpack_from("<Q",raw,40)[0]+i*64
        if struct.unpack_from("<I",raw,row)[0] != section: continue
        base = struct.unpack_from("<Q",raw,row+16)[0]
        page = base+(offset//payload)*page_bytes
        struct.pack_into(format,raw,page+32+offset%payload,value)
        struct.pack_into("<I",raw,page+16,0)
        struct.pack_into("<I",raw,page+16,crc32c(raw[page:page+page_bytes]))
        return bytes(raw)
    raise ValueError("section missing")


def main(directory):
    directory = Path(directory); directory.mkdir(parents=True,exist_ok=True)
    n = 2048
    chain = {"topology_epoch":"stress-v1","nodes":[{"id":i+1,"point":[i*0.0001,0]} for i in range(n)],
             "edges":[{"id":f"e{i:04}","from":i+1,"to":i+2} for i in range(n-1)],
             "restrictions":[{"from":"e0083","to":"e0084","access":2}]}
    raw,report = build({"graph":chain})
    (directory/"large.tmap").write_bytes(raw)
    (directory/"large-size.json").write_text(json.dumps(report,indent=2))
    (directory/"bad-adjacency.tmap").write_bytes(mutate(raw,1101,16,1,"<I"))
    (directory/"bad-points.tmap").write_bytes(mutate(raw,1102,32,2**32-1,"<I"))
    truncated = raw[:-1]; (directory/"truncated.tmap").write_bytes(truncated)
    a = {"topology_epoch":"regional-v1","nodes":[{"id":1,"point":[0,0]},{"id":2,"point":[0.001,0]}],"edges":[{"id":"a","from":1,"to":2}]}
    b = {"topology_epoch":"regional-v1","nodes":[{"id":2,"point":[0.001,0]},{"id":3,"point":[0.002,0]}],"edges":[{"id":"b","from":2,"to":3}]}
    write({"graph":a},directory/"region-a.tmap"); write({"graph":b},directory/"region-b.tmap")
    write({"graph":merge_graphs([a,b])},directory/"regions-merged.tmap")
    b["topology_epoch"] = "regional-v2"; write({"graph":b},directory/"region-incompatible.tmap")
    expected = []
    rng = random.Random(17014)
    for trial in range(12):
        nodes = [{"id":i+1,"point":[i%4*0.001,i//4*0.001]} for i in range(12)]
        pairs = {(i,i+1) for i in range(11)}
        pairs.update((rng.randrange(12),rng.randrange(12)) for _ in range(25))
        pairs = sorted((a,b) for a,b in pairs if a != b)
        edges = [{"id":f"r{i:03}","from":a+1,"to":b+1,"access":rng.choice([1,2,3]),"ascent_m":rng.randrange(8),"descent_m":0} for i,(a,b) in enumerate(pairs)]
        restrictions = []
        for incoming in edges:
            outgoing = [e for e in edges if e["from"] == incoming["to"]]
            if outgoing and rng.random() < .3:
                restrictions.append({"from":incoming["id"],"to":rng.choice(outgoing)["id"],"access":rng.choice([1,2,3]),"kind":rng.choice(["no","only"])})
        graph = {"nodes":nodes,"edges":edges,"restrictions":restrictions}
        name = f"random-{trial}.tmap"; write({"graph":graph},directory/name)
        graph["landmarks"] = 4
        write({"graph":graph},directory/f"alt-random-{trial}.tmap")
        for mode in (1,2):
            cost = reference(graph,0,11,mode,1.5)
            expected.append(f"{name} 0 11 {mode} 1.5 {cost if cost != float('inf') else -1}\n")
    (directory/"reference.txt").write_text("".join(expected))
    width,depth = 64,12
    comb_nodes = [{"id":i+1,"point":[i*0.0001,0]} for i in range(width)]
    comb_edges = []
    def connect(a,b):
        comb_edges.extend([{"id":f"{a}-{b}","from":a,"to":b}, {"id":f"{b}-{a}","from":b,"to":a}])
    for i in range(1,width): connect(i,i+1)
    for i in range(width):
        previous = i+1
        for j in range(depth):
            identity = len(comb_nodes)+1
            comb_nodes.append({"id":identity,"point":[i*0.0001,(j+1)*0.0001]})
            connect(previous,identity); previous = identity
    write({"graph":{"nodes":comb_nodes,"edges":comb_edges,"landmarks":4}},directory/"landmarks.tmap")


if __name__ == "__main__":
    if len(sys.argv) > 2: tmap_pack.DEFAULT_PAGE_BYTES = int(sys.argv[2])
    main(sys.argv[1])
