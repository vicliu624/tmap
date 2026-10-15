from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from import_osm import import_records
from merge_osm_regions import union_records, import_regions
from merge_regions import merge_graphs
from tmap_pack import build
from virtual_regions import prepare
from test_pbf import fixture

WEST = '''<osm><node id="1" lon="0" lat="0"/><node id="2" lon=".001" lat="0"/>
<node id="3" lon=".002" lat="0"/><way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/>
<tag k="highway" v="path"/></way></osm>'''
EAST = '''<osm><node id="2" lon=".001" lat="0"/><node id="4" lon=".001" lat=".001"/>
<way id="11"><nd ref="2"/><nd ref="4"/><tag k="highway" v="path"/></way></osm>'''


def elements(text):
    return list(ET.fromstring(text))


class RawRegionTests(unittest.TestCase):
    def test_mixed_pbf_xml_sources(self):
        with tempfile.TemporaryDirectory() as root:
            a,b = Path(root)/'west.pbf',Path(root)/'east.osm'
            b.write_text(EAST)
            for dense in (False,True):
                a.write_bytes(fixture(dense,xml=WEST))
                graph,_ = import_regions([a,b],1)
                expected = import_records(union_records([elements(WEST),elements(EAST)]),1)
                self.assertEqual(graph,expected)

    def test_virtual_patch_rebuilds_and_verifies_raw_seam(self):
        with tempfile.TemporaryDirectory() as root:
            a,b = Path(root)/'west.osm',Path(root)/'east.osm'
            a.write_text(WEST); b.write_text(EAST)
            documents = [{'graph':dict(import_records(elements(text),1),topology_epoch='source',landmarks=2),'page_bytes':1024}
                         for text in (WEST,EAST)]
            packages = [build(d)[0] for d in documents]
            patch,manifest,reference = prepare(documents,packages,1024,raw_source_groups=[[a],[b]])
            graph = dict(import_records(union_records([elements(WEST),elements(EAST)]),1),topology_epoch='source',landmarks=2)
            expected = build({'name':'virtual-regions','graph':graph,'page_bytes':1024})[0]
            self.assertEqual(reference,expected)
            files = packages+[patch]
            self.assertEqual(b''.join(files[e['source']][e['source_offset']:e['source_offset']+e['bytes']] for e in manifest['extents']),reference)
            self.assertEqual(len(manifest['raw_seam_sources']),2)
            b.write_text(EAST.replace('lat=".001"','lat=".002"'))
            with self.assertRaisesRegex(ValueError,'raw sources do not reproduce regional graph semantics'):
                prepare(documents,packages,1024,raw_source_groups=[[a],[b]])

    def test_seam_requires_union_before_segmentation(self):
        west = import_records(elements(WEST), 1)
        east = import_records(elements(EAST), 1)
        self.assertNotIn(2, {n['id'] for n in west['nodes']})
        self.assertIn(2, {n['id'] for n in east['nodes']})
        graph = import_records(union_records([elements(WEST), elements(EAST)]), 1)
        self.assertEqual({n['id'] for n in graph['nodes']}, {1,2,3,4})
        self.assertTrue(any(e['from']==1 and e['to']==2 for e in graph['edges']))
        self.assertTrue(any(e['from']==2 and e['to']==4 for e in graph['edges']))
        self.assertEqual(graph, import_records(union_records([elements(EAST), elements(WEST)]), 1))
        self.assertTrue(build({'graph':graph})[0])

    def test_grade_separation_does_not_join_equal_coordinates(self):
        elevated = EAST.replace('id="2"', 'id="5"').replace('ref="2"', 'ref="5"')
        graph = import_records(union_records([elements(WEST), elements(elevated)]), 1)
        self.assertNotIn(2, {n['id'] for n in graph['nodes']})
        self.assertFalse(any(e['from']==1 and e['to']==5 for e in graph['edges']))

    def test_conflicts_and_explicit_limits(self):
        for changed in (WEST.replace('lon=".001"', 'lon=".0011"'),
                        WEST.replace('v="path"', 'v="steps"'),
                        WEST.replace('<nd ref="2"/><nd ref="3"/>', '<nd ref="3"/><nd ref="2"/>')):
            with self.assertRaisesRegex(ValueError, 'conflicting regional OSM'):
                list(union_records([elements(WEST), elements(changed)]))
        with self.assertRaisesRegex(ValueError, 'entity budget exceeded'):
            list(union_records([elements(WEST),elements(EAST)], max_entities=5))
        with self.assertRaisesRegex(ValueError, 'reference budget exceeded'):
            list(union_records([elements(WEST),elements(EAST)], max_references=4))
        self.assertEqual(len(list(union_records([elements(WEST),elements(WEST)], max_entities=4, max_references=3))),4)
        with self.assertRaisesRegex(ValueError, 'duplicate regional OSM tag'):
            list(union_records([elements(WEST.replace('</way>','<tag k="highway" v="path"/></way>'))]))

    def test_cross_source_conditional_relation_and_group_merge(self):
        relation = '''<relation id="20"><member type="way" ref="10" role="from"/>
        <member type="node" ref="2" role="via"/><member type="way" ref="11" role="to"/>
        <tag k="type" v="restriction"/><tag k="restriction:foot:conditional" v="no_right_turn @ (Mo)"/></relation>'''
        graph = import_records(union_records([elements(WEST), elements(EAST.replace('</osm>',relation+'</osm>'))]), 1, runtime_conditions=True)
        self.assertEqual(len(graph['conditional_turns']),2)
        graph.update(topology_epoch='same-source',landmarks=2)
        combined = merge_graphs([graph,graph])
        self.assertEqual(combined['conditional_turns'],graph['conditional_turns'])
        self.assertEqual(combined['landmarks'],2)
        self.assertTrue(build({'graph':combined})[0])
        changed = dict(graph,conditional_turns=[dict(r,tags={'restriction:foot:conditional':'no_right_turn @ (Tu)'}) for r in graph['conditional_turns']])
        with self.assertRaisesRegex(ValueError,'shared conditional turn group differs'):
            merge_graphs([graph,changed])
        with self.assertRaisesRegex(ValueError,'conflicting regional OSM relation'):
            list(union_records([elements('<osm>'+relation+'</osm>'),elements('<osm>'+relation.replace('role="from"','role="via"')+'</osm>')]))

    def test_identity_provenance(self):
        with tempfile.TemporaryDirectory() as root:
            a,b = Path(root)/'west.osm',Path(root)/'east.osm'
            a.write_text(WEST); b.write_text(EAST)
            graph, sources = import_regions([a,b],1)
            self.assertEqual(len(graph['nodes']),4)
            self.assertEqual(len(sources),2)
            self.assertTrue(all(len(s['sha256'])==64 and s['bytes']>0 for s in sources))
            with self.assertRaisesRegex(ValueError,'distinct source paths'):
                import_regions([a,a],1)


if __name__ == '__main__':
    unittest.main()
