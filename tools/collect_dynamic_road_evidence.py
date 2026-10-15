"""Record paged runtime-road tests; no physical hardware claims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('test',type=Path)
    parser.add_argument('fixtures',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    result = subprocess.run([str(args.test.resolve()),str(args.fixtures.resolve())],capture_output=True,text=True,check=True)
    routes = re.search(r'DYNAMIC routes=(\d+) snaps=(\d+)',result.stdout)
    size = re.search(r'EDGE rule_reader=(\d+)',result.stdout)
    graph = re.search(r'GRAPH reader=(\d+) info=(\d+)',result.stdout)
    coordinate = re.search(r'COORDINATE cases=(\d+)',result.stdout)
    barrier = re.search(r'BARRIER cases=(\d+) reader=(\d+)',result.stdout)
    invalid_barriers = re.search(r'BARRIER invalid=(\d+)',result.stdout)
    turn_paged = re.search(r'TURN_PAGED cursor=(\d+) ranges=(\d+)',result.stdout)
    turn_context = re.search(r'TURN_CONTEXT cases=(\d+) reader=(\d+)',result.stdout)
    turn_grouped = re.search(r'TURN_GROUPED cases=(\d+) invalid=(\d+)',result.stdout)
    turn_routes = re.search(r'TURN_ROUTES cases=(\d+) binding=(\d+)',result.stdout)
    turn_clipped = re.search(r'TURN_CLIPPED cases=(\d+)',result.stdout)
    grouped_routes = re.search(r'TURN_GROUPED_ROUTES cases=(\d+)',result.stdout)
    combined = re.search(r'COMBINED_RULES cases=(\d+)',result.stdout)
    if not routes or not size or not graph or 'PASS bounded open' not in result.stdout: raise ValueError('missing successful evidence')
    root = Path(__file__).resolve().parents[1]
    sources = ('src/navigation.cpp','src/edge_rules.cpp','include/tmap/navigation.h','include/tmap/edge_rules.h',
               'src/road_rules.cpp','src/calendar_rules.cpp','src/section_file.cpp','tools/tmap_pack.py',
               'tools/road_rule_sections.py','tools/compile_road_conditions.py','tools/import_osm.py',
               'include/tmap/routing_rules.h','src/route_history.cpp','include/tmap/turn_context.h','src/turn_context.cpp','tools/compile_barrier_conditions.py','src/integer_route.cpp','include/tmap/integer_route.h','tests/test_package.cpp','tests/make_fixtures.py')
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = {'routes':int(routes[1]),'snaps':int(routes[2]),'edge_reader_bytes':int(size[1]),
              'graph_reader_bytes':int(graph[1]),'graph_info_bytes':int(graph[2]),
              'test_sha256':sha(args.test),'stdout':result.stdout,
              'source_sha256':{name:sha(root/name) for name in sources},
              'coordinate_cases':int(coordinate[1]) if coordinate else 0,
              'barrier_cases':int(barrier[1]) if barrier else 0,
              'invalid_barrier_cases':int(invalid_barriers[1]) if invalid_barriers else 0,
              'turn_program_cursor_bytes':int(turn_paged[1]) if turn_paged else 0,
              'turn_program_ranges_bytes':int(turn_paged[2]) if turn_paged else 0,
              'turn_context_cases':int(turn_context[1]) if turn_context else 0,
              'turn_context_reader_bytes':int(turn_context[2]) if turn_context else 0,
              'turn_grouped_cases':int(turn_grouped[1]) if turn_grouped else 0,
              'invalid_turn_context_cases':int(turn_grouped[2]) if turn_grouped else 0,
              'turn_route_cases':int(turn_routes[1]) if turn_routes else 0,
              'runtime_binding_bytes':int(turn_routes[2]) if turn_routes else 0,
              'turn_clipped_cases':int(turn_clipped[1]) if turn_clipped else 0,
              'turn_grouped_route_cases':int(grouped_routes[1]) if grouped_routes else 0,
              'combined_rule_cases':int(combined[1]) if combined else 0,
              'fixture_sha256':{path.name:sha(path) for path in sorted(args.fixtures.glob('runtime-*.tmap'))},
              'scope':'Host ABI, synthetic paged fixtures, readAt Busy every second attempt, cache reused across cases. Road/barrier cases plus 32 basic turn node routes, 128 grouped turn node routes and 96 clipped turn routes across all float/integer algorithms. No physical MCU or real-map condition claim.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
