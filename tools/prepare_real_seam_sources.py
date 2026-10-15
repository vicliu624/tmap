"""Select two original real OSM roads with a shared interior/endpoint seam.

Creates explicitly selected way extracts, not complete geographic region claims.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from import_osm import access, tags
from merge_osm_regions import canonical, digest
from osm_pbf import iter_pbf


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pbf',type=Path)
    parser.add_argument('source_directory',type=Path)
    parser.add_argument('artifact_directory',type=Path)
    parser.add_argument('--python',required=True)
    args = parser.parse_args()
    nodes,ways,blocked = {},{},set()
    for element in iter_pbf(args.pbf):
        t = tags(element)
        if element.tag == 'node':
            key,value = canonical(element)
            nodes[key[1]] = value
        elif element.tag == 'way':
            refs = [int(n.attrib['ref']) for n in element.findall('nd')]
            try: permitted = access(t,1)
            except ValueError: continue
            if permitted and 2 <= len(refs) <= 12 and len(set(refs)) == len(refs) and t.get('oneway:foot','no') in ('no','0','false'):
                key,value = canonical(element)
                ways[key[1]] = value
        elif element.tag == 'relation' and (t.get('restriction:foot') or t.get('restriction:foot:conditional') or t.get('type') == 'restriction:foot'):
            blocked.update(int(m.attrib['ref']) for m in element.findall('member') if m.attrib['type'] == 'way')
    eligible = {i:value for i,value in ways.items() if i not in blocked and all(n in nodes and not dict(nodes[n][1]).get('barrier') for n in value[0])}
    endpoints = {}
    for i,(refs,t) in sorted(eligible.items()):
        for n in (refs[0],refs[-1]): endpoints.setdefault(n,[]).append(i)
    selected = None
    for main,(refs,t) in sorted(eligible.items()):
        for junction in refs[1:-1]:
            for branch in endpoints.get(junction,()):
                if branch != main and set(refs)&set(eligible[branch][0]) == {junction}:
                    selected = main,branch,junction
                    break
            if selected: break
        if selected: break
    if selected is None: raise ValueError('no eligible real interior/endpoint seam found')
    main,branch,junction = selected
    args.source_directory.mkdir(parents=True,exist_ok=False)
    def write(label,way):
        refs,t = eligible[way]
        root = ET.Element('osm',version='0.6')
        for n in sorted(refs):
            (lat,lon),nt = nodes[n]
            node = ET.SubElement(root,'node',id=str(n),lat=str(lat/1e7),lon=str(lon/1e7))
            for k,v in nt: ET.SubElement(node,'tag',k=k,v=v)
        element = ET.SubElement(root,'way',id=str(way))
        for n in refs: ET.SubElement(element,'nd',ref=str(n))
        for k,v in t: ET.SubElement(element,'tag',k=k,v=v)
        path = args.source_directory/(label+'.osm')
        ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)
        return path
    west,east = write('main',main),write('branch',branch)
    epoch = 'real-selected-seam-'+digest(args.pbf)[:16]
    base = dict(ToolsDirectory=str(Path(__file__).resolve().parent),PythonExecutable=args.python,
                PageBytes=1024,Landmarks=2,TravelMode='foot',TopologyEpoch=epoch)
    for label,path,others in [('main',west,[]),('branch',east,[]),('union',west,[str(east.resolve())])]:
        plan = dict(base,PbfPath=str(path.resolve()),RegionalPbfPaths=others,
                    OutputDirectory=str((args.artifact_directory/label).resolve()))
        (args.source_directory/(label+'-plan.json')).write_text(json.dumps(plan,indent=2)+'\n')
    selection = dict(scope='Two selected original OSM ways, independently imported/Center-exported; not complete geographic regions.',
        original_pbf_sha256=digest(args.pbf),main_way=main,branch_way=branch,junction_node=junction,
        start_osm_node=eligible[main][0][0],destination_osm_node=next(n for n in (eligible[branch][0][0],eligible[branch][0][-1]) if n!=junction),
        main_source_sha256=digest(west),branch_source_sha256=digest(east))
    (args.source_directory/'selection.json').write_text(json.dumps(selection,indent=2)+'\n')
    print(json.dumps(selection))


if __name__ == '__main__': main()
