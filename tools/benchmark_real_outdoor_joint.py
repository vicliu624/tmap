"""Actual Center real terrain/admin/rules package, joint SDK query oracles."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from tmap_pack import point,elevation_at,crc32c
from benchmark_integer_leg_elevation import integer_totals


def sections(raw):
    header=bytearray(raw[:256]); expected=struct.unpack_from('<I',header,160)[0]
    struct.pack_into('<I',header,160,0)
    if crc32c(header)!=expected: raise ValueError('package header CRC')
    page=struct.unpack_from('<I',raw,20)[0]
    directory=struct.unpack_from('<Q',raw,40)[0]; count=struct.unpack_from('<I',raw,48)[0]
    table=raw[directory:directory+count*64]
    if crc32c(table)!=struct.unpack_from('<I',raw,164)[0]: raise ValueError('directory CRC')
    result={}
    for row in range(count):
        entry=row*64; kind=struct.unpack_from('<I',table,entry)[0]
        offset,length=struct.unpack_from('<QQ',table,entry+16)
        logical=struct.unpack_from('<Q',table,entry+48)[0]
        payload=[]
        for number,at in enumerate(range(offset,offset+length,page)):
            data=bytearray(raw[at:at+page]); checksum=struct.unpack_from('<I',data,16)[0]
            struct.pack_into('<I',data,16,0)
            if crc32c(data)!=checksum or data[:4]!=b'TPG2' or struct.unpack_from('<II',data,4)!=(kind,number):
                raise ValueError('data page CRC/identity')
            used=struct.unpack_from('<I',data,12)[0]
            if used>page-32: raise ValueError('data page payload')
            payload.append(data[32:32+used])
        result[kind]=b''.join(payload)
        if len(result[kind])!=logical: raise ValueError('logical section size')
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('artifact',type=Path);parser.add_argument('probe',type=Path)
    parser.add_argument('selection',type=Path);parser.add_argument('hgt',type=Path)
    parser.add_argument('--geometry-python-path',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    sys.path.insert(0,str(args.geometry_python_path))
    import shapely
    from shapely.geometry import Polygon,Point
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    manifest_path=args.artifact/'manifest.json'
    manifest=json.loads(manifest_path.read_text())
    package=args.artifact/manifest['output']['file']
    for record in (manifest['source'],manifest['outdoorSource']):
        if sha(Path(record['path']))!=record['sha256']: raise ValueError('Center input changed')
    doc=json.loads((args.artifact/'graph.json').read_text(encoding='utf-8'))
    if sha(package)!=manifest['output']['sha256'] or sha(args.hgt)!=doc['terrain_source']['sha256']:
        raise ValueError('package/HGT identity mismatch')
    data=sections(package.read_bytes())
    if not {1000,1001,1002,1010,1011,1012,1013,1108,1109,1117,1200,1201,1202,1203}<=data.keys():
        raise ValueError('incomplete joint capabilities')
    graph=doc['graph']; nodes=sorted(graph['nodes'],key=lambda n:n['id'])
    ids={n['id']:i for i,n in enumerate(nodes)}
    edges=sorted(graph['edges'],key=lambda e:(ids[e['from']],str(e['id'])))
    selected=json.loads(args.selection.read_text())
    start,destination=ids[selected['start_osm_node']],ids[selected['destination_osm_node']]
    prefix=max(range(len(edges)),key=lambda row:sum(struct.unpack_from('<II',data[1102],row*48+16)))
    prefix_length=struct.unpack_from('<I',data[1102],prefix*48+12)[0]
    prefix_expected=integer_totals(data,[[prefix,0,prefix_length//2]])
    features=[]
    for f in doc['administration']:
        rings=[[(point(p)[1],point(p)[0]) for p in ring] for ring in f['rings']]
        features.append((f,Polygon(rings[0],rings[1:])))
    def administrative(location):
        p=Point(location[1],location[0])
        matches=[(f,g) for f,g in features if g.covers(p)]
        if not matches: return '',False
        depth=max(len(f['path']) for f,g in matches)
        matches=[(f,g) for f,g in matches if len(f['path'])==depth]
        path=matches[0][0]['path'][:]
        for f,g in matches[1:]:
            path=path[:next((i for i,(a,b) in enumerate(zip(path,f['path'])) if a!=b),min(len(path),len(f['path'])))]
        return ' / '.join(path),any(g.boundary.covers(p) for f,g in matches)
    locations={}
    for f,g in features:
        if len(f['path'])==3 and f['osm_relation'] not in locations:
            p=g.representative_point()
            locations[f['osm_relation']]=(int(round(p.y)),int(round(p.x)))
    cases=[]
    for identity,location in locations.items():
        for zoom in (7,12,14,16,18):
            command=[str(args.probe.resolve()),str(package.resolve()),str(start),str(destination),'0',str(location[0]),str(location[1]),str(zoom),str(prefix)]
            value=json.loads(subprocess.run(command,capture_output=True,text=True,check=True).stdout)
            expected_path,boundary=administrative(location)
            expected_elevation=elevation_at(doc['dem'],location)
            if value['path']!=expected_path or value['boundary']!=boundary or value['depth']!=len(expected_path.split(' / ')):
                raise AssertionError('SDK administrative hierarchy differs from GEOS E7 oracle')
            if expected_elevation is None or abs(value['elevation_m']-expected_elevation)>1e-7:
                raise AssertionError('SDK elevation differs from source grid')
            if [value['prefix_distance_cm'],value['prefix_ascent_cm'],value['prefix_descent_cm']]!=prefix_expected:
                raise AssertionError('SDK partial edge profile differs from packed arithmetic')
            if value['segments']>128 or zoom==7 and value['segments']:
                raise AssertionError('render capacity/zoom policy violated')
            if value['route']=='ok':
                total=integer_totals(data,[[row,0,0xffffffff] for row in value['edges']])
                if value['statistics']!='ok' or [value[k] for k in ('length_cm','ascent_cm','descent_cm')]!=total or value['route_cost_units']!=total[0]:
                    raise AssertionError('SDK route/elevation totals mismatch')
            cases.append(dict(relation=identity,location_e7=location,zoom=zoom,expected_path=expected_path,expected_elevation_m=expected_elevation,**value))
    root=Path(__file__).resolve().parents[1]
    sources=['src/terrain.cpp','src/administration.cpp','src/navigation.cpp','src/edge_rules.cpp','src/integer_elevation.cpp',
             'src/resource_budget.cpp','src/binary.h','include/tmap/terrain.h','include/tmap/navigation.h','include/tmap/resource_budget.h',
             'tools/import_osm_administration.py','tools/prepare_real_liechtenstein_outdoor.py','tools/benchmark_real_outdoor_joint.py','examples/outdoor_joint_probe.cpp']
    report=dict(scope='Whole real Center Liechtenstein PBF graph, actual Mapzen Skadi HGT, original OSM country/district/municipal polygons. Cold process/cache per view, serialized round-robin budget-one calls, Busy every other file read. Two graph/rule readers for independent in-flight graph operations. Source-grid elevation and packed profile/route arithmetic checked; GEOS administrative oracle. No surveyed accuracy or physical MCU/PSRAM peak claim.',
        manifest_sha256=sha(manifest_path),package_sha256=sha(package),package_bytes=package.stat().st_size,probe_sha256=sha(args.probe),
        original_pbf_sha256=manifest['source']['sha256'],hgt_sha256=sha(args.hgt),outdoor_document_sha256=manifest['outdoorSource']['sha256'],
        nodes=len(nodes),edges=len(edges),sections=len(data),dem_columns=len(doc['dem']['heights'][0]),dem_rows=len(doc['dem']['heights']),
        administration_components=len(features),selected_relations=doc['administrative_source']['report']['selected_relations'],
        geometry_engine=dict(shapely=shapely.__version__,geos=shapely.geos_version_string),prefix_edge=prefix,prefix_oracle_cm=prefix_expected,
        source_sha256={name:sha(root/name) for name in sources},cases=cases,render_results=dict(Counter(r['contours'] for r in cases)))
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'render_results':report['render_results'],'payloads':sorted(set(c['library_payload'] for c in cases))}))


if __name__=='__main__': main()
