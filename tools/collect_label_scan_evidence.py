"""Check exact outcomes/resource savings against queued priority routing reports."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    docs = root/'docs'
    names = [f'real-{region}-label-scan-{profile}-evidence.json' for region in ('monaco','everest','liechtenstein') for profile in ('native','small')]
    reports = {name:json.loads((docs/name).read_text()) for name in names}
    monaco = reports['real-monaco-label-scan-native-evidence.json']['results']
    other = reports['real-monaco-label-scan-small-evidence.json']['results']
    key = lambda r:(r['query'],r['capacity'],r['algorithm'])
    indexed = {key(r):r for r in monaco}
    compared_fields = ('result','cost_cm','edges','states','expansions')
    for row in other:
        if any(row[f]!=indexed[key(row)][f] for f in compared_fields):
            raise AssertionError('Monaco Busy/cache changed exact route outcomes')
    comparisons = 0
    for row in monaco:
        if row['algorithm']!='integer-label-scan': continue
        baseline = indexed[(row['query'],row['capacity'],'integer-dijkstra')]
        if any(row[f]!=baseline[f] for f in compared_fields):
            raise AssertionError('queue-free and queued exact order diverged')
        if baseline['reserved_library_payload_bytes']-row['reserved_library_payload_bytes']!=row['capacity']*4:
            raise AssertionError('queue array not actually removed')
        comparisons += 1
    country_comparisons = 0
    for profile in ('native','small'):
        cases = reports[f'real-liechtenstein-label-scan-{profile}-evidence.json']['routes']
        baseline = {(r['local_instant'],r['selected_edge_id']):r for r in cases if r['algorithm']==1}
        for row in cases:
            if row['algorithm']!=4: continue
            original = baseline[(row['local_instant'],row['selected_edge_id'])]
            if any(row[f]!=original[f] for f in ('result','edges','cost_units','states','expansions')):
                raise AssertionError('country conditional route changed')
            country_comparisons += 1
    commands = {
        'independent_reference': [str(args.build/'tmap_integer_route_tests.exe'),str(args.build/'stress'),str(args.build/'fixtures')],
        'combined_rules': [str(args.build/'tmap_package_tests.exe'),str(args.build/'fixtures')],
        'psram_ownership': [str(args.build/'tmap_route_storage_tests.exe')],
        'resource_budget': [str(args.build/'tmap_resource_tests.exe')],
    }
    outputs = {name:subprocess.run(command,capture_output=True,text=True,check=True).stdout for name,command in commands.items()}
    if 'LABEL_SCAN_COMBINED cases=16' not in outputs['combined_rules'] or 'external_allocations=2 rollback_points=2' not in outputs['psram_ownership']:
        raise AssertionError('new integration/ownership checks did not execute')
    source_names = ['include/tmap/integer_route.h','include/tmap/route_storage.h','include/tmap/resource_budget.h',
                    'src/integer_route.cpp','src/integer_coordinate_route.cpp','src/integer_prepared_route.cpp','src/resource_budget.cpp',
                    'tests/test_integer_route.cpp','tests/test_package.cpp','tests/test_route_storage.cpp','tests/test_resource_budget.cpp',
                    'examples/navigation_resource_probe.cpp','examples/runtime_navigation_probe.cpp','examples/integer_coordinate_resource_probe.cpp',
                    'tools/collect_label_scan_evidence.py']
    report = dict(scope='New optional integer label-scan Dijkstra, no retained queue array. Actual host payload and deterministic work/read counts, not MCU peaks/timing/energy. Monaco exact queued-order comparison at identical capacities, both cache/retry profiles; real conditional country and mountain/clipped route statistics/maneuvers. Host PSRAM provider is a policy/failure simulation, not a physical PSRAM measurement.',
        monaco_queue_free_comparisons=comparisons,monaco_cache_busy_comparisons=len(other),country_queue_free_comparisons=country_comparisons,
        input_reports={name:sha(docs/name) for name in names},sources={name:sha(root/name) for name in source_names},
        test_executables={name:sha(Path(command[0])) for name,command in commands.items()},test_outputs=outputs)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS label-scan exact/resource/PSRAM integration evidence')


if __name__=='__main__': main()
