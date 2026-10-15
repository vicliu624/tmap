"""Remeasure immutable verified regional artifacts with another SDK cache build."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from tmap_pack import point


def measure(directory, baseline_path, probe):
    baseline_bytes = baseline_path.read_bytes()
    baseline = json.loads(baseline_bytes)
    manifest = baseline['virtual_manifest']
    names = ('source-a.tmap','source-b.tmap','patch.bin')
    files = []
    for name, expected in zip(names,manifest['sources']):
        raw = (directory/name).read_bytes()
        if len(raw) != expected['bytes'] or hashlib.sha256(raw).hexdigest() != expected['sha256']:
            raise ValueError('regional source changed since production evidence')
        files.append(raw)
    raw = (directory/'reference.tmap').read_bytes()
    if hashlib.sha256(raw).hexdigest() != manifest['virtual_sha256']: raise ValueError('reference changed')
    if hashlib.sha256((directory/'map.bin').read_bytes()).hexdigest() != baseline['binary_manifest_sha256']:
        raise ValueError('binary manifest changed')
    if b''.join(files[e['source']][e['source_offset']:e['source_offset']+e['bytes']] for e in manifest['extents']) != raw:
        raise ValueError('virtual extents do not reproduce reference')
    cases = []
    for case in baseline['cases']:
        measured = []
        for virtual in (False,True):
            command = [str(probe),str(directory if virtual else directory/'reference.tmap'),str(case['busy_every']),'5','heap']
            if virtual: command.append('virtual')
            row = json.loads(subprocess.run(command,input=' '.join(map(str,(*point(case['start']),*point(case['destination']))))+'\n',
                text=True,capture_output=True,check=True).stdout)
            if any(row[k] != case['reference'][k] for k in ('result','statistics_result','maneuver_result','legs','cost_units','length_cm','ascent_cm','descent_cm','maneuvers')):
                raise ValueError('new cache build changed verified route output')
            measured.append(row)
        cases.append({'start':case['start'],'destination':case['destination'],'busy_every':case['busy_every'],
                      'reference':measured[0],'verified_virtual':measured[1]})
    return {'baseline_report_sha256':hashlib.sha256(baseline_bytes).hexdigest(), 'virtual_manifest':manifest,
            'probe_sha256':hashlib.sha256(probe.read_bytes()).hexdigest(),'cases':cases,
            'scope':'Actual SDK remeasurement of immutable real regional source/patch/reference/manifest identities; output compared with the independently checked production baseline. Includes all verification I/O and retained virtual controls/arrays. Host ABI and cold open per case, not physical MCU peaks, storage timing or survey accuracy.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('probe',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    report = measure(args.directory.resolve(),args.baseline.resolve(),args.probe.resolve())
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS four verified-regional remeasurements and four reference comparisons')
