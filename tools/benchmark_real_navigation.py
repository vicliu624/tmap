"""Measure a Center-produced real graph package using the C++ core.

Deterministic endpoint sampling in the largest weak component, not manually
chosen successful routes. All failures remain in the report. Host payload sums
and exact-read counts are not board RAM peaks, SD latency or energy evidence.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import struct
from tmap_pack import build, graph_sections, EDGES


def benchmark(artifact,probe,capacities,busy_every,ascent_penalty=0,algorithms=None):
    if not 0 <= ascent_penalty <= 1000: raise ValueError("ascent penalty must be an integer in 0..1000")
    algorithms = algorithms or ("dijkstra","alt","fifo","integer")
    if "dijkstra" not in algorithms: raise ValueError("Dijkstra baseline is required")
    document = json.loads((artifact/"graph.json").read_text(encoding="utf-8"))
    manifest = json.loads((artifact/"manifest.json").read_text(encoding="utf-8"))
    package = artifact/manifest["output"]["file"]
    if hashlib.sha256(package.read_bytes()).hexdigest() != manifest["output"]["sha256"]:
        raise ValueError("Center package digest mismatch")
    reproduced,_ = build(document)
    if hashlib.sha256(reproduced).hexdigest() != manifest["output"]["sha256"]:
        raise ValueError("graph document does not reproduce Center package")
    graph = document["graph"]
    terrain_edges = None
    oracle_edges = None
    if "dem" in document:
        edge_bytes = next(raw for kind,raw,_ in graph_sections(graph,document["dem"]) if kind == EDGES)
        terrain_edges = sum(bool(struct.unpack_from("<H",edge_bytes,offset+10)[0] & 2)
                            for offset in range(0,len(edge_bytes),48))
        if len(graph["nodes"]) <= 256 and len(graph["edges"]) <= 4096 and not graph.get("restrictions"):
            oracle_edges = []
            for offset in range(0,len(edge_bytes),48):
                a,b,access,_,flags,length,ascent = struct.unpack_from("<IIBBHII",edge_bytes,offset)
                if access & 1:
                    if ascent_penalty and not flags & 2:
                        oracle_edges = None
                        break
                    oracle_edges.append((a,b,length+ascent_penalty*ascent))
    ids = {n["id"]:i for i,n in enumerate(sorted(graph["nodes"],key=lambda n:n["id"]))}
    adjacency = collections.defaultdict(set)
    for e in graph["edges"]:
        a,b = ids[e["from"]],ids[e["to"]]
        adjacency[a].add(b); adjacency[b].add(a)
    remaining = set(ids.values()); components = []
    while remaining:
        seed = min(remaining); stack = [seed]; remaining.remove(seed); component = []
        while stack:
            a = stack.pop(); component.append(a)
            for b in sorted(adjacency[a]):
                if b in remaining: remaining.remove(b); stack.append(b)
        components.append(sorted(component))
    largest = max(components,key=lambda c:(len(c),-c[0]))
    pairs = [(largest[len(largest)*i//8],largest[len(largest)*(7-i)//8]) for i in range(8)]
    results = []
    with tempfile.TemporaryDirectory() as directory:
        queries = Path(directory)/"queries.txt"
        queries.write_text("".join(f"{a} {b}\n" for a,b in pairs))
        for algorithm in algorithms:
            for capacity in capacities:
                output = subprocess.run([str(probe),str(package),str(queries),str(capacity),algorithm,str(busy_every),str(ascent_penalty)],
                                        check=True,capture_output=True,text=True)
                results.extend(json.loads(output.stdout))
    # Highest-capacity exact baselines must agree for successful shared queries.
    baseline = {r["query"]:r for r in results if r["algorithm"] == "dijkstra" and r["capacity"] == max(capacities)}
    for r in results:
        other = baseline[r["query"]]
        if r["result"] == other["result"] == "ok" and r["cost_cm"] != other["cost_cm"]:
            raise ValueError("algorithm cost disagrees with full-capacity baseline")
        if r["result"] == "no_path" and other["result"] == "ok":
            raise ValueError("algorithm reported no path against a successful exact baseline")
    if any(r["result"] not in ("ok","no_path","outside_coverage") for r in baseline.values()):
        raise ValueError("highest-capacity baseline did not finish; increase capacities")
    oracle_costs = None
    if oracle_edges is not None:
        oracle_costs = []
        for start,destination in pairs:
            distance = [None]*len(ids); distance[start] = 0
            for _ in range(len(ids)-1):
                changed = False
                for a,b,weight in oracle_edges:
                    if distance[a] is not None and (distance[b] is None or distance[a]+weight < distance[b]):
                        distance[b] = distance[a]+weight; changed = True
                if not changed: break
            oracle_costs.append(distance[destination])
        for row in results:
            if row["result"] == "ok" and row["cost_cm"] != oracle_costs[row["query"]]:
                raise ValueError("route disagrees with independent integer Bellman oracle")
    manifest["source"]["path"] = Path(manifest["source"]["path"]).name
    if manifest.get("outdoorSource"):
        manifest["outdoorSource"]["path"] = Path(manifest["outdoorSource"]["path"]).name
    return {"producer_manifest":manifest,"local_source_path_reduced_to_basename":True,
            "probe_sha256":hashlib.sha256(probe.read_bytes()).hexdigest(),
            "probe_source_sha256":hashlib.sha256((Path(__file__).resolve().parents[1]/"examples/navigation_resource_probe.cpp").read_bytes()).hexdigest(),
            "sections":json.loads((artifact/"sections.json").read_text()),
            "graph_nodes":len(ids),"graph_edges":len(graph["edges"]),"weak_components":len(components),
            "largest_weak_component_nodes":len(largest),"query_rows":pairs,"capacities":capacities,
            "busy_every":busy_every,"cache_page_capacity":document["page_bytes"],"results":results,
            "ascent_penalty":ascent_penalty,
            "independent_bellman_costs_cm":oracle_costs,
            "terrain_present":"dem" in document,
            "edges_with_complete_elevation":terrain_edges,
            "scope":"Center-produced static foot graph, node endpoints, explicit integer ascent penalty, cold library caches per query. Counts include package open/graph initialization. DEM sampling is production-side; runtime route costs use packed ascent.",
            "ram_scope":"Configured live library workspace/readers/cursor/label/queue/output payload, actual host sizeof; excludes allocator metadata, file adapter, query input and task stack. Not MCU peak.",
            "limitations":"Eight sampled node-endpoint queries; no raster/contour/administration performance or conditional-rule coverage. Ascent uses producer DEM sampling and threshold policy, not independently surveyed trail heights. Desktop filesystem cache affects latency. No board/SD power measurements."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact",type=Path); parser.add_argument("probe",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--capacities",type=int,nargs="+",default=[128,512,2048,8192,10000,16384])
    parser.add_argument("--busy-every",type=int,default=0)
    parser.add_argument("--ascent-penalty",type=int,default=0)
    parser.add_argument("--algorithms",nargs="+",choices=("dijkstra","alt","fifo","integer","integer-dijkstra","integer-heap","integer-alt","integer-label-scan"))
    args = parser.parse_args()
    report = benchmark(args.artifact.resolve(),args.probe.resolve(),args.capacities,args.busy_every,args.ascent_penalty,args.algorithms)
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    counts = collections.Counter((r["algorithm"],r["capacity"],r["result"]) for r in report["results"])
    print(json.dumps({str(k):v for k,v in sorted(counts.items())}))
