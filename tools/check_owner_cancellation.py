"""Record bounded owner cancellation/restart checks; no physical PSRAM claim."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    reports = []
    for name, extra in [('tmap_route_storage_tests', []),
                        ('tmap_session_tests', [str(args.build / 'fixtures')])]:
        executable = args.build / (name + '.exe')
        if not executable.exists():
            executable = args.build / name
        run = subprocess.run([str(executable.resolve()), *extra], capture_output=True, text=True, check=True)
        reports.append(dict(test=name, executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                            output=run.stdout))
    sources = ['tests/test_route_storage.cpp', 'tests/test_session.cpp',
               'include/tmap/memory.h', 'include/tmap/staged_memory.h',
               'include/tmap/memory_object.h', 'include/tmap/route_storage.h',
               'include/tmap/outdoor_session.h', 'src/outdoor_session.cpp',
               'tools/check_owner_cancellation.py']
    report = dict(scope='Every one-work-unit initialization prefix through completion, cancellation, '
                        'zero live allocations, restart on the same owner; fake external-only provider. '
                        'No MCU execution, physical PSRAM allocation/peak or allocator-latency claim.',
                  tests=reports,
                  source_sha256={name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources})
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS owner cancellation evidence:', args.output)


if __name__ == '__main__':
    main()
