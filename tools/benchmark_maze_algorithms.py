"""Resource-operation comparison of pathfinding primitives, not MCU timing.

Counts edge inspections and frontier events on known graphs. Modified flood is
represented by weighted FIFO relaxation; this is not a claim to reproduce every
competition variant. Adachi 0..3 are exploration policies, not interchangeable
shortest-path kernels, and are documented separately in ROUTING_RESEARCH.md.
RAM below is a declared C-array model, never Python RSS or measured MCU RAM.
"""
import argparse
from collections import deque
import heapq
import json
import random
from pathlib import Path

INF = 2**63-1


def solve(adjacency, start, goal, method, heuristic=None):
    n = len(adjacency)
    reverse = [[] for _ in adjacency]
    for a, edges in enumerate(adjacency):
        for b, weight in edges:
            if weight <= 0: raise ValueError("positive weights required")
            reverse[b].append((a, weight))
    scans = updates = pops = 0
    distance = [INF]*n
    if method in ("flood", "modified_flood", "bellman"):
        distance[goal] = 0
        if method == "flood" and any(w != 1 for edges in adjacency for _, w in edges):
            return {"supported": False, "reason": "FIFO unit flood does not minimize arbitrary weights"}
        if method == "bellman":
            for iteration in range(n):
                changed = False
                for a, edges in enumerate(adjacency):
                    for b, w in edges:
                        scans += 1
                        value = min(INF, distance[b]+w)
                        if value < distance[a]:
                            distance[a] = value; updates += 1; changed = True
                if not changed: break
            capacity, state_bytes = n, 12 # int64 cost + uint32 successor, no queue.
        else:
            queue = deque([goal]); queued = {goal}
            while queue:
                node = queue.popleft(); queued.remove(node); pops += 1
                for other, weight in reverse[node]:
                    scans += 1
                    value = distance[node]+weight
                    if value < distance[other]:
                        distance[other] = value; updates += 1
                        if other not in queued: queue.append(other); queued.add(other)
            capacity, state_bytes = n, 7 if method == "flood" else 17
            # Uniform <=65534-step field: uint16 cost/successor/queue + queued byte.
            # General weighted relaxation: int64 cost + u32 successor/queue + byte.
        cost = distance[start]
    else:
        distance[start] = 0
        h = heuristic or [0]*n
        heap = [(h[start],0,start)]
        seen = {start}
        while heap:
            _, negative_g, node = heapq.heappop(heap); pops += 1
            g = -negative_g
            if g != distance[node]: continue
            if node == goal: break
            for other, weight in adjacency[node]:
                scans += 1
                value = g+weight
                if value < distance[other]:
                    distance[other] = value; updates += 1; seen.add(other)
                    heapq.heappush(heap,(value+h[other],-value,other))
        cost = distance[goal]
        capacity, state_bytes = len(seen), 36 # Existing TMAP sparse label + indexed heap slot.
    return {"supported": True,"cost": None if cost == INF else cost,"edge_inspections":scans,
            "cost_updates":updates,"frontier_pops":pops,"model_state_slots":capacity,
            "model_state_bytes":capacity*state_bytes}


def grid(side, weighted):
    n = side*side; graph = [[] for _ in range(n)]
    rng = random.Random(17014)
    for y in range(side):
        for x in range(side):
            a = y*side+x
            for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                xx, yy = x+dx, y+dy
                if 0 <= xx < side and 0 <= yy < side:
                    b = yy*side+xx
                    graph[a].append((b,rng.randrange(1,101) if weighted else 1))
    # Manhattan distance is a proven bound here only: every edge costs >=1.
    h = [side-1-x+side-1-y for y in range(side) for x in range(side)]
    return graph,0,n-1,h


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    args = parser.parse_args()
    report = {"scope":"Known node graphs without turn-state expansion; primitive operation counts, not device timing or MCU memory measurements.",
              "ram_model":"Uniform flood uses uint16 cost/successor/queue and queued byte (7/state). General weighted fields use cost8+successor4; FIFO adds queue4+queued1. Sparse search models TMAP label32+heap4. Capacity rounding, controls, cache, graph and Python lazy-heap duplicates excluded; not physical MCU memory.","cases":[]}
    for name, weighted in (("uniform_16x16",False),("weighted_16x16",True)):
        graph,start,goal,h = grid(16,weighted)
        results = {method:solve(graph,start,goal,method,h if method == "astar" else None)
                   for method in ("flood","modified_flood","bellman","dijkstra","astar")}
        expected = results["dijkstra"]["cost"]
        if any(r.get("supported") and r["cost"] != expected for r in results.values()): raise ValueError("cost mismatch")
        report["cases"].append({"name":name,"nodes":len(graph),"results":results})
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__ == "__main__": main()
