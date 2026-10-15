import copy
import json
import math
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from tmap_pack import write, crc32c
from make_legacy_administration import write_legacy_administration
import tmap_pack


def document():
    return {
        "name": "synthetic terrain and restricted junction",
        "contour_lod": False,
        "dem": {"origin": [0, 0], "step": [0.001, 0.001], "resolution_m": 30,
                "block_side": 2, "vertical_datum": 1, "source": "synthetic",
                "heights": [[100, 110, 120], [130, 140, 150], [160, 170, 180]]},
        "contours": [
            {"id": 1, "elevation_m": 100, "points": [[i*0.00001, 0.0005] for i in range(201)]},
            {"id": 2, "elevation_m": 110, "points": [[0, 0.001], [0.002, 0.001]]},
            {"id": 3, "elevation_m": 120, "points": [[0, 0.0015], [0.002, 0.0015]]}],
        "graph": {
            "nodes": [{"id": 1, "point": [0, 0]}, {"id": 2, "point": [0.001, 0]},
                      {"id": 3, "point": [0.002, 0]}, {"id": 4, "point": [0.001, 0.001]},
                      {"id": 5, "point": [0.002, 0.002]}],
            "edges": [{"id": "a", "from": 1, "to": 2}, {"id": "b", "from": 2, "to": 3},
                      {"id": "d", "from": 2, "to": 4}, {"id": "r", "from": 2, "to": 1},
                      {"id": "c", "from": 4, "to": 3}],
            "restrictions": [{"from": "a", "to": "b", "access": 1, "kind": "no"}]}}


