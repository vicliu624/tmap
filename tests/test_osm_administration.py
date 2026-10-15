import argparse
import copy
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET
parser=argparse.ArgumentParser(add_help=False)
parser.add_argument('--geometry-python-path')
options,remaining=parser.parse_known_args()
sys.argv=[sys.argv[0],*remaining]
if options.geometry_python_path: sys.path.insert(0,options.geometry_python_path)
try:
    import shapely
    HAS_GEOMETRY=True
except ImportError:
    if options.geometry_python_path: raise
    HAS_GEOMETRY=False
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from import_osm_administration import stitch,convert
from tmap_pack import build


def records():
    root=ET.Element('osm')
    groups=[([(0,0),(4,0),(4,4),(0,4)],10,'outer'),
            ([(1,1),(2,1),(2,2),(1,2)],20,'outer'),
            ([(1.2,1.2),(1.8,1.2),(1.8,1.8),(1.2,1.8)],21,'inner')]
    next_node=1
    for points,way,role in groups:
        refs=list(range(next_node,next_node+4)); next_node+=4
        for identity,(lon,lat) in zip(refs,points): ET.SubElement(root,'node',id=str(identity),lon=str(lon),lat=str(lat))
        element=ET.SubElement(root,'way',id=str(way))
        for identity in [*refs,refs[0]]: ET.SubElement(element,'nd',ref=str(identity))
    for identity,name,level,members in [(100,'Country',2,[(10,'outer')]),(200,'District',6,[(20,'outer'),(21,'inner')])]:
        relation=ET.SubElement(root,'relation',id=str(identity))
        for way,role in members: ET.SubElement(relation,'member',type='way',ref=str(way),role=role)
        for key,value in [('name',name),('admin_level',str(level)),('boundary','administrative'),('type','boundary')]: ET.SubElement(relation,'tag',k=key,v=value)
    return list(root)


class StitchTests(unittest.TestCase):
    def test_direction_independent_exact_node_stitch(self):
        self.assertEqual(stitch({1:(1,2),2:(3,2),3:(3,4),4:(1,4)}),[[1,2,3,4,1]])

    def test_touching_rings_separate_by_stable_articulation(self):
        rings=stitch({1:(1,2),2:(2,3,1),3:(1,4,5),4:(5,1)})
        self.assertEqual(rings,[[1,2,3,1],[1,4,5,1]])

    def test_open_and_nonseparable_branch_rejected(self):
        with self.assertRaisesRegex(ValueError,'open administrative'):
            stitch({1:(1,2),2:(2,3)})
        with self.assertRaisesRegex(ValueError,'nonseparable'):
            stitch({1:(1,2),2:(1,3,2),3:(1,4,2),4:(1,5,2)})


@unittest.skipUnless(HAS_GEOMETRY,'optional host Shapely dependency')
class BoundaryTests(unittest.TestCase):
    def test_holes_hierarchy_and_sdk_writer(self):
        features,report=convert(records(),[100,200],100)
        self.assertEqual(report['parents'],{100:None,200:100})
        self.assertEqual(features[1]['path'],['Country','District'])
        self.assertEqual(len(features[1]['rings']),2)
        self.assertTrue(build({'administration':features})[0])

    def test_missing_way_node_and_budgets_fail(self):
        source=records()
        for kind,identity in [('way','10'),('node','1')]:
            with self.assertRaisesRegex(ValueError,'missing'):
                convert([e for e in source if not (e.tag==kind and e.attrib['id']==identity)],[100,200],100)
        with self.assertRaisesRegex(ValueError,'entity budget'):
            convert(source,[100,200],100,max_entities=1)
        with self.assertRaisesRegex(ValueError,'reference budget'):
            convert(source,[100,200],100,max_references=1)
        with self.assertRaisesRegex(ValueError,'vertex budget'):
            convert(source,[100,200],100,max_vertices=4)

    def test_nested_relation_cycle_and_root_containment(self):
        source=records()
        child=next(e for e in source if e.tag=='relation' and e.attrib['id']=='200')
        ET.SubElement(child,'member',type='relation',ref='200',role='outer')
        with self.assertRaisesRegex(ValueError,'cyclic'):
            convert(source,[100,200],100)
        source=records()
        for e in source:
            if e.tag=='node' and e.attrib['id'] in ('5','6','7','8'):
                e.set('lon',str(float(e.attrib['lon'])+10))
        with self.assertRaisesRegex(ValueError,'hole|outside'):
            convert(source,[100,200],100)


if __name__=='__main__': unittest.main()
