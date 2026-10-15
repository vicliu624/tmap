"""Partition a verified Center mountain artifact and measure verified virtual routing."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
from tmap_pack import build, e7, point, graph_sections
from virtual_regions import prepare, binary_manifest
from merge_outdoor_regions import merge_outdoor_documents
from benchmark_integer_leg_elevation import integer_totals
from benchmark_integer_coordinate_route import maneuver_oracle


def benchmark(artifact, directory, probe):
    manifest = json.loads((artifact/'manifest.json').read_text())
    doc = json.loads((artifact/'graph.json').read_text())
    original = artifact/manifest['output']['file']
    digest = manifest['output']['sha256']
    if hashlib.sha256(original.read_bytes()).hexdigest() != digest or hashlib.sha256(build(doc)[0]).hexdigest() != digest:
        raise ValueError('Center package/document identity mismatch')
    if directory.exists(): raise ValueError('benchmark output directory must be new')
    dem, graph = doc['dem'], doc['graph']
    width = len(dem['heights'][0])
    if width < 5 or len(graph['edges']) < 4: raise ValueError('insufficient regional input')
    ordered = sorted(graph['edges'],key=lambda e:(sum(p[0] for p in e['points'])/len(e['points']),str(e['id'])))
    halves = [ordered[:len(ordered)//2],ordered[len(ordered)//2:]]
    cut = width//2
    regions = []
    for index, edges in enumerate(halves):
        region = copy.deepcopy(doc)
        region.pop('contours',None)
        region.pop('bounds',None)
        region['name'] = f'real-region-{index}'
        region['page_bytes'] = 1024
        used = {int(e[k]) for e in edges for k in ('from','to')}
        region['graph']['nodes'] = [copy.deepcopy(n) for n in graph['nodes'] if int(n['id']) in used]
        region['graph']['edges'] = copy.deepcopy(edges)
        edge_ids = {str(e['id']) for e in edges}
        region['graph']['restrictions'] = [r for r in graph.get('restrictions',[]) if str(r['from']) in edge_ids and str(r['to']) in edge_ids]
        if graph.get('restrictions'): raise ValueError('benchmark partition requires restriction-free input to avoid dropping cross-region operands')
        region['graph']['landmarks'] = min(int(graph.get('landmarks',0)),len(used))
        if index == 0: region['dem']['heights'] = [r[:cut+1] for r in dem['heights']]
        else:
            region['dem']['heights'] = [r[cut:] for r in dem['heights']]
            region['dem']['origin'][0] = (e7(dem['origin'][0])+cut*e7(dem['step'][0]))/1e7
        region['dem']['source'] = str(dem.get('source','unspecified'))+f':column-partition:{index}:{cut}'
        regions.append(region)
    packages = [build(r)[0] for r in regions]
    patch, virtual_manifest, raw = prepare(regions,packages,1024)
    merged = merge_outdoor_documents(regions,1024)
    if build(merged)[0] != raw: raise ValueError('virtual reference mismatch')
    directory.mkdir(parents=True)
    for index,name in enumerate(('source-a.tmap','source-b.tmap')):
        (directory/name).write_bytes(packages[index])
        (directory/f'region-{index}.json').write_text(json.dumps(regions[index]),encoding='utf-8')
    (directory/'patch.bin').write_bytes(patch)
    (directory/'reference.tmap').write_bytes(raw)
    binary = binary_manifest(virtual_manifest)
    (directory/'map.bin').write_bytes(binary)
    (directory/'map.sha256').write_text(hashlib.sha256(binary).hexdigest()+'\n')
    (directory/'extent-count.txt').write_text(str(virtual_manifest['extent_count'])+'\n')
    (directory/'manifest.json').write_text(json.dumps(virtual_manifest,indent=2)+'\n')
    files = packages+[patch]
    reconstructed = b''.join(files[e['source']][e['source_offset']:e['source_offset']+e['bytes']] for e in virtual_manifest['extents'])
    if reconstructed != raw: raise ValueError('cross-file reconstruction mismatch')
    sections = {kind:data for kind,data,_ in graph_sections(merged['graph'],merged['dem'])}
    paths = [e['points'] for e in graph['edges'] if len(e['points']) >= 4]
    a,b = paths[0][len(paths[0])//4],paths[2][len(paths[2])*3//4]
    cases = []
    for start,end in ((a,b),(b,a)):
        for busy in (0,2):
            results = []
            for virtual in (False,True):
                command = [str(probe),str(directory if virtual else directory/'reference.tmap'),str(busy),'5','heap']
                if virtual: command.append('virtual')
                result = json.loads(subprocess.run(command,input=' '.join(map(str,(*point(start),*point(end))))+'\n',text=True,capture_output=True,check=True).stdout)
                expected = integer_totals(sections,result['legs'])
                if result['result'] != 'ok' or result['statistics_result'] != 'ok' or result['maneuver_result'] != 'ok': raise ValueError('real virtual pipeline failed')
                if [result[k] for k in ('length_cm','ascent_cm','descent_cm')] != expected or result['cost_units'] != expected[0]+5*expected[1]:
                    raise ValueError('real virtual totals disagree with oracle')
                instructions = maneuver_oracle(sections,result['legs'])
                if len(instructions) != len(result['maneuvers']): raise ValueError('maneuver count mismatch')
                results.append(result)
            if any(results[0][k] != results[1][k] for k in ('legs','cost_units','length_cm','ascent_cm','descent_cm','maneuvers')):
                raise ValueError('verified virtual view changed route output')
            cases.append({'start':start,'destination':end,'busy_every':busy,'reference':results[0],'verified_virtual':results[1]})
    return {'center_package_sha256':digest,'virtual_manifest':virtual_manifest,'binary_manifest_sha256':hashlib.sha256(binary).hexdigest(),
            'probe_sha256':hashlib.sha256(probe.read_bytes()).hexdigest(),'cases':cases,
            'scope':'Real Center OSM/HGT artifact partitioned into two overlapping terrain column crops and directed-edge groups by the SDK benchmark, not two independent Center exports. Actual verified multi-file/source/patch/virtual SHA then coordinate route/statistics/maneuvers. Host retained payload/physical reads, not MCU peaks or survey accuracy. Original input restriction-free; partial regional terrain is completed by the mosaic.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact',type=Path)
    parser.add_argument('directory',type=Path)
    parser.add_argument('probe',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    report = benchmark(args.artifact.resolve(),args.directory.resolve(),args.probe.resolve())
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS four real verified-regional cases and four reference comparisons')