def main(directory):
    directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
    doc = document()
    report = write(doc, directory/"outdoor.tmap")
    shared = document()
    shared['dem']['profile_spacing_m'] = 100000
    shared['dem']['profile_threshold_m'] = 0
    shared['graph'] = {'nodes':[{'id':i+1,'point':[(i%10)*.0002,(i//10)*.0001]} for i in range(201)],
                       'edges':[{'id':str(i),'from':i+1,'to':i+2} for i in range(200)],'landmarks':8}
    shared['contours'] = [{'id':1,'elevation_m':100,'points':[[i*.0000001,.0005] for i in range(20001)]}]
    shared['administration'] = [{'path':['Test','Area'],'rings':[[*[ [i*.000001,0] for i in range(2001)], [.002,.002],[0,.002],[0,0]]]}]
    write(shared,directory/'shared-cache.tmap')
    (directory/"outdoor.json").write_text(json.dumps(doc, indent=2), encoding="utf-8")
    (directory/"size-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    missing = copy.deepcopy(doc); missing["dem"]["heights"][1][1] = None
    write(missing, directory/"missing.tmap")
    generated = copy.deepcopy(doc); del generated["contours"]
    write(generated, directory/"generated.tmap")
    lod = copy.deepcopy(doc); lod["contour_lod"] = True
    write(lod, directory/"lod.tmap")
    dense = {"name": "dense synthetic mountain contours", "contours":
             [{"id": i+1, "elevation_m": (i+1)*10,
               "points": [[-0.003+j*0.00003, -0.002+i*0.0001+math.sin(j/7)*0.00008] for j in range(201)]}
              for i in range(40)]}
    write(dense, directory/"dense-contours.tmap")
    stream = copy.deepcopy(dense); stream["contour_lod"] = False; stream["contour_overview"] = False
    write(stream,directory/"read-stream.tmap")
    write({"generate_contours":False,"dem":{"origin":[0,0],"step":[.00001,.00001],"resolution_m":1,"block_side":32,
          "heights":[[x+2*y for x in range(64)] for y in range(64)]}},directory/"dem-boundary.tmap")
    chain = copy.deepcopy(doc)
    chain["graph"] = {"nodes": [{"id": 1, "point": [0, 0]}, {"id": 2, "point": [0.001, 0]},
                                {"id": 3, "point": [0.002, 0]}],
                      "edges": [{"id": "f1", "from": 1, "to": 2, "opposite": "r1"},
                                {"id": "r1", "from": 2, "to": 1, "opposite": "f1"},
                                {"id": "f2", "from": 2, "to": 3, "opposite": "r2"},
                                {"id": "r2", "from": 3, "to": 2, "opposite": "f2"}]}
    write(chain, directory/"chain.tmap")
    history = copy.deepcopy(chain)
    history['graph'] = {'nodes':[{'id':i+1,'point':[i*.001,0]} for i in range(4)],
                        'edges':[{'id':name,'from':i+1,'to':i+2} for i,name in enumerate(('a','b','c'))],
                        'landmarks':2,'via_restrictions':[{'edges':['a','b','c'],'kind':'no','access':1}]}
    write(history,directory/'history-no.tmap')
    from import_osm import import_records
    from test_importers import via_osm
    import xml.etree.ElementTree as ET
    imported_history = copy.deepcopy(history)
    imported_history['graph'] = import_records(ET.fromstring(via_osm()))
    imported_history['graph']['landmarks'] = 2
    write(imported_history,directory/'history-no.tmap')
    history_bytes = tmap_pack.build(imported_history)[0]
    history_page_bytes = struct.unpack_from('<I',history_bytes,20)[0]
    history_directory = struct.unpack_from('<Q',history_bytes,40)[0]
    for row in range(struct.unpack_from('<I',history_bytes,48)[0]):
        entry = history_directory + row*64
        if struct.unpack_from('<I',history_bytes,entry)[0] == 1114:
            history_page = struct.unpack_from('<Q',history_bytes,entry+16)[0]
            break
    malformed_history = bytearray(history_bytes)
    struct.pack_into('<I',malformed_history,history_page+32+12,4)
    struct.pack_into('<I',malformed_history,history_page+16,0)
    struct.pack_into('<I',malformed_history,history_page+16,crc32c(malformed_history[history_page:history_page+history_page_bytes]))
    (directory/'history-invalid-flags.tmap').write_bytes(malformed_history)
    history['graph']['nodes'].append({'id':5,'point':[.002,.001]})
    history['graph']['edges'].append({'id':'d','from':3,'to':5})
    history['graph']['via_restrictions'][0]['kind'] = 'only'
    write(history,directory/'history-only.tmap')
    detour = copy.deepcopy(history)
    detour['graph']['via_restrictions'][0]['kind'] = 'no'
    detour['graph']['edges'] = detour['graph']['edges'][:3] + [{'id':'s','from':1,'to':5},{'id':'t','from':5,'to':2}]
    write(detour,directory/'history-detour.tmap')
    bent = copy.deepcopy(chain)
    bent["graph"]["nodes"][2]["point"] = [0.001, 0.001]
    write(bent, directory/"bent-chain.tmap")
    one_way = copy.deepcopy(chain)
    one_way["graph"]["edges"] = [e for e in one_way["graph"]["edges"] if e["id"].startswith("f")]
    for e in one_way["graph"]["edges"]: e.pop("opposite")
    write(one_way, directory/"one-way.tmap")
    restricted = copy.deepcopy(one_way)
    restricted["graph"]["restrictions"] = [{"from": "f1", "to": "f2", "access": 1}]
    write(restricted, directory/"endpoint-restricted.tmap")
    restricted["graph"]["landmarks"] = 2
    write(restricted, directory/"endpoint-restricted-alt.tmap")
    missing_chain = copy.deepcopy(chain)
    missing_chain["dem"]["heights"][0][1] = None
    write(missing_chain, directory/"unknown-edge-height.tmap")
    boundary = copy.deepcopy(chain)
    boundary["graph"]["nodes"].append({"id":4,"point":[0.002,0.001]})
    boundary["graph"]["nodes"][2]["boundary"] = True
    write(boundary, directory/"boundary.tmap")
    raw = bytearray((directory/"outdoor.tmap").read_bytes())
    directory_offset = struct.unpack_from("<Q",raw,40)[0]
    first_page = struct.unpack_from("<Q",raw,directory_offset+16)[0]
    raw[first_page+40] ^= 1
    (directory/"bad-page.tmap").write_bytes(raw)
    raw = bytearray((directory/"outdoor.tmap").read_bytes()); raw[struct.unpack_from("<Q",raw,40)[0]] ^= 1
    (directory/"bad-directory.tmap").write_bytes(raw)
    raw = bytearray((directory/"outdoor.tmap").read_bytes()); raw[140] = 0x80
    struct.pack_into("<I", raw, 160, 0); struct.pack_into("<I", raw, 160, crc32c(raw[:256]))
    (directory/"unknown-capability.tmap").write_bytes(raw)
    def ring(w,s,e,n): return [[w,s],[e,s],[e,n],[w,n],[w,s]]
    administration = [
        {"path":["Country","Province"],"rings":[ring(0,0,.004,.004)]},
        {"path":["Country","Province","West"],"rings":[ring(0,0,.002,.004),ring(.0005,.0025,.0015,.0035)]},
        {"path":["Country","Province","East"],"rings":[ring(.002,0,.004,.004)]},
        {"path":["中国","云南省","昆明市"],"rings":[ring(.005,0,.007,.002)]}]
    write({"administration":administration},directory/"administration.tmap")
    invalid_admin = bytearray((directory/"administration.tmap").read_bytes())
    for i in range(struct.unpack_from("<I",invalid_admin,48)[0]):
        entry = struct.unpack_from("<Q",invalid_admin,40)[0]+i*64
        if struct.unpack_from("<I",invalid_admin,entry)[0] == 1203:
            start = struct.unpack_from("<Q",invalid_admin,entry+16)[0]
            invalid_admin[start+34] = 0xff
            struct.pack_into("<I",invalid_admin,start+16,0)
            page_bytes = struct.unpack_from("<I",invalid_admin,20)[0]
            struct.pack_into("<I",invalid_admin,start+16,crc32c(invalid_admin[start:start+page_bytes]))
    (directory/"bad-admin-utf8.tmap").write_bytes(invalid_admin)
    write_legacy_administration(directory/"legacy-administration.tmap")
    # A valid minimal v1 container with empty POI/index tables verifies legacy
    # reading remains separate from the outdoor profile.
    types = [1, 2, 20, 21, 22, 23, 24, 30, 31, 32, 40, 41, 42]
    directory_bytes = bytearray()
    for section in types:
        directory_bytes.extend(struct.pack("<IHHII6Q", section, 1, 0, section, 0, 8192, 0, 0, 0, 0, 0))
    legacy = bytearray(8192); legacy[:8] = b"TMAP\r\n\x1a\n"
    struct.pack_into("<HHIIIQQQII", legacy, 8, 1, 0, 256, 0x01020304, 4096, 0, len(legacy), 4096, len(types), 64)
    struct.pack_into("<4iIIQQ", legacy, 120, 0, 0, 20000, 20000, 0, 0, 8192, 0)
    struct.pack_into("<2I", legacy, 168, 1, 2)
    struct.pack_into("<I", legacy, 164, crc32c(directory_bytes))
    struct.pack_into("<I", legacy, 160, crc32c(legacy[:256]))
    legacy[4096:4096+len(directory_bytes)] = directory_bytes
    (directory/"legacy.tmap").write_bytes(legacy)
    from make_virtual_fixtures import generate
    timed = document()
    timed["graph"]["travel_modes"] = 1
    timed["graph"]["condition_snapshot"] = "1970-01-01T02:00:00+01:00"
    timed_bytes = tmap_pack.build(timed)[0]
    (directory/"timed-profile.tmap").write_bytes(timed_bytes)
    page_size = struct.unpack_from("<I",timed_bytes,20)[0]
    directory_start = struct.unpack_from("<Q",timed_bytes,40)[0]
    for row in range(struct.unpack_from("<I",timed_bytes,48)[0]):
        entry = directory_start+row*64
        if struct.unpack_from("<I",timed_bytes,entry)[0] == 1113:
            profile_page = struct.unpack_from("<Q",timed_bytes,entry+16)[0]
            break
    for index,(relative,value) in enumerate(((0,2),(4,2),(4,0),(16,86400),(20,0),(20,4),(12,0x80000000))):
        broken = bytearray(timed_bytes)
        struct.pack_into("<I",broken,profile_page+32+relative,value)
        struct.pack_into("<I",broken,profile_page+16,0)
        struct.pack_into("<I",broken,profile_page+16,crc32c(broken[profile_page:profile_page+page_size]))
        (directory/f"bad-routing-profile-{index}.tmap").write_bytes(broken)
    generate(directory,tmap_pack.DEFAULT_PAGE_BYTES)
    runtime = document()
    runtime['graph']['edges'][0].update(road_tags={'highway':'path','foot:conditional':'no @ (Mo)'},road_reverse=False)
    (directory/'runtime-road-program.tmap').write_bytes(tmap_pack.build(runtime)[0])
    runtime['graph'] = {'nodes':[{'id':1,'point':[0,0]},{'id':2,'point':[.001,0]},{'id':3,'point':[.002,0]}],
        'edges':[{'id':'a','from':1,'to':2,'access':3,'road_tags':{'highway':'path','foot:conditional':'no @ (Mo)'},'road_reverse':False},
                 {'id':'b','from':2,'to':3,'access':3}]}
    runtime['graph']['landmarks'] = 2
    (directory/'runtime-road-route.tmap').write_bytes(tmap_pack.build(runtime)[0])
    tags = {'highway':'path','oneway':'yes','oneway:foot':'yes',
            'oneway:bicycle:conditional':'no @ (Mo)','oneway:foot:conditional':'no @ (Mo)'}
    runtime['graph']['edges'] = [
        {'id':'a-f','from':1,'to':2,'access':3,'opposite':'a-r','road_tags':tags,'road_reverse':False},
        {'id':'a-r','from':2,'to':1,'access':3,'opposite':'a-f','road_tags':tags,'road_reverse':True},
        {'id':'b-f','from':2,'to':3,'access':3,'opposite':'b-r','road_tags':tags,'road_reverse':False},
        {'id':'b-r','from':3,'to':2,'access':3,'opposite':'b-f','road_tags':tags,'road_reverse':True}]
    (directory/'runtime-road-coordinate.tmap').write_bytes(tmap_pack.build(runtime)[0])
    for edge in runtime['graph']['edges']:
        edge['barrier_tags'] = [
            {'barrier':'gate','foot:conditional':'no @ (Mo)'},
            {'barrier':'gate','bicycle:conditional':'no @ (Tu)'}]
    barrier_bytes = tmap_pack.build(runtime)[0]
    (directory/'runtime-barrier-route.tmap').write_bytes(barrier_bytes)
    from compile_turn_conditions import encode_turn_program
    turn_descriptor,turn_records = encode_turn_program({'restriction':'no_right_turn',
        'restriction:bicycle:conditional':'none @ (Th)'},'bicycle')
    original_graph_sections = tmap_pack.graph_sections
    def turn_fixture_sections(graph,dem=None):
        sections = original_graph_sections(graph,dem)
        meta = bytearray(sections[0][1])
        struct.pack_into('<I',meta,8,struct.unpack_from('<I',meta,8)[0] | 262144)
        sections[0] = (1100,bytes(meta),1)
        return sections+[(1120,turn_descriptor*104,104),(1121,turn_records,1)]
    tmap_pack.graph_sections = turn_fixture_sections
    try: (directory/'runtime-turn-program.tmap').write_bytes(tmap_pack.build(document())[0])
    finally: tmap_pack.graph_sections = original_graph_sections
    turn_context = document()
    turn_context['graph'] = {
        'landmarks':2,
        'nodes':[{'id':i,'point':[(i-1)*.001,0]} for i in range(1,6)],
        'edges':[{'id':'a','from':1,'to':2},{'id':'b','from':2,'to':3},
                 {'id':'c','from':3,'to':4},{'id':'d','from':3,'to':5}],
        'conditional_turns':[{'edges':['a','b','c'],'mode':'foot','group':1,
            'tags':{'restriction:foot':'no_straight_on','restriction:foot:conditional':'only_straight_on @ (Th)'}}]}
    turn_context['graph']['nodes'][4]['point'] = [.002,.001]
    (directory/'runtime-turn-context.tmap').write_bytes(tmap_pack.build(turn_context)[0])
    combined = copy.deepcopy(turn_context)
    combined['graph']['conditional_turns'][0]['tags'] = {'restriction:foot':'none',
        'restriction:foot:conditional':'no_straight_on @ (Mo); only_straight_on @ (Th)'}
    combined['graph']['edges'][0].update(road_tags={'highway':'path','foot:conditional':'no @ (Tu)'},road_reverse=False)
    combined['graph']['edges'][1]['barrier_tags'] = [{'barrier':'gate','bicycle:conditional':'no @ (We)'}]
    (directory/'runtime-combined-rules.tmap').write_bytes(tmap_pack.build(combined)[0])
    full = copy.deepcopy(combined)
    full['contour_lod'] = True
    full['graph']['travel_modes'] = 3
    full['administration'] = [{'path':['Test Country','Test District'],
        'rings':[[[-.001,-.001],[.004,-.001],[.004,.003],[-.001,.003],[-.001,-.001]]]}]
    full_bytes,full_report = tmap_pack.build(full)
    assert len(full_report['sections']) == 37
    (directory/'runtime-full-features.tmap').write_bytes(full_bytes)
    (directory/'runtime-full-features.json').write_text(json.dumps(full,indent=2))
    (directory/'runtime-full-features-report.json').write_text(json.dumps(full_report,indent=2))
    full_graph_sections = tmap_pack.graph_sections
    tmap_pack.graph_sections = lambda graph,dem=None: full_graph_sections(graph,dem)+[(2000+i,b'',0) for i in range(27)]
    try: maximum_bytes = tmap_pack.build(full)[0]
    finally: tmap_pack.graph_sections = full_graph_sections
    (directory/'directory-64.tmap').write_bytes(maximum_bytes)
    invalid_directory = bytearray(maximum_bytes)
    struct.pack_into('<I',invalid_directory,48,65)
    struct.pack_into('<I',invalid_directory,160,0)
    struct.pack_into('<I',invalid_directory,160,crc32c(invalid_directory[:256]))
    (directory/'directory-65-invalid.tmap').write_bytes(invalid_directory)
    turn_grouped = copy.deepcopy(turn_context)
    turn_grouped['graph']['conditional_turns'].append(dict(turn_grouped['graph']['conditional_turns'][0],edges=['a','b','d']))
    (directory/'runtime-turn-union.tmap').write_bytes(tmap_pack.build(turn_grouped)[0])
    turn_grouped['graph']['conditional_turns'].append({'edges':['a','b','d'],'mode':'foot','group':2,
        'tags':{'restriction:foot':'only_straight_on','restriction:foot:conditional':'none @ (Mo)'}})
    grouped_bytes = tmap_pack.build(turn_grouped)[0]
    (directory/'runtime-turn-intersection.tmap').write_bytes(grouped_bytes)
    overlap = document()
    overlap['graph'] = {'nodes':[{'id':1,'point':[0,0]},{'id':2,'point':[.001,0]}],
        'edges':[{'id':'a','from':1,'to':1,'points':[[0,0],[.0002,.0002],[0,0]]},
                 {'id':'b','from':1,'to':2}],
        'conditional_turns':[{'edges':['a','a','b'],'mode':'foot','group':1,
            'tags':{'restriction:foot':'only_straight_on','restriction:foot:conditional':'none @ (Mo)'}}]}
    (directory/'runtime-turn-overlap.tmap').write_bytes(tmap_pack.build(overlap)[0])
    grouped_page_bytes = struct.unpack_from('<I',grouped_bytes,20)[0]
    grouped_payload = grouped_page_bytes-32
    grouped_directory = struct.unpack_from('<Q',grouped_bytes,40)[0]
    grouped_sections = {}
    for row in range(struct.unpack_from('<I',grouped_bytes,48)[0]):
        entry = grouped_directory+64*row
        grouped_sections[struct.unpack_from('<I',grouped_bytes,entry)[0]] = struct.unpack_from('<Q',grouped_bytes,entry+16)[0]
    for name,kind,logical,format_code,value in (
        ('index-range',1124,32,'<Q',0xffffffffffffffff),
        ('index-reserved',1124,44,'<I',1),
        ('match-reference',1125,64,'<I',0xffffffff),
        ('match-flags',1125,76,'<I',2),
        ('match-complete',1125,76,'<I',0),
        ('match-next',1125,72,'<I',2),
        ('relation-range',1122,24,'<Q',0xfffffffffffffffc),
        ('program-domain',1120,60,'<I',2),
        ('inactive-action',1121,44,'<I',11)):
        malformed = bytearray(grouped_bytes)
        page = grouped_sections[kind]+(logical//grouped_payload)*grouped_page_bytes
        struct.pack_into(format_code,malformed,page+32+logical%grouped_payload,value)
        struct.pack_into('<I',malformed,page+16,0)
        struct.pack_into('<I',malformed,page+16,crc32c(malformed[page:page+grouped_page_bytes]))
        (directory/f'runtime-turn-invalid-{name}.tmap').write_bytes(malformed)
    barrier_page_bytes = struct.unpack_from('<I',barrier_bytes,20)[0]
    barrier_payload = barrier_page_bytes-32
    barrier_directory = struct.unpack_from('<Q',barrier_bytes,40)[0]
    barrier_sections = {}
    for row in range(struct.unpack_from('<I',barrier_bytes,48)[0]):
        entry = barrier_directory+64*row
        barrier_sections[struct.unpack_from('<I',barrier_bytes,entry)[0]] = struct.unpack_from('<Q',barrier_bytes,entry+16)[0]
    for name,kind,logical,format_code,value in (
        ('direction',1117,120+8+4*16+12,'<I',2),
        ('reversed',1117,120+6,'<B',1),
        ('inactive-action',1118,24+20,'<I',99),
        ('reference',1119,4,'<I',0xfffffffe),
        ('range',1117,120+8+3*16,'<Q',0xffffffffffffffff)):
        malformed = bytearray(barrier_bytes)
        page = barrier_sections[kind]+(logical//barrier_payload)*barrier_page_bytes
        struct.pack_into(format_code,malformed,page+32+logical%barrier_payload,value)
        struct.pack_into('<I',malformed,page+16,0)
        struct.pack_into('<I',malformed,page+16,crc32c(malformed[page:page+barrier_page_bytes]))
        (directory/f'runtime-barrier-invalid-{name}.tmap').write_bytes(malformed)


if __name__ == "__main__":
    if len(sys.argv) > 2: tmap_pack.DEFAULT_PAGE_BYTES = int(sys.argv[2])
    main(sys.argv[1])
