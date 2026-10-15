#!/usr/bin/env python3
"""Import regional OSM XML or PBF as an offline foot/bicycle graph.

Conservative static profile: excludes private/destination-only access; preserves
one-way cycling, node-via and bounded ordered via-way restrictions, bridge/tunnel topology and shared shapes.
Unsupported conditional access and ambiguous/incomplete via-way rules cause
an error rather than silently producing an invalid route graph.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

FOOT = {"footway", "path", "pedestrian", "steps", "living_street", "residential", "service",
        "unclassified", "track", "tertiary", "secondary", "primary"}
BIKE = (FOOT-{"steps"}) | {"cycleway"}
DENIED = {"no", "private", "destination", "customers", "delivery", "agricultural", "forestry"}
ALLOWED = {"yes", "designated", "permissive", "official"}
TURN_KINDS = {"no_left_turn", "no_right_turn", "no_straight_on", "no_u_turn",
              "only_left_turn", "only_right_turn", "only_straight_on", "only_u_turn",
              "no_entry", "no_exit"}


def tags(element): return {x.attrib["k"]: x.attrib["v"] for x in element.findall("tag")}


def access(t, allowed_modes=3):
    general = t.get("access", "") not in DENIED
    foot = t.get("highway") in FOOT and general
    bike = t.get("highway") in BIKE and general and t.get("vehicle", "") not in DENIED
    for key, old in (("foot", foot), ("bicycle", bike)):
        value = t.get(key)
        new = True if value in ALLOWED else False if value in DENIED or value == "dismount" else old
        if key == "foot": foot = new
        else: bike = new
    foot = foot and bool(allowed_modes & 1)
    bike = bike and bool(allowed_modes & 2)
    # A condition can enable an otherwise excluded road. Validate potential
    # roads before the static mask discards them; do not silently lose topology.
    conditional = []
    if allowed_modes & 1 and (t.get("highway") in FOOT or "foot" in t or "foot:conditional" in t):
        conditional += ["access:conditional", "foot:conditional", "oneway:foot:conditional"]
    if allowed_modes & 2 and (t.get("highway") in BIKE or "bicycle" in t or "bicycle:conditional" in t):
        conditional += ["access:conditional", "bicycle:conditional", "vehicle:conditional",
                        "oneway:conditional", "oneway:bicycle:conditional"]
    if any(k in t for k in conditional):
        raise ValueError("conditional road rules require a richer profile")
    return int(foot) | (int(bike) << 1)


def xml_records(path):
    """Release parsed XML entities after the consumer has extracted their data."""
    with open(path, "rb") as source:
        context = ET.iterparse(source, events=("start", "end"))
        _, root = next(context)
        if root.tag != "osm": raise ValueError("expected an OSM snapshot")
        for event, element in context:
            if event == "end" and element.tag in ("node", "way", "relation"):
                yield element
                root.clear()


def import_xml(path, allowed_modes=3, condition_instant=None, runtime_conditions=False):
    return import_records(xml_records(path), allowed_modes, condition_instant, runtime_conditions)


def import_pbf(path, allowed_modes=3, condition_instant=None, runtime_conditions=False):
    from osm_pbf import iter_pbf
    return import_records(iter_pbf(path), allowed_modes, condition_instant, runtime_conditions)


def import_records(records, allowed_modes=3, condition_instant=None, runtime_conditions=False):
    if allowed_modes not in (1,2,3): raise ValueError("invalid travel mode profile")
    if type(runtime_conditions) is not bool or (runtime_conditions and condition_instant is not None):
        raise ValueError('runtime conditions require no production snapshot')
    runtime_ways = {}
    coordinates, node_tags = {}, {}
    way_records, relation_records = [], []
    for element in records:
        if element.tag == "node":
            key = int(element.attrib["id"])
            coordinates[key] = [float(element.attrib["lon"]), float(element.attrib["lat"])]
            t = tags(element)
            if condition_instant is not None:
                from osm_conditions import resolve_tags
                t = resolve_tags(t, condition_instant, allowed_modes)
            node_tags[key] = t
        elif element.tag == "way":
            t = tags(element)
            if condition_instant is not None:
                from osm_conditions import resolve_tags
                t = resolve_tags(t, condition_instant, allowed_modes)
            if "highway" in t:
                way_records.append((int(element.attrib["id"]),
                                    [int(x.attrib["ref"]) for x in element.findall("nd")], t))
        elif element.tag == "relation":
            t = tags(element)
            legacy_mode = {'restriction:foot':'foot','restriction:bicycle':'bicycle'}.get(t.get('type'))
            if legacy_mode:
                t['type'] = 'restriction'
                for suffix in ('',':conditional'):
                    old = 'restriction'+suffix
                    if old in t: t.setdefault('restriction:'+legacy_mode+suffix,t.pop(old))
            if condition_instant is not None:
                from osm_conditions import resolve_tags
                t = resolve_tags(t, condition_instant, allowed_modes)
            if t.get("type") == "restriction":
                relation_records.append((int(element.attrib["id"]), t, [dict(m.attrib) for m in element.findall("member")]))
    ways = []
    uses = {}
    for way_id, refs, t in way_records:
        relevant = {'access:conditional'}
        if allowed_modes & 1: relevant.update(('foot:conditional','oneway:foot:conditional'))
        if allowed_modes & 2: relevant.update(('vehicle:conditional','bicycle:conditional','oneway:conditional','oneway:bicycle:conditional'))
        if runtime_conditions and any(key in t for key in relevant):
            from compile_road_conditions import compile_road_conditions
            policy = dict(t)
            for key in tuple(policy):
                if key.endswith(':conditional') and key not in relevant: del policy[key]
            if not allowed_modes & 1:
                policy['foot'] = 'no'; policy['oneway:foot'] = 'no'
            if not allowed_modes & 2:
                policy['bicycle'] = 'no'; policy['vehicle'] = 'no'
                policy['oneway'] = 'no'; policy['oneway:bicycle'] = 'no'
            compile_road_conditions(policy)  # Validate every branch before retaining topology.
            runtime_ways[way_id] = policy
            mode = allowed_modes
        else:
            mode = access(t,allowed_modes)
        if not mode: continue
        if len(refs) < 2: continue
        if any(n not in coordinates for n in refs):
            raise ValueError("OSM extract lacks referenced nodes; use a complete regional extract")
        for n in set(refs): uses[n] = uses.get(n, 0)+1
        ways.append((way_id, refs, t, mode))
    included = {w[0] for w in ways}
    way_refs = {w[0]:set(w[1]) for w in ways}
    relations = []
    runtime_relations = []
    via_nodes = set()
    for relation_id, t, member_records in relation_records:
        excepted = {value.strip() for value in t.get("except", "").split(";")}
        rule_keys = [(key,mask) for key,mask in (("restriction",2),("restriction:bicycle",2),("restriction:foot",1))
                     if mask & allowed_modes and not (mask == 2 and "bicycle" in excepted) and not (mask == 1 and "foot" in excepted)]
        conditional_keys = (["restriction:foot:conditional"] if allowed_modes & 1 and 'foot' not in excepted else []) + (
            ["restriction:conditional","restriction:bicycle:conditional"] if allowed_modes & 2 and 'bicycle' not in excepted else [])
        # Restrictions for an excluded mode cannot affect this graph.
        if not any(t.get(key) for key,_ in rule_keys) and not any(key in t for key in conditional_keys): continue
        members = {role:[m for m in member_records if m.get("role") == role]
                   for role in ("from","to","via")}
        froms = [int(m["ref"]) for m in members["from"]]
        tos = [int(m["ref"]) for m in members["to"]]
        if not (set(froms) & included or set(tos) & included): continue
        if not froms or not tos or not members["via"]:
            raise ValueError(f"restriction {relation_id} lacks from/to/via members")
        if any(m.get("type") != "way" for m in members["from"]+members["to"]):
            raise ValueError("restriction from/to members must be ways")
        if any(k in t for k in conditional_keys):
            if not runtime_conditions: raise ValueError("conditional restriction needs an explicit resolved snapshot")
        if len(members['via']) == 1 and members['via'][0]['type'] == 'node':
            via = int(members['via'][0]['ref']); via_nodes.add(via)
        elif all(m['type'] == 'way' for m in members['via']):
            via = tuple(int(m['ref']) for m in members['via'])
            if len(via) > 32 or len(set(via)) != len(via) or any(w not in included for w in via):
                raise ValueError('incomplete/duplicate/oversized via-way members')
            for start_way in froms:
                for end_way in tos:
                    if start_way not in included or end_way not in included: continue
                    for a,b in zip((start_way,*via),(*via,end_way)):
                        common = set(way_refs[a]) & set(way_refs[b])
                        if len(common) != 1: raise ValueError('ambiguous/disconnected via-way junction')
                        via_nodes.update(common)
        else: raise ValueError('mixed/unsupported via members')
        if len(set(froms)) != len(froms) or len(set(tos)) != len(tos):
            raise ValueError("duplicate restriction member")
        if isinstance(via,int) and any(via not in way_refs[w] for w in froms+tos if w in included):
            raise ValueError("restriction way does not connect to via node")
        rules = []
        dynamic_modes = []
        if runtime_conditions:
            from osm_conditional_turns import conditional_turn_modes
            dynamic_modes = conditional_turn_modes(t,allowed_modes)
            if dynamic_modes: runtime_relations.append((relation_id,froms,tos,via,t,dynamic_modes))
        # Generic OSM turn restrictions apply to cycling unless excepted;
        # pedestrian restrictions require explicit restriction:foot.
        excepted = {value.strip() for value in t.get("except", "").split(";")}
        for key, mask in rule_keys:
            if any(mask == dynamic_mask for _,dynamic_mask,_ in dynamic_modes): continue
            if key == "restriction" and "restriction:bicycle" in t: continue
            value = t.get(key)
            if not value or (mask == 2 and "bicycle" in excepted) or (mask == 1 and "foot" in excepted): continue
            if value == 'none': continue
            if value not in TURN_KINDS: raise ValueError("unknown or dynamic restriction kind")
            if len(froms) > 1 and value != "no_entry": raise ValueError("multiple from members require no_entry")
            if len(tos) > 1 and value != "no_exit": raise ValueError("multiple to members require no_exit")
            rules.append((mask, "only" if value.startswith("only_") else "no", value))
        for from_way in froms:
            for to_way in tos:
                if from_way in included and to_way not in included and any(kind == 'only' for _,kind,_ in rules):
                    raise ValueError('only-turn target is outside the included graph')
                if from_way in included and to_way in included:
                    relations.append((from_way, to_way, via, rules))
    junctions = set(via_nodes)
    for _, refs, _, _ in ways:
        junctions.update((refs[0], refs[-1]))
        junctions.update(n for n in refs if uses[n] > 1 or node_tags[n].get("barrier"))
    node_list = [{"id": n, "point": coordinates[n]} for n in sorted(junctions) if n in coordinates]
    edges = []
    way_edges = {}
    for way_id, refs, t, mode in ways:
        start = 0
        for i in range(1, len(refs)):
            if refs[i] not in junctions: continue
            a, b = refs[start], refs[i]
            forward, backward = mode, mode
            pending_barriers = []
            for endpoint in (a, b):
                nt = node_tags[endpoint]
                if nt.get("barrier"):
                    conditional_keys = ["access:conditional"]
                    if mode & 1: conditional_keys.append("foot:conditional")
                    if mode & 2: conditional_keys.extend(("vehicle:conditional", "bicycle:conditional"))
                    if any(key in nt for key in conditional_keys):
                        if not runtime_conditions: raise ValueError("conditional barrier needs an explicit resolved snapshot")
                        from compile_barrier_conditions import encode_barrier_program
                        policy = dict(nt)
                        for key in tuple(policy):
                            if key.endswith(':conditional') and key not in conditional_keys: del policy[key]
                        if not mode & 1: policy['foot'] = 'no'
                        if not mode & 2: policy['bicycle'] = 'no'; policy['vehicle'] = 'no'
                        encode_barrier_program(policy)
                        pending_barriers.append(policy)
                        continue
                    barrier = nt["barrier"]
                    permitted = 3 if barrier in ("gate", "lift_gate", "bollard", "cycle_barrier", "entrance") else 1 if barrier in ("stile", "kissing_gate") else 0
                    if nt.get("access") in DENIED: permitted = 0
                    if nt.get("vehicle") in DENIED: permitted &= ~2
                    for key, bit in (("foot",1),("bicycle",2)):
                        if nt.get(key) in ALLOWED: permitted |= bit
                        elif nt.get(key) in DENIED or nt.get(key) == "dismount": permitted &= ~bit
                    forward &= permitted; backward &= permitted
            oneway = t.get("oneway", "yes" if t.get("junction") == "roundabout" else "no")
            bicycle_oneway = t.get("oneway:bicycle", oneway)
            if way_id not in runtime_ways:
                if bicycle_oneway in ("yes", "1", "true"): backward &= ~2
                elif bicycle_oneway == "-1": forward &= ~2
                if t.get("oneway:foot") in ("yes", "1", "true"): backward &= ~1
                elif t.get("oneway:foot") == "-1": forward &= ~1
            f, r = f"{way_id}:{start}:f", f"{way_id}:{start}:r"
            path = [coordinates[n] for n in refs[start:i+1]]
            if forward:
                edges.append({"id": f, "from": a, "to": b, "points": path, "access": forward, "name": t.get("name", ""), "opposite": r})
                way_edges.setdefault(way_id, []).append(edges[-1])
                if way_id in runtime_ways: edges[-1].update(road_tags=runtime_ways[way_id],road_reverse=False)
                if pending_barriers: edges[-1]['barrier_tags'] = pending_barriers
            if backward:
                edges.append({"id": r, "from": b, "to": a, "points": path[::-1], "access": backward, "name": t.get("name", ""), "opposite": f})
                way_edges.setdefault(way_id, []).append(edges[-1])
                if way_id in runtime_ways: edges[-1].update(road_tags=runtime_ways[way_id],road_reverse=True)
                if pending_barriers: edges[-1]['barrier_tags'] = pending_barriers
            start = i
    restrictions, history_restrictions = [], []
    for a, b, via, rules in relations:
        if isinstance(via,tuple):
            from osm_via_paths import via_way_sequences
            for mask,kind,value in rules:
                if not any(e['access'] & mask for e in way_edges.get(a,[])): continue
                for sequence in via_way_sequences(a,b,via,way_refs,way_edges,mask):
                    history_restrictions.append({'edges':list(sequence),'access':mask,'kind':kind})
            continue
        incoming = [e for e in way_edges.get(a, []) if e["to"] == via]
        outgoing = [e for e in way_edges.get(b, []) if e["from"] == via]
        for mask, kind, value in rules:
            targets = [e for e in outgoing if e["access"] & mask]
            if len(targets) > 1 and not value.endswith("u_turn"):
                raise ValueError("ambiguous only-turn relation")
            for x in incoming:
                selected = [y for y in targets if y["to"] == x["from"]] if value.endswith("u_turn") else targets
                if kind == "only" and len(selected) != 1: raise ValueError("ambiguous only-turn relation")
                for y in selected:
                    if x["access"] & mask:
                        restrictions.append({"from": x["id"], "to": y["id"], "access": mask, "kind": kind})
    if not edges: raise ValueError("no routable roads")
    graph = {"nodes": node_list, "edges": edges, "restrictions": restrictions,
             "travel_modes": allowed_modes}
    if history_restrictions: graph['via_restrictions'] = history_restrictions
    conditional_turns = []
    for relation_id,froms,tos,via,t,modes in runtime_relations:
        from osm_conditional_turns import conditional_relation_paths
        paths = conditional_relation_paths(relation_id,froms,tos,via,t,modes,way_refs,way_edges)
        if len(conditional_turns)+len(paths) > 4096: raise ValueError('conditional turn relation budget exceeded')
        conditional_turns.extend(paths)
    if conditional_turns: graph['conditional_turns'] = conditional_turns
    if condition_instant is not None:
        graph["condition_snapshot"] = condition_instant.isoformat()
    return graph


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path); parser.add_argument("output", type=Path)
    parser.add_argument("--format", choices=("auto", "xml", "pbf"), default="auto")
    parser.add_argument("--travel-mode", choices=("foot","bicycle","both"), default="both")
    parser.add_argument("--terrain-document", type=Path)
    parser.add_argument("--condition-at", help="explicit local ISO instant with UTC offset; weekly/time snapshot only")
    parser.add_argument("--runtime-conditions", action="store_true", help="retain conditional road topology; runtime reader support required")
    parser.add_argument("--topology-epoch",help="explicit source snapshot identity for compatible regional merges")
    parser.add_argument("--region-input", action="append", type=Path, default=[], help="additional compatible original XML/PBF; union before graph segmentation")
    parser.add_argument("--max-region-entities", type=int, default=1_000_000, help="host-only unique raw regional entity limit")
    parser.add_argument("--max-region-references", type=int, default=8_000_000, help="host-only retained way/member reference limit")
    args = parser.parse_args()
    doc = json.loads(args.terrain_document.read_text(encoding="utf-8")) if args.terrain_document else {"name": args.input.stem}
    input_format = args.format if args.format != "auto" else "pbf" if args.input.suffix.lower() == ".pbf" else "xml"
    modes = {"foot":1,"bicycle":2,"both":3}[args.travel_mode]
    from osm_conditions import parse_instant
    instant = parse_instant(args.condition_at) if args.condition_at else None
    if args.region_input:
        if not args.topology_epoch: parser.error('regional sources require an explicit compatible topology epoch')
        from merge_osm_regions import import_regions
        doc['graph'], doc['regional_sources'] = import_regions([args.input,*args.region_input], modes, instant,
            args.runtime_conditions, args.max_region_entities, args.format, args.max_region_references)
        doc['regional_policy'] = 'Stable OSM identity/E7/tag/member union before junction and restriction construction; no coordinate proximity joins.'
    else:
        doc["graph"] = import_pbf(args.input,modes,instant,args.runtime_conditions) if input_format == "pbf" else import_xml(args.input,modes,instant,args.runtime_conditions)
    if instant is not None: doc["condition_snapshot"] = instant.isoformat()
    if args.topology_epoch: doc["graph"]["topology_epoch"] = args.topology_epoch
    args.output.write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__": main()
