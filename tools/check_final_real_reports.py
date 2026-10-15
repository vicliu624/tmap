"""Reconcile current real-map cache-profile results and preserve source identities."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    docs = root / 'docs'
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    inputs = {}
    def load(name):
        path = docs / name
        inputs[name] = sha(path)
        return json.loads(path.read_text(encoding='utf-8'))
    mountain = [load(f'final-everest-coordinate-{p}-evidence.json') for p in ('native', 'small')]
    country = [load(f'final-real-runtime-{p}-evidence.json') for p in ('native', 'small')]
    if any(len(r['cases']) != 40 for r in mountain):
        raise AssertionError('missing mountain algorithm/direction/penalty/Busy cases')
    for a, b in zip(*(r['cases'] for r in mountain)):
        if any(a[k] != b[k] for k in ('start', 'destination', 'penalty', 'algorithm', 'busy_every', 'oracle_cm', 'maneuver_oracle')):
            raise AssertionError('mountain test identity differs')
        if any(a['measurement'][k] != b['measurement'][k] for k in
               ('result', 'statistics_result', 'cost_units', 'length_cm', 'ascent_cm', 'descent_cm', 'legs', 'maneuvers')):
            raise AssertionError('mountain cache profile changes route/statistics')
    if any(len(r['cases']) != 144 or len(r['routes']) != 400 for r in country):
        raise AssertionError('missing country permission/route cases')
    for a, b in zip(*(r['routes'] for r in country)):
        if any(a[k] != b[k] for k in ('local_instant', 'selected_edge_id', 'algorithm', 'result', 'edges', 'cost_units', 'states', 'expansions')):
            raise AssertionError('country cache profile changes route outcome')
    for a, b in zip(*(r['cases'] for r in country)):
        if any(a[k] != b[k] for k in ('local_instant', 'edge_id', 'expected_access', 'result', 'access')):
            raise AssertionError('country cache profile changes permission')
    for name in ('world', 'china'):
        if not load(f'final-baseline-{name}.json')['identical']:
            raise AssertionError('legacy output changed')
    source_files = sorted([*root.glob('src/*.cpp'), *root.glob('src/*.h'), *root.glob('include/tmap/*.h')])
    report = dict(scope='Latest real mountain and conditional-country probe output agreement across default/compact '
                        'profiles, plus exact legacy comparison. Individual reports retain their oracle limitations. '
                        'No physical MCU peak/timing evidence.',
                  mountain_cases_per_profile=40, country_permission_cases_per_profile=144,
                  country_route_cases_per_profile=400, country_results=country[0]['route_results'],
                  legacy_comparisons=2, input_sha256=inputs,
                  core_source_sha256={str(p.relative_to(root)).replace('\\', '/'): sha(p) for p in source_files})
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS final real-profile reconciliation')


if __name__ == '__main__':
    main()
