"""Measure independently Center-exported real way extracts and verified raw seam patch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from merge_regions import merge_graphs
from tmap_pack import build
from virtual_regions import prepare,binary_manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('artifacts',type=Path)
    parser.add_argument('selection',type=Path)
    parser.add_argument('probe',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    selected = json.loads(args.selection.read_text())
    documents,packages,manifests = {},{},{}
    for label in ('main','branch','union'):
        root = args.artifacts/label
        manifest = json.loads((root/'manifest.json').read_text())
        for record in [manifest['source'],*manifest.get('regionalSources',[])]:
            if sha(Path(record['path'])) != record['sha256']: raise ValueError('source identity changed')
        doc = json.loads((root/'graph.json').read_text())
        raw = (root/'navigation.tmap').read_bytes()
        if hashlib.sha256(raw).hexdigest() != manifest['output']['sha256'] or build(doc)[0] != raw:
            raise ValueError('Center package reproduction failed')
        documents[label],packages[label],manifests[label] = doc,raw,manifest
    if manifests['main']['source']['sha256'] != selected['main_source_sha256'] or manifests['branch']['source']['sha256'] != selected['branch_source_sha256']:
        raise ValueError('Center inputs differ from selected original ways')
    junction = selected['junction_node']
    if junction in {n['id'] for n in documents['main']['graph']['nodes']} or junction not in {n['id'] for n in documents['union']['graph']['nodes']}:
        raise ValueError('selection does not exercise independent segmentation')
    naive = {'name':'naive-seam','page_bytes':1024,'graph':merge_graphs([documents['main']['graph'],documents['branch']['graph']])}
    raw_groups = [[Path(manifests[k]['source']['path'])] for k in ('main','branch')]
    patch,manifest,reference = prepare([documents[k] for k in ('main','branch')],[packages[k] for k in ('main','branch')],1024,raw_source_groups=raw_groups)
    virtual = args.artifacts/'verified'
    virtual.mkdir(exist_ok=True)
    binary = binary_manifest(manifest)
    files = {'source-a.tmap':packages['main'],'source-b.tmap':packages['branch'],'patch.bin':patch,'map.bin':binary,
             'map.sha256':(hashlib.sha256(binary).hexdigest()+'\n').encode(),'extent-count.txt':(str(len(manifest['extents']))+'\n').encode(),
             'reference.tmap':reference,'naive.tmap':build(naive)[0]}
    for name,raw in files.items():
        path = virtual/name
        if path.exists() and path.read_bytes()!=raw: raise ValueError('existing benchmark artifact changed')
        if not path.exists(): path.write_bytes(raw)
    cases = []
    for label in ('naive','center_union','reference','virtual'):
        # Raw-patch rows are independently serialized; its stable node sort matches union.
        graph = naive['graph'] if label=='naive' else documents['union']['graph']
        ids = {n['id']:i for i,n in enumerate(sorted(graph['nodes'],key=lambda n:n['id']))}
        edges = sorted(graph['edges'],key=lambda e:(ids[e['from']],str(e['id'])))
        for reverse in (False,True):
            start,end = selected['start_osm_node'],selected['destination_osm_node']
            if reverse: start,end=end,start
            for algorithm in range(4):
                path = args.artifacts/'union'/'navigation.tmap' if label=='center_union' else virtual if label=='virtual' else virtual/(label+'.tmap')
                command = [str(args.probe.resolve()),str(path.resolve()),'0','0','1',str(ids[start]),str(ids[end]),str(algorithm)]
                if label=='virtual': command.append('virtual')
                value = json.loads(subprocess.run(command,capture_output=True,text=True,check=True).stdout)
                expected = 'no_path' if label=='naive' else 'ok'
                if value['result']!=expected: raise AssertionError((label,reverse,algorithm,value))
                if expected=='ok' and len(value['edges'])!=2: raise AssertionError('wrong actual seam path')
                current = start
                for row in value['edges']:
                    edge = edges[row]
                    if edge['from'] != current or not edge['access'] & 1:
                        raise AssertionError('disconnected or forbidden seam route')
                    current = edge['to']
                if expected=='ok' and current!=end: raise AssertionError('wrong seam destination')
                cases.append(dict(kind=label,reverse=reverse,algorithm=algorithm,**value))
    for reverse in (False,True):
        for algorithm in range(4):
            rows = [r for r in cases if r['reverse']==reverse and r['algorithm']==algorithm and r['kind']!='naive']
            if len({tuple(r['edges']) for r in rows})!=1: raise AssertionError('Center/reference/verified route mismatch')
    report = dict(scope='Original real Liechtenstein PBF: two explicitly selected actual OSM ways, not complete geographic extracts. Independent Center main/branch exports segment the shared node differently. Raw union Center export and verified source/patch package restore actual stable-ID connectivity. Naive graph union is a measured disconnected control. All four integer algorithms, both directions, cold cache/process and Busy every other physical read. No DEM or board measurements.',
                  selection=selected,selection_sha256=sha(args.selection),probe_sha256=sha(args.probe),
                  center_manifests={k:sha(args.artifacts/k/'manifest.json') for k in manifests},
                  center_package_sha256={k:hashlib.sha256(v).hexdigest() for k,v in packages.items()},
                  virtual_manifest=manifest,binary_manifest_sha256=hashlib.sha256(binary).hexdigest(),cases=cases,
                  tools_sha256={name:sha(Path(__file__).parent/name) for name in ('import_osm.py','merge_osm_regions.py','merge_regions.py','merge_outdoor_regions.py','virtual_regions.py','prepare_real_seam_sources.py','benchmark_real_raw_seam.py')})
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS real raw seam:',len(cases),'queries, extents',len(manifest['extents']))


if __name__ == '__main__': main()
