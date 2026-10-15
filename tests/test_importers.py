from pathlib import Path
import sys
import tempfile
import gzip
import struct
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from import_dem import read_ascii
from import_osm import import_xml, access
from osm_conditions import parse_instant, matches, resolve_tags
from tmap_pack import build
from merge_regions import merge_graphs
from import_hgt import read_hgt
from import_administration import convert


OSM = '''<osm version="0.6">
<node id="1" lon="0" lat="0"/><node id="2" lon="0.001" lat="0"/>
<node id="3" lon="0.002" lat="0"/><node id="4" lon="0.001" lat="0.001"/>
<way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/>
<tag k="highway" v="residential"/><tag k="oneway" v="yes"/><tag k="name" v="山路"/></way>
<way id="11"><nd ref="2"/><nd ref="4"/><tag k="highway" v="path"/></way>
<relation id="20"><member type="way" ref="10" role="from"/>
<member type="node" ref="2" role="via"/><member type="way" ref="11" role="to"/>
<tag k="type" v="restriction"/><tag k="restriction:bicycle" v="no_right_turn"/>
</relation></osm>'''


def via_osm():
    raw = '<osm version="0.6">'+''.join(f'<node id="{i}" lon="{(i-1)*.001}" lat="0"/>' for i in range(1,5))
    for i in range(3):
        raw += f'<way id="{10+i}"><nd ref="{i+1}"/><nd ref="{i+2}"/><tag k="highway" v="path"/><tag k="oneway" v="yes"/><tag k="oneway:foot" v="yes"/></way>'
    return raw+'<relation id="1"><member type="way" ref="10" role="from"/><member type="way" ref="11" role="via"/><member type="way" ref="12" role="to"/><tag k="type" v="restriction"/><tag k="restriction:foot" v="no_straight_on"/></relation></osm>'


