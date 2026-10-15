"""Check actual Center output made with installed SDK tools against source output."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('selection', type=Path)
    parser.add_argument('--probe', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = json.loads((args.artifact / 'manifest.json').read_text())
    reference = json.loads((args.reference / 'manifest.json').read_text())
    package = args.artifact / manifest['output']['file']
    if package.read_bytes() != (args.reference / reference['output']['file']).read_bytes():
        raise AssertionError('installed producer changed the real source-reference package')
    for record in [manifest['source'], *manifest['regionalSources']]:
        if sha(Path(record['path'])) != record['sha256']:
            raise AssertionError('original OSM input changed')
    graph = json.loads((args.artifact / 'graph.json').read_text())['graph']
    nodes = sorted(graph['nodes'], key=lambda n: n['id'])
    ids = {n['id']: i for i, n in enumerate(nodes)}
    edges = sorted(graph['edges'], key=lambda e: (ids[e['from']], str(e['id'])))
    selection = json.loads(args.selection.read_text())
    cases = []
    for probe in args.probe:
        for reverse in (False, True):
            start, end = selection['start_osm_node'], selection['destination_osm_node']
            if reverse:
                start, end = end, start
            for algorithm in range(5):
                run = subprocess.run([str(probe.resolve()), str(package.resolve()), '0', '0', '1',
                                      str(ids[start]), str(ids[end]), str(algorithm)],
                                     capture_output=True, text=True, check=True)
                value = json.loads(run.stdout)
                if value['result'] != 'ok' or len(value['edges']) != 2:
                    raise AssertionError(value)
                current = start
                for row in value['edges']:
                    edge = edges[row]
                    if edge['from'] != current or not edge['access'] & 1:
                        raise AssertionError('disconnected/forbidden route')
                    current = edge['to']
                if current != end:
                    raise AssertionError('wrong destination')
                cases.append(dict(probe_sha256=sha(probe), reverse=reverse, algorithm=algorithm, **value))
    report = dict(scope='Actual Center CLI production using installed SDK Python tools; package byte identity '
                        'against source-tools Center output for two selected original OSM ways. '
                        'Both directions, five integer algorithms, cold Busy probes. Not whole-region capacity '
                        'or physical MCU/PSRAM evidence.',
                  package_sha256=sha(package), package_bytes=package.stat().st_size,
                  manifest=manifest, reference_manifest_sha256=sha(args.reference / 'manifest.json'),
                  nodes=len(nodes), edges=len(edges), cases=cases)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(cases=len(cases), bytes=package.stat().st_size, nodes=len(nodes), edges=len(edges))))


if __name__ == '__main__':
    main()
