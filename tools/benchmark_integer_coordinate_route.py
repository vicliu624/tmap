"""Validate integer coordinate navigation on an actual Center mountain package."""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import subprocess
import struct
from tmap_pack import build, graph_sections, point, distance
from benchmark_integer_leg_elevation import integer_totals


def maneuver_oracle(sections, legs):
    events, previous, arrival, accumulated = [], None, None, 0
    for row, start, end in legs:
        edge = sections[1102][row * 48:(row + 1) * 48]
        length = struct.unpack_from('<I', edge, 12)[0]
        if end == 0xffffffff: end = length
        offset, count = struct.unpack_from('<QI', edge, 24)
        points = [struct.unpack_from('<ii', sections[1103], offset + i * 8) for i in range(count)]
        if struct.unpack_from('<H', edge, 10)[0] & 1: points.reverse()
        cumulative = [0.]
        for a, b in zip(points, points[1:]): cumulative.append(cumulative[-1] + distance(a, b))
        def locate(cm):
            target = cumulative[-1] * cm / length
            upper = max(1, min(count - 1, bisect.bisect_left(cumulative, target)))
            a, b = points[upper - 1], points[upper]
            width = cumulative[upper] - cumulative[upper - 1]
            fraction = (target - cumulative[upper - 1]) / width if width else 0
            p = [math.floor(x + (y - x) * fraction + .5) for x, y in zip(a, b)]
            return p, (a, b)
        departure, heading = locate(start)
        arrival, incoming = locate(end)
        kind = 0
        if previous is not None:
            scale = math.cos(heading[0][0] / 1e7 * math.pi / 180)
            def bearing(pair):
                a, b = pair
                return math.atan2((b[1] - a[1]) * scale, b[0] - a[0])
            angle = math.remainder((bearing(heading) - bearing(previous)) * 180 / math.pi, 360)
            kind = 4 if abs(angle) > 150 else 3 if angle > 30 else 2 if angle < -30 else 1
        events.append({'kind': kind, 'point': departure, 'distance_m': accumulated / 100,
                       'name_id': struct.unpack_from('<Q', edge, 40)[0]})
        previous = incoming
        accumulated += end - start
    if arrival is not None: events.append({'kind': 5, 'point': arrival, 'distance_m': accumulated / 100, 'name_id': 0})
    return events


def benchmark(artifact, probe, algorithms=None):
    manifest = json.loads((artifact / 'manifest.json').read_text())
    doc = json.loads((artifact / 'graph.json').read_text())
    package = artifact / manifest['output']['file']
    digest = manifest['output']['sha256']
    if hashlib.sha256(package.read_bytes()).hexdigest() != digest or hashlib.sha256(build(doc)[0]).hexdigest() != digest:
        raise ValueError('Center package/document identity mismatch')
    sections = {kind: raw for kind, raw, _ in graph_sections(doc['graph'], doc['dem'])}
    paths = [e['points'] for e in doc['graph']['edges'] if len(e.get('points', [])) >= 4]
    a = paths[0][len(paths[0]) // 4]
    b = paths[2][len(paths[2]) * 3 // 4]
    cases = []
    for start, end in ((a, b), (b, a)):
        for penalty in (0, 5):
            baseline = None
            for algorithm in (algorithms or ('fifo', 'linear', 'heap', 'alt')):
                for busy in (0, 2):
                    command = [str(probe), str(package), str(busy), str(penalty), algorithm]
                    row = json.loads(subprocess.run(command, input=' '.join(map(str, (*point(start), *point(end)))) + '\n',
                        text=True, capture_output=True, check=True).stdout)
                    expected = integer_totals(sections, row['legs'])
                    actual = [row[k] for k in ('length_cm', 'ascent_cm', 'descent_cm')]
                    if row['result'] != 'ok' or row['statistics_result'] != 'ok' or actual != expected:
                        raise ValueError('Integer coordinate statistics disagree with packed oracle')
                    if row['cost_units'] != expected[0] + penalty * expected[1]:
                        raise ValueError('Route cost disagrees with integer statistics')
                    instructions = maneuver_oracle(sections, row['legs'])
                    if row['maneuver_result'] != 'ok' or len(row['maneuvers']) != len(instructions):
                        raise ValueError('Missing or duplicated maneuver events')
                    for actual_event, expected_event in zip(row['maneuvers'], instructions):
                        if actual_event['kind'] != expected_event['kind'] or actual_event['name_id'] != expected_event['name_id']:
                            raise ValueError('Maneuver kind/name disagrees with oracle')
                        if distance(actual_event['point'], expected_event['point']) > .05 or abs(actual_event['distance_m'] - expected_event['distance_m']) > 1e-8:
                            raise ValueError('Maneuver position/distance disagrees with oracle')
                    if baseline is not None and row['cost_units'] != baseline:
                        raise ValueError('Algorithm or Busy changed minimum route cost')
                    baseline = row['cost_units']
                    cases.append({'start': start, 'destination': end, 'penalty': penalty,
                                  'algorithm': algorithm, 'busy_every': busy, 'oracle_cm': expected, 'maneuver_oracle': instructions, 'measurement': row})
    return {'package_sha256': digest, 'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
            'cases': cases, 'scope': 'Real Center package and integer coordinate snap/endpoint/search/clipped statistics. Independent packed-profile arithmetic; cross-algorithm cost equality is not an independent optimality proof. Fixed 128 states/legs and host ABI. No physical MCU peak, allocator/application overhead or surveyed accuracy claim.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--algorithms', nargs='+', choices=('fifo','linear','heap','alt','label-scan'))
    args = parser.parse_args()
    report = benchmark(args.artifact.resolve(), args.probe.resolve(), args.algorithms)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"PASS {len(report['cases'])} real integer coordinate cases")
