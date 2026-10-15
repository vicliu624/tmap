"""Host-only strict OSM boundary assembly and geometry-verified hierarchy.

Requires explicit selected relation IDs/root and optional host Shapely installation.
No polygon repair, boundary guessing, coordinate proximity join or MCU dependency.
"""
import argparse
import json
from pathlib import Path
import sys
from import_osm import xml_records, tags
from merge_osm_regions import digest
from osm_pbf import iter_pbf
from tmap_pack import point, administrative_sections


def stitch(parts, depth=0):
    if depth>32: raise ValueError('administrative ring decomposition depth exceeded')
    endpoints = {}
    for identity, refs in parts.items():
        if len(refs)<2 or any(a==b for a,b in zip(refs,refs[1:])):
            raise ValueError('invalid boundary way node sequence')
        for node in (refs[0],refs[-1]): endpoints.setdefault(node,[]).append(identity)
    if any(len(ways)<2 or len(ways)%2 for ways in endpoints.values()):
        raise ValueError('open administrative boundary')
    junctions=[n for n,w in endpoints.items() if len(w)>2]
    if junctions:
        # Separate rings that meet at an articulation node using original IDs,
        # without geometric angle guesses or polygon repair.
        junction=min(junctions)
        remaining=set(parts); components=[]
        while remaining:
            seed=min(remaining); remaining.remove(seed)
            pending=[seed]; component={seed}
            while pending:
                edge=pending.pop()
                for node in (parts[edge][0],parts[edge][-1]):
                    if node==junction: continue
                    for neighbour in endpoints[node]:
                        if neighbour in remaining:
                            remaining.remove(neighbour); component.add(neighbour); pending.append(neighbour)
            components.append(component)
        if len(components)==1: raise ValueError('nonseparable branching administrative rings')
        return [ring for component in components for ring in stitch({i:parts[i] for i in component},depth+1)]
    remaining = set(parts)
    rings = []
    while remaining:
        current = min(remaining); remaining.remove(current)
        ring = list(parts[current])
        while ring[-1]!=ring[0]:
            choices = [identity for identity in endpoints[ring[-1]] if identity in remaining]
            if len(choices)!=1: raise ValueError('ambiguous administrative ring')
            current = choices[0]; remaining.remove(current)
            refs = parts[current]
            if refs[0]!=ring[-1]: refs=refs[::-1]
            ring.extend(refs[1:])
        if len(ring)<4: raise ValueError('short administrative ring')
        rings.append(ring)
    return rings


