"""Expand ordered OSM via-way members into bounded directed edge sequences."""


def via_way_sequences(from_way, to_way, via_ways, way_refs, way_edges, mode, max_paths=4096, max_edges=64):
    chain = (from_way,*via_ways,to_way)
    connections = []
    for a,b in zip(chain,chain[1:]):
        common = set(way_refs[a]) & set(way_refs[b])
        if len(common) != 1: raise ValueError('ambiguous/disconnected via-way junction')
        connections.append(next(iter(common)))
    incoming = [e for e in way_edges.get(from_way,[]) if e['to'] == connections[0] and e['access'] & mode]
    outgoing = [e for e in way_edges.get(to_way,[]) if e['from'] == connections[-1] and e['access'] & mode]
    if len(incoming) != 1 or len(outgoing) != 1: raise ValueError('ambiguous/inaccessible via-way boundary')
    sequences = [(incoming[0]['id'],)]
    work = 0
    for index,way in enumerate(via_ways):
        start,end = connections[index:index+2]
        adjacent = {}
        for edge in way_edges.get(way,[]):
            if edge['access'] & mode: adjacent.setdefault(edge['from'],[]).append(edge)
        paths, pending = [], [(start,(),frozenset((start,)))]
        while pending:
            node,path,seen = pending.pop()
            work += 1
            if work > max_paths*max_edges: raise ValueError('via-way exploration budget exceeded')
            for edge in adjacent.get(node,[]):
                candidate = path+(edge['id'],)
                if len(candidate) >= max_edges: raise ValueError('via-way sequence edge budget exceeded')
                if edge['to'] == end:
                    paths.append(candidate)
                    if len(paths) > max_paths: raise ValueError('via-way path budget exceeded')
                elif edge['to'] not in seen:
                    pending.append((edge['to'],candidate,seen | {edge['to']}))
        if not paths: raise ValueError('no legal traversal of via way')
        if len(sequences)*len(paths) > max_paths: raise ValueError('via-way combination budget exceeded')
        sequences = [a+b for a in sequences for b in paths]
    sequences = [s+(outgoing[0]['id'],) for s in sequences]
    if any(len(s) > max_edges for s in sequences): raise ValueError('via-way sequence edge budget exceeded')
    return sorted(set(sequences))
