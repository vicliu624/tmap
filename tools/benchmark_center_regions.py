"""Cold SDK routing across a Center-produced synthetic raw regional seam."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from tmap_pack import build


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('artifact', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest_path = args.artifact/'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    if not manifest.get('regionalPolicy') or len(manifest.get('regionalSources', [])) != 2:
        raise ValueError('expected west/east/relation source provenance')
    for record in [manifest['source'], *manifest['regionalSources']]:
        if sha(Path(record['path'])) != record['sha256']:
            raise ValueError('source changed')
    document_path = args.artifact/'graph.json'
    document = json.loads(document_path.read_text())
    package = args.artifact/manifest['output']['file']
    if sha(package) != manifest['output']['sha256'] or hashlib.sha256(build(document)[0]).hexdigest() != sha(package):
        raise ValueError('package/graph mismatch')
    graph = document['graph']
    ids = {n['id']:i for i,n in enumerate(sorted(graph['nodes'],key=lambda n:n['id']))}
    edges = sorted(graph['edges'],key=lambda e:(ids[e['from']],str(e['id'])))
    cases = []
    for utc in (345600,432000,518400,0):
        for reverse in (False,True):
            expected = 'no_path' if utc in (432000,518400) or utc == 345600 and not reverse else 'ok'
            edge_ids = ['11:0:r','10:0:r'] if reverse else ['10:0:f','11:0:f']
            for algorithm in range(4):
                result = subprocess.run([str(args.probe.resolve()),str(package.resolve()),str(utc),'0','1',
                    str(ids[4] if reverse else ids[1]),str(ids[1] if reverse else ids[4]),str(algorithm)],capture_output=True,text=True,check=True)
                value = json.loads(result.stdout)
                actual = [edges[i]['id'] for i in value['edges']]
                if value['result'] != expected or actual != (edge_ids if expected == 'ok' else []):
                    raise AssertionError((utc,reverse,algorithm,value,actual))
                cases.append(dict(utc=utc,reverse=reverse,algorithm=algorithm,expected_result=expected,edge_ids=actual,**value))
    report = dict(scope='Actual Center production from three explicitly synthetic raw XML sources: west main road, east branch/gate, cross-source conditional relation. West alone does not split at the seam node; union before segmentation connects it. Four clocks, both directions, four integer algorithms, cold cache/process per query, every other read Busy. Host typed payload with 32 labels/output and retained ALT, excludes driver/file/allocator/task storage. No MCU or real independent geographic extract claim.',
                  manifest_sha256=sha(manifest_path), graph_sha256=sha(document_path), package_sha256=sha(package),
                  package_bytes=package.stat().st_size, probe_sha256=sha(args.probe), source_sha256=[r['sha256'] for r in [manifest['source'],*manifest['regionalSources']]],
                  nodes=len(graph['nodes']),edges=len(edges),cases=cases)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS raw Center regional seam:',len(cases),'queries')


if __name__ == '__main__':
    main()