def convert(records, selected, root, max_entities=1_000_000, max_references=8_000_000, max_vertices=500_000):
    from shapely.geometry import Polygon, MultiPolygon
    import shapely
    if not selected or root not in selected or len(set(selected))!=len(selected) or any(type(i) is not int or i<=0 for i in selected):
        raise ValueError('explicit distinct administrative relations including root required')
    if any(type(n) is not int or n<1 for n in (max_entities,max_references,max_vertices)):
        raise ValueError('invalid host administrative budget')
    nodes,ways,relations = {},{},{}
    count = references = 0
    for element in records:
        count+=1
        if count>max_entities: raise ValueError('administrative entity budget exceeded')
        identity = int(element.attrib['id'])
        if element.tag=='node': target,value=nodes,point([float(element.attrib['lon']),float(element.attrib['lat'])])
        elif element.tag=='way':
            target,value=ways,tuple(int(n.attrib['ref']) for n in element.findall('nd'))
            references+=len(value)
        elif element.tag=='relation':
            target,value=relations,(tags(element),tuple((m.attrib['type'],int(m.attrib['ref']),m.attrib.get('role','')) for m in element.findall('member')))
            references+=len(value[1])
        else: raise ValueError('unsupported OSM record')
        if references>max_references: raise ValueError('administrative reference budget exceeded')
        if identity in target and target[identity]!=value: raise ValueError('conflicting administrative source identity')
        target[identity]=value
    geometries,features,metadata = {},{},{}
    vertices = 0
    for identity in selected:
        if identity not in relations: raise ValueError('selected administrative relation missing')
        t,_ = relations[identity]
        if t.get('boundary')!='administrative' or not t.get('name') or not t.get('admin_level','').isdigit():
            raise ValueError('administrative name/level/boundary tags required')
        outer,inner = {},{}
        def members(relation,invert=False,ancestors=()):
            if relation in ancestors or len(ancestors)>=16 or relation not in relations:
                raise ValueError('missing/cyclic/deep administrative relation')
            rt,rs = relations[relation]
            if rt.get('type') not in ('boundary','multipolygon'):
                raise ValueError('unsupported administrative relation type')
            for kind,ref,role in rs:
                if role in ('admin_centre','label','subarea'): continue
                if role not in ('','outer','inner'): raise ValueError('unsupported administrative member role')
                is_inner = invert != (role=='inner')
                if kind=='relation': members(ref,is_inner,(*ancestors,relation))
                elif kind=='way':
                    if ref not in ways: raise ValueError('administrative way missing from extract')
                    if ref in outer or ref in inner: raise ValueError('duplicate administrative boundary way')
                    (inner if is_inner else outer)[ref]=ways[ref]
                else: raise ValueError('nonway administrative geometry member')
        members(identity)
        if not outer: raise ValueError('administrative relation has no outer boundary')
        def coordinates(ring):
            nonlocal vertices
            vertices+=len(ring)
            if vertices>max_vertices: raise ValueError('administrative vertex budget exceeded')
            if any(n not in nodes for n in ring): raise ValueError('administrative node missing from extract')
            if any(not -900000000<=nodes[n][0]<=900000000 or not -1800000000<=nodes[n][1]<=1800000000 for n in ring):
                raise ValueError('administrative coordinate outside WGS84')
            return [(nodes[n][1],nodes[n][0]) for n in ring]
        try:
            outers = [coordinates(ring) for ring in stitch(outer)]
            holes = [coordinates(ring) for ring in stitch(inner)]
        except ValueError as error: raise ValueError(f'administrative relation {identity}: {error}') from error
        polygons = [Polygon(ring) for ring in outers]
        assigned = [[] for _ in outers]
        for ring in holes:
            hole = Polygon(ring)
            owners = [i for i,p in enumerate(polygons) if p.contains(hole)]
            if not hole.is_valid or len(owners)!=1: raise ValueError('invalid or ambiguous administrative hole')
            assigned[owners[0]].append(ring)
        polygons = [Polygon(ring,assigned[i]) for i,ring in enumerate(outers)]
        geometry = MultiPolygon(polygons)
        if not geometry.is_valid or geometry.is_empty: raise ValueError('invalid administrative polygon; no repair applied')
        geometries[identity]=geometry
        features[identity]=[[ring,*assigned[i]] for i,ring in enumerate(outers)]
        metadata[identity]={'name':t['name'],'admin_level':int(t['admin_level'])}
    parents = {root:None}
    root_level = metadata[root]['admin_level']
    for identity in selected:
        if identity==root: continue
        if metadata[identity]['admin_level']<=root_level or not geometries[root].covers(geometries[identity]):
            raise ValueError('selected administrative relation outside declared root')
        candidates = [i for i in selected if metadata[i]['admin_level']<metadata[identity]['admin_level'] and geometries[i].covers(geometries[identity])]
        nearest = max(metadata[i]['admin_level'] for i in candidates)
        candidates = [i for i in candidates if metadata[i]['admin_level']==nearest]
        if len(candidates)!=1: raise ValueError('ambiguous administrative parent containment')
        parents[identity]=candidates[0]
    result = []
    for identity in selected:
        path,chain=[],[]
        current=identity
        while current is not None:
            path.insert(0,metadata[current]['name']); chain.insert(0,current); current=parents[current]
        for rings in features[identity]:
            result.append({'path':path,'rings':[[[lon/1e7,lat/1e7] for lon,lat in ring] for ring in rings],
                           'osm_relation':identity,'hierarchy_relations':chain})
    administrative_sections(result)  # Validate the exact downstream wire geometry/path constraints.
    return result,{'selected_relations':metadata,'parents':parents,'vertices':vertices,
                   'shapely':shapely.__version__,'geos':shapely.geos_version_string,
                   'scope':'Original OSM E7 boundary ways, exact stable-node stitching, validated holes/polygons, nearest containing selected admin level. No repair or surveyed accuracy claim.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path); parser.add_argument('output',type=Path)
    parser.add_argument('--root',required=True,type=int)
    parser.add_argument('--relations',required=True,type=int,nargs='+')
    parser.add_argument('--geometry-python-path',type=Path)
    args=parser.parse_args()
    if args.geometry_python_path: sys.path.insert(0,str(args.geometry_python_path))
    before=digest(args.input)
    records=iter_pbf(args.input) if args.input.suffix.lower()=='.pbf' else xml_records(args.input)
    features,report=convert(records,args.relations,args.root)
    if digest(args.input)!=before: raise ValueError('administrative source changed during import')
    doc={'name':'OSM selected administration','administration':features,
         'administrative_source':{'sha256':before,'attribution':'OpenStreetMap contributors','report':report}}
    args.output.write_text(json.dumps(doc,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({'features':len(features),'vertices':report['vertices'],'parents':report['parents']}))


if __name__=='__main__': main()
