"""Compare real packed administrative queries with GEOS E7 component geometry."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from tmap_pack import point


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--geometry-python-path', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.geometry_python_path))
    import shapely
    from shapely.geometry import Polygon, Point
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = json.loads((args.artifact / 'manifest.json').read_text())
    package = args.artifact / manifest['output']['file']
    if sha(package) != manifest['output']['sha256']:
        raise ValueError('package identity mismatch')
    document = args.artifact / 'graph.json'
    doc = json.loads(document.read_text(encoding='utf-8'))
    features = []
    for feature in doc['administration']:
        rings = [[(point(p)[1], point(p)[0]) for p in ring] for ring in feature['rings']]
        features.append((feature, Polygon(rings[0], rings[1:])))
    cases = []
    for component, (feature, geometry) in enumerate(features):
        interior = geometry.representative_point()
        vertex = tuple(map(int, geometry.exterior.coords[0]))
        locations = [('interior', (round(interior.x), round(interior.y))), ('vertex', vertex)]
        locations += [(label, (vertex[0] + dx, vertex[1] + dy)) for label, dx, dy in
                      [('east_e7', 1, 0), ('west_e7', -1, 0), ('north_e7', 0, 1), ('south_e7', 0, -1)]]
        for kind, location in locations:
            query = Point(*location)
            matches = [(f, g) for f, g in features if g.covers(query)]
            depth = max((len(f['path']) for f, _ in matches), default=0)
            matches = [(f, g) for f, g in matches if len(f['path']) == depth]
            path = list(matches[0][0]['path']) if matches else []
            for f, _ in matches[1:]:
                path = path[:next((i for i, (a, b) in enumerate(zip(path, f['path'])) if a != b),
                                  min(len(path), len(f['path'])))]
            expected = ' / '.join(path)
            boundary = any(g.boundary.covers(query) for _, g in matches)
            command = [str(args.probe.resolve()), str(package.resolve()), 'region',
                       f'{location[0] / 1e7:.7f}', f'{location[1] / 1e7:.7f}']
            run = subprocess.run(command, capture_output=True, text=True)
            parsed = re.fullmatch(r'region=(.*?) depth=(\d+) approximate=0 boundary=(\d+) result=(\S+) reads=(\d+) bytes=(\d+) operations=(\d+)\s*', run.stdout)
            if not parsed:
                raise AssertionError(f'Unexpected probe output: {run.stdout!r} {run.stderr!r}')
            actual, actual_depth, actual_boundary, result, reads, transferred, operations = parsed.groups()
            if actual != expected or int(actual_depth) != len(path) or bool(int(actual_boundary)) != boundary:
                raise AssertionError(f'GEOS mismatch at component {component} {kind}: {parsed.groups()} expected {expected!r}, {boundary}')
            if result != ('ok' if matches else 'missing') or run.returncode != (0 if matches else 1):
                raise AssertionError(f'Unexpected result {result}: {run.stderr}')
            cases.append(dict(component=component, relation=feature['osm_relation'], kind=kind,
                              longitude_e7=location[0], latitude_e7=location[1], path=actual,
                              deepest_matches=len(matches),
                              depth=int(actual_depth), boundary=boundary, result=result,
                              reads=int(reads), transferred_bytes=int(transferred), operations=int(operations)))
    root = Path(__file__).resolve().parents[1]
    sources = ['src/administration.cpp', 'src/binary.h', 'include/tmap/administration.h',
               'examples/inspect.cpp', 'tools/check_real_administrative_boundaries.py']
    report = dict(scope='All real imported polygon components: one representative interior, first outer vertex '
                        'and four one-E7 offsets per component; GEOS membership/common hierarchy/boundary oracle. '
                        'Cold process per query. No Busy injection here; no surveyed boundary accuracy claim.',
                  package_sha256=sha(package), document_sha256=sha(document), probe_sha256=sha(args.probe),
                  geometry_engine=dict(shapely=shapely.__version__, geos=shapely.geos_version_string),
                  source_sha256={name: sha(root / name) for name in sources}, components=len(features),
                  outcomes=dict(Counter(c['result'] for c in cases)),
                  boundary_cases=sum(c['boundary'] for c in cases), cases=cases)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('components', 'outcomes', 'boundary_cases')}))


if __name__ == '__main__':
    main()
