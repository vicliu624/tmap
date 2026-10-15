"""Cold runtime-rule queries against an actual Center-produced synthetic artifact."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('artifact',type=Path)
    parser.add_argument('probe',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--combined-turns',action='store_true',help='four-node Center road/barrier/via-way synthetic scenario')
    args = parser.parse_args()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest_path = args.artifact/'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    package = args.artifact/'navigation.tmap'
    if not manifest.get('RuntimeConditions') or sha(package) != manifest['output']['sha256']:
        raise ValueError('wrong runtime policy or package identity')
    source = Path(manifest['source']['path'])
    if sha(source) != manifest['source']['sha256']: raise ValueError('source identity changed')
    rows = []
    for utc in ((345600,432000,518400,0) if args.combined_turns else (345600,432000,0)):
        for mode in (1,2):
            for reverse in ((False,) if args.combined_turns else (False,True)):
                expected = ('no_path' if (mode==1 and utc in (345600,432000)) or (mode==2 and utc==518400) else 'ok') if args.combined_turns else (
                    'no_path' if (utc == 432000 and mode == 1) or (utc != 345600 and reverse) else 'ok')
                for algorithm in range(4):
                    result = subprocess.run([str(args.probe.resolve()),str(package.resolve()),str(utc),'0',str(mode),
                                             '2' if reverse else '0','3' if args.combined_turns else '0' if reverse else '2',str(algorithm)],
                                            capture_output=True,text=True,check=True)
                    value = json.loads(result.stdout)
                    if value['result'] != expected: raise AssertionError((utc,mode,reverse,algorithm,value))
                    expected_edges = ([0,2,3] if args.combined_turns else [3,1] if reverse else [0,2]) if expected == 'ok' else []
                    if value['edges'] != expected_edges: raise AssertionError('directed route output mismatch')
                    rows.append(dict(utc=utc,mode=mode,reverse=reverse,algorithm=algorithm,**value))
    report = {'scope':'Actual Center CLI production from explicitly synthetic OSM XML. Each query starts a new probe/package/cache, Busy every other readAt. Integer algorithms and node routes; no physical MCU or real regional conditions.',
              'manifest_sha256':sha(manifest_path),'package_sha256':sha(package),'package_bytes':package.stat().st_size,
              'source_sha256':sha(source),'probe_sha256':sha(args.probe),'cases':rows}
    report['combined_turns'] = args.combined_turns
    args.output.write_text(json.dumps(report,indent=2)+'\n')
