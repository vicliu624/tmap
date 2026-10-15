"""Record successful streaming-rule tests and their exact implementation identity."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('test',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    executable = args.test.resolve()
    result = subprocess.run([str(executable)],capture_output=True,text=True,check=True)
    sizes = re.search(r'cursor=(\d+) output=(\d+)',result.stdout)
    if not sizes or 'PASS streaming clauses' not in result.stdout: raise ValueError('missing successful evidence')
    root = Path(__file__).resolve().parents[1]
    sources = ('include/tmap/calendar_rules.h','include/tmap/road_permissions.h','include/tmap/turn_permissions.h','include/tmap/turn_rules.h','include/tmap/road_rules.h','src/turn_rules.cpp','src/road_rules.cpp','src/calendar_rules.cpp','tools/compile_calendar.py','tools/compile_road_conditions.py','tools/compile_turn_conditions.py','tests/test_calendar_rules.cpp')
    report = {'cursor_bytes':int(sizes[1]),'output_bytes':int(sizes[2]),'record_bytes':24,
              'test_output':result.stdout.strip(),'test_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),
              'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sources},
              'scope':'Actual SDK stream-unit tests, including replacement during read and publication gate. Host ABI, no physical MCU peaks or live navigation rule integration claim.'}
    permission_sizes = re.search(r'permissions=(\d+) masks=(\d+) permission_cases=(\d+)',result.stdout)
    if permission_sizes:
        report.update(road_permission_bytes=int(permission_sizes[1]),road_mask_bytes=int(permission_sizes[2]),road_permission_cases=int(permission_sizes[3]))
    road_sizes = re.search(r'ROAD cursor=(\d+) masks=(\d+)',result.stdout)
    if road_sizes: report.update(road_rule_cursor_bytes=int(road_sizes[1]),road_rule_output_bytes=int(road_sizes[2]))
    turn_sizes = re.search(r'TURN cursor=(\d+) output=(\d+)',result.stdout)
    if turn_sizes: report.update(turn_rule_cursor_bytes=int(turn_sizes[1]),turn_rule_output_bytes=int(turn_sizes[2]))
    decision = re.search(r'TURN_DECISION cursor=(\d+) cases=(\d+)',result.stdout)
    if decision: report.update(turn_decision_cursor_bytes=int(decision[1]),turn_decision_cases=int(decision[2]))
    args.output.write_text(json.dumps(report,indent=2)+'\n')
