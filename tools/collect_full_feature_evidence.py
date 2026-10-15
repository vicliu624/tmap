"""Capture joint-service and explicit directory-capacity evidence, not board peaks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('artifact', type=Path)
    parser.add_argument('builds', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest_path = args.artifact/'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    for record in (manifest['source'], manifest['outdoorSource']):
        if sha(Path(record['path'])) != record['sha256']:
            raise ValueError('source changed')
    package = args.artifact/manifest['output']['file']
    if sha(package) != manifest['output']['sha256']:
        raise ValueError('package changed')
    section_report = json.loads((args.artifact/'sections.json').read_text())
    if len(section_report['sections']) != 37 or section_report['capabilities'] != 15:
        raise ValueError('wrong joint feature scope')
    profiles = []
    for build in args.builds:
        executable = build/'tmap_full_feature_tests.exe'
        config = {}
        for line in (build/'CMakeCache.txt').read_text().splitlines():
            if line.startswith(('TMAP_CACHE_PAGE_BYTES:', 'TMAP_PACKAGE_CACHE_PAGES:', 'TMAP_PACKAGE_SECTION_SLOTS:')):
                key, value = line.split('=', 1)
                config[key.split(':')[0]] = int(value)
        outputs = {}
        for label, command in [('synthetic_sdk', [str(build/'fixtures')]), ('synthetic_center', [str(args.artifact), 'center'])]:
            result = subprocess.run([str(executable.resolve()), *command], capture_output=True, text=True, check=True)
            outputs[label] = result.stdout
        profiles.append(dict(configuration=config, probe_sha256=sha(executable), outputs=outputs))
    root = Path(__file__).resolve().parents[1]
    sources = ['tests/test_full_features.cpp', 'tests/make_fixtures.py', 'tools/tmap_pack.py',
               'include/tmap/package.h', 'src/package.cpp', 'src/resource_budget.cpp', 'tools/collect_full_feature_evidence.py']
    report = dict(scope='Explicitly synthetic SDK and Center joint-feature packages. Serialized round-robin integer ALT node route, road/barrier/turn readers, partial-edge integer elevation, live contour renderer and polygon administration share one cache. Busy every other file read. 37 real feature sections; padded valid 64-section and CRC-valid malformed 65-count fixtures test boundaries. Typed payload includes configured service controls/buffers and two separately budgeted permission outputs, excludes host driver/file/allocator/task storage. No physical board or real combined geographic dataset claim.',
                  center_manifest_sha256=sha(manifest_path), package_sha256=sha(package), package_bytes=package.stat().st_size,
                  source_sha256=manifest['source']['sha256'], outdoor_sha256=manifest['outdoorSource']['sha256'],
                  section_types=list(section_report['sections']), sources={p:sha(root/p) for p in sources}, profiles=profiles)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS full feature evidence:', len(profiles), 'profiles')


if __name__ == '__main__':
    main()
