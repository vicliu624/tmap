#!/usr/bin/env python3
"""Merge compatible regional graph documents before embedded packaging.

This produces one connected, paged graph without a device-side global ID map.
Inputs must declare the same topology_epoch (source snapshot/profile contract).
Shared stable node IDs must have identical E7 coordinates; conflicting edges,
heights and restrictions are rejected. Boundary warnings remain conservative.
"""
import argparse
import copy
import json
from pathlib import Path
from tmap_pack import point


def regional_contract(graphs):
    if not graphs: raise ValueError("no graphs")
    epoch = graphs[0].get("topology_epoch")
    if not epoch or any(g.get("topology_epoch") != epoch for g in graphs):
        raise ValueError("missing or incompatible topology_epoch")
    snapshot = graphs[0].get("condition_snapshot")
    modes = graphs[0].get("travel_modes")
    if any(g.get("condition_snapshot") != snapshot for g in graphs):
        raise ValueError("incompatible conditional snapshots")
    if any(g.get("travel_modes") != modes for g in graphs):
        raise ValueError("incompatible travel mode profiles")
    return epoch, snapshot, modes


def merge_graphs(graphs):
    epoch, snapshot, modes = regional_contract(graphs)
    nodes, edges, restrictions, history, conditional = {}, {}, {}, {}, {}
    conditional_groups = {}
    for graph in graphs:
        for node in graph["nodes"]:
            key = int(node["id"])
            if key in nodes:
                existing = nodes[key]
                if point(existing["point"]) != point(node["point"]): raise ValueError("shared node coordinate mismatch")
                a, b = existing.get("elevation_m"), node.get("elevation_m")
                if a is not None and b is not None and a != b: raise ValueError("shared node height mismatch")
                if a is None and b is not None: existing["elevation_m"] = b
                existing["boundary"] = bool(existing.get("boundary") or node.get("boundary"))
            else: nodes[key] = copy.deepcopy(node)
        for edge in graph["edges"]:
            key = str(edge["id"])
            if key in edges and edges[key] != edge: raise ValueError("shared edge differs")
            edges[key] = copy.deepcopy(edge)
        for rule in graph.get("restrictions",[]):
            key = json.dumps(rule,sort_keys=True)
            restrictions[key] = copy.deepcopy(rule)
        for rule in graph.get('via_restrictions',[]):
            history[json.dumps(rule,sort_keys=True)] = copy.deepcopy(rule)
        for rule in graph.get('conditional_turns',[]):
            group = (rule['group'], rule['mode'])
            tags = json.dumps(rule['tags'], sort_keys=True)
            if group in conditional_groups and conditional_groups[group] != tags:
                raise ValueError('shared conditional turn group differs')
            conditional_groups[group] = tags
            conditional[json.dumps(rule,sort_keys=True)] = copy.deepcopy(rule)
    result = {"topology_epoch": epoch, "nodes": [nodes[k] for k in sorted(nodes)],
            "edges": [edges[k] for k in sorted(edges)], "restrictions": [restrictions[k] for k in sorted(restrictions)]}
    if snapshot is not None: result["condition_snapshot"] = snapshot
    if modes is not None: result["travel_modes"] = modes
    if history: result['via_restrictions'] = [history[k] for k in sorted(history)]
    if conditional: result['conditional_turns'] = [conditional[k] for k in sorted(conditional)]
    landmarks = max(g.get('landmarks', 0) for g in graphs)
    if landmarks: result['landmarks'] = landmarks
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs",nargs="+",type=Path); parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--terrain-document",type=Path)
    args = parser.parse_args()
    documents = [json.loads(p.read_text(encoding="utf-8")) for p in args.inputs]
    result = json.loads(args.terrain_document.read_text(encoding="utf-8")) if args.terrain_document else {"name": "merged-regions"}
    result["graph"] = merge_graphs([d["graph"] for d in documents])
    args.output.write_text(json.dumps(result,ensure_ascii=False),encoding="utf-8")


if __name__ == "__main__": main()