class ImporterTests(unittest.TestCase):
    def test_conditional_history_overlapping_prefix_matches(self):
        from route_history_compile import route_history_sections
        edges = [{'id':'a','from':1,'to':1},{'id':'b','from':1,'to':2}]
        tags = {'restriction:foot':'only_straight_on','restriction:foot:conditional':'none @ (Th)'}
        graph = {'conditional_turns':[{'edges':['a','a','b'],'mode':'foot','group':1,'tags':tags}]}
        sections = route_history_sections(graph,edges,{'a':0,'b':1})
        self.assertEqual(sections[0][2],3)
        self.assertEqual(struct.unpack_from('<QII',sections[2][1],32),(1,2,0))
        self.assertEqual(struct.unpack_from('<4I',sections[3][1],16),(0,1,0,0))
        self.assertEqual(struct.unpack_from('<4I',sections[3][1],32),(0,2,1,1))
        excepted = dict(graph['conditional_turns'][0],tags=dict(tags,**{'except':'foot'}))
        graph['conditional_turns'].insert(0,excepted)
        sections = route_history_sections(graph,edges,{'a':0,'b':1})
        self.assertEqual(struct.unpack_from('<I',sections[3][1])[0],0)

    def test_conditional_relation_preallocation_bounds(self):
        from osm_conditional_turns import conditional_relation_paths
        modes = [('bicycle',2,{'no_right_turn'})]
        ways = {10:[{'id':str(i),'from':i+10,'to':1,'access':2} for i in range(4)],
                20:[{'id':'out','from':1,'to':2,'access':2}]}
        with self.assertRaisesRegex(ValueError,'path budget'):
            conditional_relation_paths(1,[10],[20],1,{},modes,{},ways,max_paths=3)
        self.assertEqual(len(conditional_relation_paths(1,[10],[20],1,{},modes,{},ways,max_paths=4)),4)
        with self.assertRaisesRegex(ValueError,'member-pair'):
            conditional_relation_paths(1,[10,11],[20,21],1,{},modes,{},ways,max_paths=3)
        for group in (0,-1,True,2**64):
            with self.assertRaises(ValueError): conditional_relation_paths(group,[10],[20],1,{},modes,{},ways)
        for limit in (0,-1,True,4097):
            with self.assertRaises(ValueError): conditional_relation_paths(1,[10],[20],1,{},modes,{},ways,max_paths=limit)

    def test_runtime_conditional_relation_import(self):
        raw = OSM.replace('</relation>','<tag k="restriction:bicycle:conditional" v="none @ (Mo)"/></relation>')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'turn.osm'; path.write_text(raw,encoding='utf-8')
            graph = import_xml(path,2,runtime_conditions=True)
            self.assertEqual(graph['restrictions'],[])
            self.assertEqual(graph['conditional_turns'][0]['edges'],['10:0:f','11:0:f'])
            self.assertEqual(graph['conditional_turns'][0]['group'],20)
            self.assertEqual(graph['conditional_turns'][0]['mode'],'bicycle')
            self.assertTrue(build({'graph':graph})[0])
            self.assertNotIn('conditional_turns',import_xml(path,1,runtime_conditions=True))
            via = via_osm().replace('</relation>','<tag k="restriction:foot:conditional" v="only_straight_on @ (Mo)"/></relation>')
            path.write_text(via,encoding='utf-8')
            graph = import_xml(path,1,runtime_conditions=True)
            self.assertNotIn('via_restrictions',graph)
            self.assertEqual(graph['conditional_turns'][0]['edges'],['10:0:f','11:0:f','12:0:f'])
            self.assertTrue(build({'graph':graph})[0])
            path.write_text(raw.replace('none @ (Mo)','none @ (PH)'),encoding='utf-8')
            with self.assertRaises(ValueError): import_xml(path,2,runtime_conditions=True)
            path.write_text(raw.replace('</relation>','<tag k="except" v=" bicycle "/></relation>'),encoding='utf-8')
            self.assertNotIn('conditional_turns',import_xml(path,2,runtime_conditions=True))

    def test_conditional_relation_sections(self):
        from turn_rule_sections import turn_rule_sections
        from tmap_pack import graph_sections
        edges = [{'id':'a','from':1,'to':2},{'id':'b','from':2,'to':3},{'id':'c','from':2,'to':4}]
        index = {e['id']:i for i,e in enumerate(edges)}
        tags = {'restriction':'only_straight_on','restriction:bicycle:conditional':'none @ (Mo)'}
        rules = [{'edges':['a','b'],'tags':tags,'mode':'bicycle','group':9},
                 {'edges':['a','c'],'tags':tags,'mode':'bicycle','group':9}]
        graph = {'nodes':[{'id':i,'point':[i*.001,0]} for i in range(1,5)],'edges':edges,'conditional_turns':rules}
        sections = turn_rule_sections(graph,edges,index)
        self.assertEqual([s[0] for s in sections],[1120,1121,1122,1123])
        self.assertEqual(sections[0][2],1)
        self.assertEqual(struct.unpack_from('<QIIQ',sections[2][1]),(0,2,0,9))
        self.assertEqual(struct.unpack_from('<QIIQ',sections[2][1],24),(8,2,0,9))
        self.assertEqual(struct.unpack('<4I',sections[3][1]),(0,1,0,2))
        self.assertTrue(build({'graph':graph})[0])
        self.assertEqual(struct.unpack_from('<I',graph_sections(graph)[0][1],8)[0] & 262144,262144)
        from route_history_compile import route_history_sections
        history = route_history_sections(graph,edges,index)
        self.assertEqual([s[0] for s in history],[1114,1115,1124,1125])
        self.assertEqual(history[0][2],2) # root and prefix (edge a).
        self.assertEqual(struct.unpack_from('<I',graph_sections(graph)[0][1],8)[0] >> 8 & 3,2)
        # Context tracking alone must not statically apply a conditional only rule.
        for at in range(0,len(history[1][1]),16):
            self.assertEqual(struct.unpack_from('<I',history[1][1],at+8)[0],3)
        prefix_next = struct.unpack_from('<I',history[1][1],4)[0]
        self.assertEqual(prefix_next,1)
        self.assertEqual(struct.unpack_from('<QII',history[2][1]),(0,0,0))
        self.assertEqual(struct.unpack_from('<QII',history[2][1],16),(0,2,0))
        self.assertEqual(struct.unpack_from('<4I',history[3][1]),(0,1,1,1))
        self.assertEqual(struct.unpack_from('<4I',history[3][1],16),(1,1,2,1))
        with self.assertRaises(ValueError): route_history_sections(graph,edges,index,max_dynamic_matches=1)
        with self.assertRaises(ValueError): route_history_sections(graph,edges,index,max_transitions=1)
        with self.assertRaises(ValueError): turn_rule_sections(graph,edges,index,max_rules=1)
        with self.assertRaises(ValueError): turn_rule_sections(graph,edges,index,max_records=0)
        with self.assertRaises(ValueError): turn_rule_sections(graph,edges,index,max_programs=0)
        bad = dict(graph,conditional_turns=[dict(rules[0],edges=['b','a'])])
        with self.assertRaises(ValueError): turn_rule_sections(bad,edges,index)
        bad = dict(graph,conditional_turns=[rules[0],dict(rules[1],tags={'restriction':'no_right_turn'})])
        with self.assertRaises(ValueError): turn_rule_sections(bad,edges,index)
        bad = dict(graph,condition_snapshot='2026-10-12T09:00:00+00:00')
        with self.assertRaises(ValueError): build({'graph':bad})

    def test_conditional_turn_none_overrides_generic(self):
        from compile_turn_conditions import compile_turn_conditions, encode_turn_program, INHERIT
        monday = parse_instant('2026-10-12T09:00:00+00:00')
        tags = {'restriction':'no_right_turn','restriction:bicycle:conditional':'none @ (Mo)'}
        resolved = resolve_tags(tags,monday,2)
        self.assertEqual(resolved['restriction:bicycle'],'none')
        compiled = compile_turn_conditions(tags,'bicycle')
        self.assertEqual(compiled[0]['fallback'],2)
        self.assertEqual(compiled[1]['fallback'],INHERIT)
        self.assertEqual(struct.unpack_from('<I',compiled[1]['records'],20)[0],0)
        descriptor,records = encode_turn_program(tags,'bicycle',48)
        self.assertEqual(len(descriptor),40)
        self.assertEqual(struct.unpack_from('<IBBH',descriptor),(1,2,0,0))
        self.assertEqual(struct.unpack_from('<QII',descriptor,24),(48,1,11))
        self.assertEqual(records,compiled[1]['records'])
        with self.assertRaises(ValueError): encode_turn_program(tags,'bicycle',max_records=0)
        with self.assertRaises(ValueError): encode_turn_program(tags,'bicycle',record_offset=0xffffffffffffffff)
        self.assertEqual(compile_turn_conditions(tags,'foot'),[{'fallback':0,'records':b''},{'fallback':INHERIT,'records':b''}])
        self.assertIsNone(compile_turn_conditions(dict(tags,**{'except':' bicycle ; foot '}),'foot'))
        with self.assertRaises(ValueError): compile_turn_conditions({'restriction:conditional':'none @ (PH)'},'bicycle')
        raw = OSM.replace('k="restriction:bicycle"','k="restriction"').replace('</relation>',
            '<tag k="restriction:bicycle:conditional" v="none @ (Mo)"/></relation>')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'turn.osm'; path.write_text(raw,encoding='utf-8')
            self.assertEqual(import_xml(path,2,monday)['restrictions'],[])
            self.assertTrue(import_xml(path,2,parse_instant('2026-10-13T09:00:00+00:00'))['restrictions'])

    def test_runtime_barrier_program_and_topology(self):
        from compile_barrier_conditions import encode_barrier_program
        from road_rule_sections import road_rule_sections
        from tmap_pack import graph_sections
        descriptor,records = encode_barrier_program({'barrier':'stile','foot:conditional':'no @ (Mo)'})
        self.assertEqual(descriptor[5],1)
        self.assertEqual(len(records),24)
        tags = {'barrier':'gate','access':'no','access:conditional':'yes @ (Mo)'}
        sections = road_rule_sections([{'barrier_tags':[tags,tags]}])
        self.assertEqual(sections[1][2],1)
        self.assertEqual(struct.unpack('<II',sections[-1][1]),(0,0))
        raw = OSM.replace('<node id="2" lon="0.001" lat="0"/>',
            '<node id="2" lon="0.001" lat="0"><tag k="barrier" v="gate"/>'
            '<tag k="access" v="no"/><tag k="access:conditional" v="yes @ (Mo)"/></node>')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'barrier.osm'; path.write_text(raw,encoding='utf-8')
            graph = import_xml(path,runtime_conditions=True)
            self.assertTrue(graph['edges'])
            self.assertTrue(all(e.get('barrier_tags') for e in graph['edges']))
            produced = graph_sections(graph)
            self.assertTrue(any(s[0] == 1119 for s in produced))
            self.assertEqual(struct.unpack_from('<I',produced[0][1],8)[0] & 131072,131072)
            self.assertTrue(build({'graph':graph})[0])
        with self.assertRaises(ValueError): encode_barrier_program({'barrier':'gate','foot:conditional':'yes @ (PH)'})

    def test_runtime_road_topology_keeps_closed_and_reverse_edges(self):
        raw = OSM.replace('<tag k="highway" v="residential"/>',
            '<tag k="highway" v="construction"/><tag k="foot" v="no"/>'
            '<tag k="foot:conditional" v="yes @ (Mo)"/>'
            '<tag k="bicycle:conditional" v="yes @ (Tu)"/>')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'runtime.osm'; path.write_text(raw,encoding='utf-8')
            graph = import_xml(path,3,runtime_conditions=True)
            dynamic = [e for e in graph['edges'] if e['id'].startswith('10:')]
            self.assertEqual(len(dynamic),4)
            self.assertEqual(sum(e['road_reverse'] for e in dynamic),2)
            self.assertTrue(all(e['access'] == 3 for e in dynamic))
            self.assertTrue(build({'graph':graph})[0])
            with self.assertRaises(ValueError): import_xml(path,3,parse_instant('2026-10-12T09:00:00+00:00'),True)
            path.write_text(raw.replace('yes @ (Mo)','yes @ (PH)'),encoding='utf-8')
            with self.assertRaises(ValueError): import_xml(path,1,runtime_conditions=True)
            self.assertTrue(import_xml(path,2,runtime_conditions=True)['edges'])

    def test_road_rule_sections_deduplicate_and_require_runtime(self):
        from road_rule_sections import road_rule_sections
        from tmap_pack import graph_sections
        tags = {'highway':'path','foot:conditional':'no @ (Mo)'}
        edge = {'id':'a','from':1,'to':2,'road_tags':tags,'road_reverse':False}
        duplicate = dict(edge,id='b')
        reverse = dict(edge,id='c',road_reverse=True)
        sections = road_rule_sections([edge,duplicate,reverse,{'id':'static'}])
        self.assertEqual(struct.unpack('<4I',sections[0][1]),(0,0,1,0xffffffff))
        self.assertEqual(sections[1][2],2)
        self.assertEqual(sections[2][2],2)
        self.assertEqual(struct.unpack_from('<Q',sections[1][1],120+8+2*16)[0],24)
        with self.assertRaises(ValueError): road_rule_sections([edge],max_programs=0)
        with self.assertRaises(ValueError): road_rule_sections([edge],max_records=0)
        graph = {'nodes':[{'id':1,'point':[0,0]},{'id':2,'point':[.001,0]}],'edges':[edge]}
        produced = graph_sections(graph)
        self.assertEqual(struct.unpack_from('<I',produced[0][1],8)[0] & 65536,65536)
        self.assertTrue({1116,1117,1118}.issubset({s[0] for s in produced}))
        self.assertTrue(build({'graph':graph})[0])

    def test_road_property_programs(self):
        from compile_road_conditions import compile_road_conditions, encode_road_program, PROPERTIES
        highway, programs = compile_road_conditions({'highway':'path','junction':'roundabout',
            'access':'no','foot':'yes','oneway:bicycle:conditional':'no @ (Mo); -1 @ (Tu)',
            'foot:conditional':'no @ (Mo 08:00-09:00,14:00-15:00)'})
        self.assertEqual(highway,3)
        self.assertEqual(tuple(p['property'] for p in programs),PROPERTIES)
        self.assertEqual([p['fallback'] for p in programs],[1,0,2,0,2,0,1])
        self.assertEqual(len(programs[2]['records']),48)
        self.assertEqual(struct.unpack_from('<II',programs[2]['records'],16),(0,1))
        self.assertEqual(struct.unpack_from('<II',programs[2]['records'],40),(0,1))
        self.assertEqual(struct.unpack_from('<II',programs[5]['records'],16),(0,1))
        self.assertEqual(struct.unpack_from('<II',programs[5]['records'],40),(1,3))
        for key,value in (('foot','unknown'),('oneway','reversible'),('foot:conditional','yes @ (PH)'),
                          ('access:conditional','magical @ (Tu)')):
            with self.assertRaises(ValueError): compile_road_conditions({key:value})
        descriptor,records = encode_road_program({'highway':'path','foot:conditional':'no @ (Mo)'},True,48)
        self.assertEqual(len(descriptor),120)
        self.assertEqual(len(records),24)
        self.assertEqual(struct.unpack_from('<IBBBB',descriptor),(1,0,3,1,0))
        self.assertEqual(struct.unpack_from('<QII',descriptor,8+2*16),(48,1,0))
        self.assertEqual(struct.unpack_from('<QII',descriptor,8+3*16),(72,0,0))
        with self.assertRaises(ValueError): encode_road_program({'foot:conditional':'no @ (Mo)'},max_records=0)
        with self.assertRaises(ValueError): encode_road_program({'foot:conditional':'no @ (Mo)'},record_offset=0xffffffffffffffff)

    def test_conditional_barrier_snapshot_and_mode_precedence(self):
        raw = '<osm version="0.6">'
        raw += '<node id="1" lon="0" lat="0"/>'
        raw += '<node id="2" lon="0.001" lat="0"><tag k="barrier" v="gate"/>{}</node>'
        raw += '<node id="3" lon="0.002" lat="0"/><node id="4" lon="0.003" lat="0"/>'
        raw += '<way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="path"/></way>'
        raw += '<way id="11"><nd ref="3"/><nd ref="4"/><tag k="highway" v="path"/></way></osm>'
        monday = parse_instant('2026-10-12T09:00:00+02:00')
        tuesday = parse_instant('2026-10-13T09:00:00+02:00')
        def main_edges(graph): return [e for e in graph['edges'] if e['id'].startswith('10:')]
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'gate.osm'
            path.write_text(raw.format('<tag k="access:conditional" v="no @ (Mo)"/>'))
            with self.assertRaisesRegex(ValueError,'conditional barrier'): import_xml(path)
            self.assertEqual(main_edges(import_xml(path,3,monday)),[])
            self.assertEqual(len(main_edges(import_xml(path,3,tuesday))),4)
            path.write_text(raw.format('<tag k="vehicle:conditional" v="no @ (Mo)"/>'))
            self.assertTrue(all(e['access'] == 1 for e in main_edges(import_xml(path,3,monday))))
            self.assertEqual(len(main_edges(import_xml(path,1))),4)
            with self.assertRaisesRegex(ValueError,'conditional barrier'): import_xml(path,2)
            path.write_text(raw.format('<tag k="vehicle" v="no"/><tag k="bicycle" v="yes"/>'))
            self.assertTrue(all(e['access'] == 3 for e in main_edges(import_xml(path))))
            path.write_text(raw.format('<tag k="foot:conditional" v="magical @ (Tu)"/>'))
            with self.assertRaises(ValueError): import_xml(path,1,monday)

    def test_conditional_override_on_nondefault_highway(self):
        for mode,key in ((1,'foot'),(2,'bicycle')):
            road = {'highway':'construction',key+':conditional':'yes @ (Mo)'}
            with self.assertRaises(ValueError): access(road,mode)
            self.assertEqual(access(road,3-mode),0)
            resolved = resolve_tags(road,parse_instant('2026-10-12T09:00:00+00:00'),mode)
            self.assertEqual(access(resolved,mode),mode)

    def test_via_way_history_export(self):
        raw = via_osm()
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'via.osm'; path.write_text(raw)
            graph = import_xml(path,3)
            self.assertEqual(graph['via_restrictions'],[{'edges':['10:0:f','11:0:f','12:0:f'],'access':1,'kind':'no'}])
            from tmap_pack import graph_sections
            self.assertTrue({1114,1115}.issubset({s[0] for s in graph_sections(graph)}))
            path.write_text(raw.replace('no_straight_on','only_straight_on'))
            self.assertEqual(import_xml(path,3)['via_restrictions'][0]['kind'],'only')
            legacy = raw.replace('k="type" v="restriction"','k="type" v="restriction:foot"').replace('k="restriction:foot"','k="restriction"')
            path.write_text(legacy)
            self.assertEqual(import_xml(path,3)['via_restrictions'][0]['access'],1)
            path.write_text(raw.replace('ref="11" role="via"','ref="999" role="via"'))
            with self.assertRaises(ValueError): import_xml(path,3)
    def test_selected_modes_keep_relevant_rules(self):
        # A time rule may reopen a statically denied road. Reject explicitly
        # instead of emitting an incomplete graph that appears fully valid.
        for key, mode, denied in (("foot",1,"no"),("bicycle",2,"private")):
            road = {"highway":"path", key:denied, key+":conditional":"yes @ (Mo-Fr)"}
            with self.assertRaises(ValueError): access(road,mode)
            self.assertEqual(access(road,3-mode),3-mode)
        with self.assertRaises(ValueError):
            access({"highway":"path","access":"no","access:conditional":"yes @ (summer)"},1)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"profile.osm"
            path.write_text(OSM,encoding="utf-8")
            foot = import_xml(path,1)
            self.assertTrue(all(e["access"] == 1 for e in foot["edges"]))
            self.assertEqual(foot["restrictions"],[])
            bike = import_xml(path,2)
            self.assertTrue(all(e["access"] == 2 for e in bike["edges"]))
            self.assertTrue(bike["restrictions"])
            path.write_text(OSM.replace('restriction:bicycle','restriction:foot'),encoding="utf-8")
            self.assertTrue(import_xml(path,1)["restrictions"])
            path.write_text(OSM.replace('<tag k="oneway" v="yes"/>','<tag k="bicycle:conditional" v="no @ (night)"/>'),encoding="utf-8")
            self.assertTrue(import_xml(path,1)["edges"])
            with self.assertRaises(ValueError): import_xml(path,2)

    def test_weekly_condition_snapshot(self):
        monday = parse_instant("2026-10-12T09:00:00+02:00")
        self.assertTrue(matches("Mo-Fr 08:00-10:00",monday))
        self.assertFalse(matches("Mo-Fr 08:00-09:00",monday))
        self.assertTrue(matches("Fr-Mo",monday))
        self.assertTrue(matches("Mo,We 08:00-10:00,12:00-14:00",monday))
        for condition in ("PH", "sunrise-sunset", "Mo 06:00-06:00", "Mo 24:00-06:00", "Mo 25:00-26:00", "Mo-Tu-We"):
            with self.assertRaises(ValueError): matches(condition,monday)
        for stamp,expected in (("2026-10-12T21:59:59+02:00",False),
                               ("2026-10-12T22:00:00+02:00",True),
                               ("2026-10-13T05:59:59+02:00",True),
                               ("2026-10-13T06:00:00+02:00",False),
                               ("2026-10-12T05:00:00+02:00",False),
                               ("2026-10-13T22:00:00+02:00",False)):
            self.assertEqual(matches("Mo 22:00-06:00",parse_instant(stamp)),expected)
        self.assertTrue(matches("Su 22:00-06:00",parse_instant("2026-10-12T05:00:00+02:00")))
        self.assertTrue(matches("Mo-Fr 22:00-06:00",parse_instant("2026-10-17T05:00:00+02:00")))
        self.assertFalse(matches("Mo-Fr 22:00-06:00",parse_instant("2026-10-17T22:00:00+02:00")))
        with self.assertRaises(ValueError): parse_instant("2026-10-12T09:00:00")
        self.assertEqual(resolve_tags({"foot":"no","foot:conditional":"yes @ (Mo); no @ (Mo 08:00-10:00)"},monday,1),{"foot":"no"})
        with self.assertRaises(ValueError):
            resolve_tags({"foot:conditional":"yes @ (Mo); magical @ (Tu)"},monday,1)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"conditions.osm"
            document = OSM.replace('<tag k="highway" v="residential"/>',
                '<tag k="highway" v="residential"/><tag k="foot" v="no"/><tag k="foot:conditional" v="yes @ (Mo-Fr 08:00-10:00)"/>')
            path.write_text(document,encoding="utf-8")
            opened = import_xml(path,1,monday)
            closed = import_xml(path,1,parse_instant("2026-10-12T10:00:00+02:00"))
            self.assertTrue(any(e["id"].startswith("10:") for e in opened["edges"]))
            self.assertFalse(any(e["id"].startswith("10:") for e in closed["edges"]))
            self.assertTrue(build({"graph":opened})[0])
            path.write_text(document.replace("Mo-Fr 08:00-10:00","Mo-Fr 22:00-06:00"),encoding="utf-8")
            saturday = import_xml(path,1,parse_instant("2026-10-17T05:00:00+02:00"))
            expired = import_xml(path,1,parse_instant("2026-10-17T06:00:00+02:00"))
            self.assertTrue(any(e["id"].startswith("10:") for e in saturday["edges"]))
            self.assertFalse(any(e["id"].startswith("10:") for e in expired["edges"]))
            self.assertTrue(build({"graph":saturday})[0])
            self.assertEqual(opened["condition_snapshot"],monday.isoformat())
            opened["topology_epoch"] = closed["topology_epoch"] = "same-source"
            with self.assertRaises(ValueError): merge_graphs([opened,closed])
            compatible = merge_graphs([opened,opened])
            self.assertEqual(compatible["condition_snapshot"],monday.isoformat())
            self.assertEqual(compatible["travel_modes"],1)
            changed_mode = dict(opened,travel_modes=2)
            with self.assertRaises(ValueError): merge_graphs([opened,changed_mode])
            missing_snapshot = dict(opened)
            del missing_snapshot["condition_snapshot"]
            with self.assertRaises(ValueError): merge_graphs([opened,missing_snapshot])

    def test_month_conditions_and_midnight_boundaries(self):
        cases = [("May-Oct","2026-10-31T12:00:00+02:00",True),
                 ("May-Oct","2026-11-01T12:00:00+02:00",False),
                 ("Nov-Mar","2027-01-01T12:00:00+02:00",True),
                 ("Nov-Mar","2027-04-01T12:00:00+02:00",False),
                 ("Jan,Mar-May","2028-02-29T12:00:00+02:00",False),
                 ("Jun Mo-Fr 22:00-06:00","2026-07-01T05:59:59+02:00",True),
                 ("Jun Mo-Fr 22:00-06:00","2026-07-01T06:00:00+02:00",False),
                 ("Jun Mo-Fr 22:00-06:00","2026-07-01T22:00:00+02:00",False),
                 ("Dec 22:00-06:00","2027-01-01T05:00:00+02:00",True)]
        for condition,stamp,expected in cases:
            self.assertEqual(matches(condition,parse_instant(stamp)),expected,(condition,stamp))
        instant = parse_instant("2026-06-30T23:00:00+02:00")
        self.assertTrue(matches("24/7",instant))
        self.assertTrue(matches("Jun Mo - Fr 22:00 - 06:00",instant))
        self.assertEqual(resolve_tags({"foot":"no","foot:conditional":"yes @ May-Oct"},instant,1),{"foot":"yes"})
        for condition in ("winter","Jun 32-Oct 01","Jan-Feb-Mar","Mo Jan","May-Oct PH"):
            with self.assertRaises(ValueError): matches(condition,instant)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"seasonal.osm"
            path.write_text(OSM.replace('<tag k="highway" v="residential"/>',
                '<tag k="highway" v="residential"/><tag k="foot" v="no"/><tag k="foot:conditional" v="yes @ May-Oct"/>'),encoding="utf-8")
            summer = import_xml(path,1,instant)
            winter = import_xml(path,1,parse_instant("2026-11-01T12:00:00+02:00"))
            self.assertTrue(any(e["id"].startswith("10:") for e in summer["edges"]))
            self.assertFalse(any(e["id"].startswith("10:") for e in winter["edges"]))
            self.assertTrue(build({"graph":summer})[0])

    def test_explicit_date_ranges(self):
        cases = [("Jun 01-Oct 01","2026-06-01T00:00:00+02:00",True),
                 ("Jun 01-Oct 01","2026-10-01T23:59:59+02:00",True),
                 ("Jun 01-Oct 01","2026-10-02T00:00:00+02:00",False),
                 ("Dec 15-Jan 10","2027-01-01T12:00:00+02:00",True),
                 ("Feb 29","2028-02-29T12:00:00+02:00",True),
                 ("Feb 29","2027-02-28T12:00:00+02:00",False),
                 ("2026 Jun 01-15","2026-06-15T12:00:00+02:00",True),
                 ("2026 Jun 01-15","2027-06-15T12:00:00+02:00",False),
                 ("2026 Dec 31-2027 Jan 02","2027-01-02T12:00:00+02:00",True),
                 ("2026 Dec 31-2027 Jan 02","2027-01-03T12:00:00+02:00",False),
                 ("Jun 30 Tu 22:00-06:00","2026-07-01T05:59:59+02:00",True),
                 ("Jun 30 Tu 22:00-06:00","2026-07-01T06:00:00+02:00",False)]
        for condition,stamp,expected in cases:
            self.assertEqual(matches(condition,parse_instant(stamp)),expected,(condition,stamp))
        instant = parse_instant("2026-06-15T12:00:00+02:00")
        for condition in ("Feb 30","2027 Feb 29","2026 Jun 15-01","Jun 01-2027 Oct 01","2026 Dec 31-Jan 02"):
            with self.assertRaises(ValueError): matches(condition,instant)
        with self.assertRaises(ValueError):
            resolve_tags({"foot:conditional":"yes @ (Jun 01-15); no @ (2027 Feb 29)"},instant,1)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"dated.osm"
            path.write_text(OSM.replace('<tag k="highway" v="residential"/>',
                '<tag k="highway" v="residential"/><tag k="foot" v="no"/><tag k="foot:conditional" v="yes @ (2026 Jun 01-15)"/>'),encoding="utf-8")
            active = import_xml(path,1,instant)
            inactive = import_xml(path,1,parse_instant("2027-06-15T12:00:00+02:00"))
            self.assertTrue(any(e["id"].startswith("10:") for e in active["edges"]))
            self.assertFalse(any(e["id"].startswith("10:") for e in inactive["edges"]))
            self.assertTrue(build({"graph":active})[0])
    def test_multi_member_no_entry_exit(self):
        extra = '<way id="12"><nd ref="2"/><nd ref="3"/><tag k="highway" v="path"/></way>'
        base = OSM.replace('<relation id="20">',extra+'<relation id="20">')
        variants = [
            base.replace('v="no_right_turn"','v="no_entry"').replace('<member type="way" ref="10" role="from"/>',
                '<member type="way" ref="10" role="from"/><member type="way" ref="12" role="from"/>'),
            base.replace('v="no_right_turn"','v="no_exit"').replace('<member type="way" ref="11" role="to"/>',
                '<member type="way" ref="11" role="to"/><member type="way" ref="12" role="to"/>')]
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"sample.osm"
            for index,document in enumerate(variants):
                path.write_text(document,encoding="utf-8")
                graph = import_xml(path)
                self.assertEqual(len(graph["restrictions"]),2)
                self.assertEqual({r["from"].split(":")[0] for r in graph["restrictions"]},
                                 {"10","12"} if index == 0 else {"10"})
                self.assertEqual({r["to"].split(":")[0] for r in graph["restrictions"]},
                                 {"11"} if index == 0 else {"11","12"})
                self.assertTrue(build({"graph":graph})[0])

    def test_invalid_restriction_members_and_dynamic_kind(self):
        bad = [
            OSM.replace('<member type="way" ref="10" role="from"/>',''),
            OSM.replace('<member type="node" ref="2" role="via"/>',
                '<member type="node" ref="2" role="via"/><member type="node" ref="3" role="via"/>'),
            OSM.replace('<member type="way" ref="10" role="from"/>',
                '<member type="way" ref="10" role="from"/><member type="way" ref="10" role="from"/>'),
            OSM.replace('v="no_right_turn"','v="no_right_turn_on_red"'),
            OSM.replace('<member type="node" ref="2" role="via"/>','<member type="node" ref="1" role="via"/>'),
            OSM.replace('<member type="way" ref="10" role="from"/>','<member type="node" ref="10" role="from"/>')]
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"sample.osm"
            for document in bad:
                path.write_text(document,encoding="utf-8")
                with self.assertRaises(ValueError): import_xml(path)

    def test_dem_orientation_and_nodata(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"sample.asc"
            path.write_text("ncols 2\nnrows 2\nxllcorner 0\nyllcorner 0\ncellsize 0.001\nNODATA_value -9999\n30 -9999\n10 20\n")
            with self.assertRaises(ValueError): read_ascii(path, resolution_m=30, source="test", vertical_datum=1)
            doc = read_ascii(path, resolution_m=30, source="test", vertical_datum=1, geographic=True)
            self.assertEqual(doc["dem"]["origin"], [0.0005, 0.0005])
            self.assertEqual(doc["dem"]["heights"], [[10, 20], [30, None]])
            self.assertTrue(build(doc)[0])

    def test_junction_oneway_and_restriction(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"sample.osm"; path.write_text(OSM, encoding="utf-8")
            graph = import_xml(path)
            self.assertEqual(len(graph["nodes"]), 4)
            self.assertEqual(len(graph["edges"]), 6)
            self.assertEqual([e["access"] for e in graph["edges"] if e["id"].startswith("10:")], [3, 1, 3, 1])
            self.assertEqual(graph["restrictions"][0]["access"], 2)
            self.assertTrue(build({"graph": graph})[0])

    def test_unsupported_rule_fails(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/"sample.osm"
            path.write_text(OSM.replace('<tag k="oneway" v="yes"/>', '<tag k="access:conditional" v="no @ (night)"/>'), encoding="utf-8")
            with self.assertRaises(ValueError): import_xml(path)

    def test_cycleway_and_mode_overrides(self):
        from import_osm import access
        self.assertEqual(access({"highway":"cycleway"}), 2)
        self.assertEqual(access({"highway":"path","access":"private","foot":"yes"}),1)
        self.assertEqual(access({"highway":"residential","vehicle":"no"}),1)

    def test_regions_require_explicit_compatibility(self):
        a = {"topology_epoch":"snapshot-A", "nodes":[{"id":1,"point":[0,0]},{"id":2,"point":[0.001,0]}],
             "edges":[{"id":"a","from":1,"to":2}]}
        b = {"topology_epoch":"snapshot-A", "nodes":[{"id":2,"point":[0.001,0]},{"id":3,"point":[0.002,0]}],
             "edges":[{"id":"b","from":2,"to":3}]}
        combined = merge_graphs([a,b])
        self.assertEqual(len(combined["nodes"]),3)
        self.assertTrue(build({"graph":combined})[0])
        b["topology_epoch"] = "snapshot-B"
        with self.assertRaises(ValueError): merge_graphs([a,b])
        b["topology_epoch"] = "snapshot-A"; b["nodes"][0]["point"] = [1,1]
        with self.assertRaises(ValueError): merge_graphs([a,b])

    def test_geojson_administration(self):
        ring = [[0,0],[1,0],[1,1],[0,1],[0,0]]
        doc = {"type":"FeatureCollection","features":[{"properties":{"country":"A","province":"B"},
                "geometry":{"type":"MultiPolygon","coordinates":[[ring],[ring]]}}]}
        converted = convert(doc,["country","province"])
        self.assertEqual(len(converted),2)
        self.assertEqual(converted[0]["path"],["A","B"])
        self.assertTrue(build({"administration":converted})[0])
        with self.assertRaises(ValueError): convert(doc,["absent"])
        with self.assertRaises(ValueError): convert(doc,["country"],["country","absent"])
        doc["crs"] = {"properties":{"name":"EPSG:3857"}}
        with self.assertRaises(ValueError): convert(doc,["country"])

    def test_hgt_north_south_orientation(self):
        with tempfile.TemporaryDirectory() as root:
            side = 1201; raw = bytearray(side*side*2)
            for x,y,z in ((600,600,100),(601,600,110),(600,601,130),(601,601,140)):
                struct.pack_into(">h",raw,((1200-y)*side+x)*2,z)
            path = Path(root)/"N00E000.hgt.gz"; path.write_bytes(gzip.compress(raw))
            doc = read_hgt(path,(0.5,0.5,0.50083,0.50083),source="synthetic",vertical_datum=1)
            self.assertEqual(doc["dem"]["heights"],[[100,110],[130,140]])
            self.assertTrue(build(doc)[0])


if __name__ == "__main__": unittest.main()
