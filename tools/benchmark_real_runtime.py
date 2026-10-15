"""Verify real Center conditional edges with cold bounded SDK queries."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import subprocess
from import_osm import access
from osm_conditions import resolve_tags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('artifact', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--algorithms', type=int, nargs='+', choices=range(5), default=list(range(4)))
    args = parser.parse_args()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest_path = args.artifact / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    package = args.artifact / manifest['output']['file']
    source = Path(manifest['source']['path'])
    if not manifest['RuntimeConditions'] or sha(package) != manifest['output']['sha256'] or sha(source) != manifest['source']['sha256']:
        raise ValueError('Center provenance mismatch')
    graph = json.loads((args.artifact / 'graph.json').read_text())['graph']
    nodes = sorted(graph['nodes'], key=lambda n: n['id'])
    node_index = {n['id']: i for i, n in enumerate(nodes)}
    edges = sorted(graph['edges'], key=lambda e: (node_index[e['from']], str(e['id'])))
    selected = [(i, e) for i, e in enumerate(edges) if any(k.endswith(':conditional') for k in e.get('road_tags', {}))]
    if not selected:
        raise ValueError('source has no represented conditional roads')
    # Both seasons and rush/off-peak hours; offset is explicit, not inferred by SDK.
    instants = ('2026-01-15T07:00:00+01:00', '2026-01-15T12:00:00+01:00',
                '2026-06-15T07:00:00+02:00', '2026-06-15T12:00:00+02:00')
    cases, routes = [], []
    for text in instants:
        instant = datetime.fromisoformat(text)
        utc, offset = int(instant.timestamp()), int(instant.utcoffset().total_seconds())
        if graph.get('restrictions'):
            raise ValueError('reachability oracle requires graph without turn restrictions')
        adjacency = {}
        for leg in edges:
            tags = leg.get('road_tags')
            mask = (access(resolve_tags(tags, instant, 1), 1) if tags else leg['access']) & leg['access']
            if mask & 1:
                adjacency.setdefault(leg['from'], []).append(leg['to'])
        reachable = {}
        def probe(start, end, algorithm, edge=False):
            command = [str(args.probe.resolve()), str(package.resolve()), str(utc), str(offset), '1', str(start), str(end), str(algorithm)]
            if edge:
                command.append('edge')
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            return json.loads(result.stdout)
        for row, edge in selected:
            tags = edge['road_tags']
            expected = access(resolve_tags(tags, instant, 1), 1) & edge['access']
            value = probe(row, 0, 0, True)
            if value['result'] != 'ok' or value['access'] != expected:
                raise AssertionError((text, edge['id'], expected, value))
            cases.append(dict(local_instant=text, edge_row=row, edge_id=edge['id'], tags=tags, expected_access=expected, **value))
        # One actual segment per source way, both directions, all algorithms.
        seen = set()
        for row, edge in selected:
            key = (str(edge['id']).split(':')[0], str(edge['id']).split(':')[-1])
            if key in seen:
                continue
            seen.add(key)
            if edge['from'] not in reachable:
                visited, pending = {edge['from']}, [edge['from']]
                while pending:
                    for node in adjacency.get(pending.pop(), ()):
                        if node not in visited:
                            visited.add(node)
                            pending.append(node)
                reachable[edge['from']] = visited
            oracle_reachable = edge['to'] in reachable[edge['from']]
            for algorithm in args.algorithms:
                value = probe(node_index[edge['from']], node_index[edge['to']], algorithm)
                if value['result'] != 'budget_exceeded' and (value['result'] == 'ok') != oracle_reachable:
                    raise AssertionError('SDK outcome differs from host full-graph reachability')
                if value['result'] == 'ok':
                    current = edge['from']
                    for route_row in value['edges']:
                        leg = edges[route_row]
                        if leg['from'] != current:
                            raise AssertionError('route continuity')
                        tags = leg.get('road_tags')
                        mask = access(resolve_tags(tags, instant, 1), 1) if tags else leg['access']
                        if not mask & 1:
                            raise AssertionError('route traverses closed conditional road')
                        current = leg['to']
                    if current != edge['to']:
                        raise AssertionError('route endpoint')
                routes.append(dict(local_instant=text, selected_edge_id=edge['id'], oracle_reachable=oracle_reachable, start_node=node_index[edge['from']],
                                   end_node=node_index[edge['to']], algorithm=algorithm, **value))
    report = dict(scope='Original real PBF exported by Center, no injected conditions. All represented conditional road edges at four explicit instants. Cold process/cache per query; Busy every other read. Host snapshot policy is the edge oracle. Full-graph host reachability checks completed outcomes; route continuity and active road access checked. No independent shortest-path oracle or board memory measurement. Fixed 32 search/output slots, ALT workspace retained for all algorithms. Relation access polygons are outside road/turn scope.',
                  manifest_sha256=sha(manifest_path), graph_sha256=sha(args.artifact/'graph.json'), source_sha256=sha(source),
                  package_sha256=sha(package), probe_sha256=sha(args.probe), package_bytes=package.stat().st_size,
                  nodes=len(nodes), edges=len(edges), conditional_edges=len(selected),
                  conditional_ways=len({str(e['id']).split(':')[0] for _, e in selected}), cases=cases, routes=routes,
                  route_results=dict(Counter(r['result'] for r in routes)))
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('package_bytes', 'nodes', 'edges', 'conditional_edges', 'conditional_ways', 'route_results')}))


if __name__ == '__main__':
    main()
