"""Check clipped integer legs on a Center artifact against packed sample arithmetic."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from tmap_pack import build, graph_sections


def integer_totals(sections, legs):
    totals = [0, 0, 0]
    for row, start, end in legs:
        edge = sections[1102][row * 48:(row + 1) * 48]
        if not struct.unpack_from('<H', edge, 10)[0] & 2:
            raise ValueError('Missing elevation coverage')
        length, ascent, descent = struct.unpack_from('<III', edge, 12)
        if end == 0xffffffff: end = length
        if start == 0 and end == length:
            values = [ascent, descent]
        else:
            offset, count, _ = struct.unpack_from('<QII', sections[1108], row * 16)
            samples = [struct.unpack_from('<III', sections[1109], offset + i * 12) for i in range(count)]
            positions = [s[0] for s in samples]
            def prefix(at, channel):
                if at == 0: return 0
                if at == length: return samples[-1][channel]
                i = bisect.bisect_left(positions, at)
                a, b = samples[i - 1], samples[i]
                width = b[0] - a[0]
                return a[channel] + ((b[channel] - a[channel]) * (at - a[0]) + width // 2) // width
            values = [prefix(end, c) - prefix(start, c) for c in (1, 2)]
        totals[0] += end - start
        totals[1] += values[0]
        totals[2] += values[1]
    return totals

def benchmark(artifact, probe):
    manifest = json.loads((artifact / 'manifest.json').read_text())
    doc = json.loads((artifact / 'graph.json').read_text())
    package = artifact / manifest['output']['file']
    digest = manifest['output']['sha256']
    if hashlib.sha256(package.read_bytes()).hexdigest() != digest or hashlib.sha256(build(doc)[0]).hexdigest() != digest:
        raise ValueError('Center package/document identity mismatch')
    sections = {kind: raw for kind, raw, _ in graph_sections(doc['graph'], doc['dem'])}
    cases = []
    count = len(sections[1102]) // 48
    for row in range(count):
        length = struct.unpack_from('<I', sections[1102], row * 48 + 12)[0]
        for legs in ([[row, 0, 0xffffffff]], [[row, length // 4, length * 3 // 4]],
                     [[row, 0, length // 2], [row, length // 2, length]]):
            expected = integer_totals(sections, legs)
            baseline = None
            for busy in (0, 2):
                command = [str(probe), str(package), str(busy)]
                result = json.loads(subprocess.run(command, input=''.join(f'{a} {b} {c}\n' for a, b, c in legs),
                    text=True, capture_output=True, check=True).stdout)
                actual = [result[k] for k in ('length_cm', 'ascent_cm', 'descent_cm')]
                if result['result'] != 'ok' or actual != expected:
                    raise ValueError('Integer clipped totals disagree with oracle')
                if baseline is not None and actual != baseline: raise ValueError('Busy changed totals')
                baseline = actual
                cases.append({'legs': legs, 'busy_every': busy, 'oracle_cm': expected, 'measurement': result})
    return {'package_sha256': digest, 'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
            'cases': cases, 'scope': 'Actual Center package, integer clipped/full/split edges and independent packed-profile arithmetic. Host retained library payload excludes unused probe leg slots, allocator and application overhead. Not MCU peak RAM/stack/PSRAM or surveyed elevation accuracy.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = benchmark(args.artifact.resolve(), args.probe.resolve())
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"PASS {len(report['cases'])} real integer leg cases")
