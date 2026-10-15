"""Optional geometry dependency tests, including an actual SDK polygon query."""
import argparse
import hashlib
import json
from datetime import datetime, timezone
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from union_regional_administration import union_administration
from tmap_pack import build
from shapely.geometry import Point, Polygon
from virtual_regions import prepare
from stitch_regional_contours import stitch_contours


def feature(west, south, east, north, path=('country', 'district')):
    return {'path':list(path), 'rings':[[[west,south],[east,south],[east,north],[west,north],[west,south]]]}


class UnionTests(unittest.TestCase):
    def test_contour_noding(self):
        a = {'id':1,'elevation_m':20,'points':[[0,0],[.003,0]]}
        b = {'id':2,'elevation_m':20,'points':[[.001,0],[.002,0]]}
        stitched = stitch_contours([a,b],node_intersections=True)
        self.assertEqual(len(stitched),1)
        self.assertEqual(stitched[0]['points'],[[0.,0.],[.001,0.],[.002,0.],[.003,0.]])
        self.assertEqual(stitched,stitch_contours([b,a],node_intersections=True))
        crossing = dict(b,points=[[.001,-.001],[.001,.001]])
        noded = stitch_contours([a,crossing],node_intersections=True)
        self.assertEqual(len(noded),4)
        self.assertEqual(sum(len(p['points'])-1 for p in noded),4)
        with self.assertRaises(ValueError): stitch_contours([a,crossing],3,node_intersections=True)
        self.assertEqual(len(stitch_contours([a,dict(crossing,elevation_m=40)],node_intersections=True)),2)
    def test_seam_and_order(self):
        a, b = feature(0,0,.001,.001), feature(.001,0,.002,.001)
        merged = union_administration([a,b])
        self.assertEqual(len(merged),1)
        self.assertEqual(merged,union_administration([b,a,a]))
        geometry = Polygon([(p[1],p[0]) for p in merged[0]['rings'][0]])
        self.assertTrue(geometry.contains(Point(.0005,.001)))
        self.assertFalse(geometry.boundary.contains(Point(.0005,.001)))
        self.assertEqual(a,feature(0,0,.001,.001))

    def test_holes_islands_and_hierarchies(self):
        outer = feature(0,0,.004,.004)
        outer['rings'].append(feature(.001,.001,.003,.003)['rings'][0])
        outer['rings'].append(feature(.0015,.0015,.0025,.0025)['rings'][0])
        merged = union_administration([outer])
        self.assertEqual(len(merged),2)
        self.assertEqual(sorted(len(p['rings']) for p in merged),[1,2])
        distinct = union_administration([feature(0,0,.001,.001),feature(.001,0,.002,.001,('country','other'))])
        self.assertEqual(len(distinct),2)
        disjoint = union_administration([feature(0,0,.001,.001),feature(.002,0,.003,.001)])
        self.assertEqual(len(disjoint),2)

    def test_invalid_and_limits(self):
        with self.assertRaises(ValueError): union_administration([feature(0,0,.001,.001)],4)
        broken = feature(0,0,.001,.001)
        broken['rings'][0] = [[0,0],[.001,.001],[0,.001],[.001,0],[0,0]]
        with self.assertRaises(ValueError): union_administration([broken])
        overlap = union_administration([feature(0,0,.002,.001),feature(.001,0,.003,.001)])
        self.assertEqual(len(overlap),1)

    def test_virtual_production(self):
        a = {'graph':{'topology_epoch':'union','nodes':[{'id':1,'point':[0,0]},{'id':2,'point':[.001,0]}],
                     'edges':[{'id':'a','from':1,'to':2}]}, 'administration':[feature(0,0,.001,.001)]}
        b = copy.deepcopy(a)
        b['administration'] = [feature(.001,0,.002,.001)]
        a['contours'] = [{'id':1,'elevation_m':20,'points':[[0,0],[.003,0]]}]
        b['contours'] = [{'id':1,'elevation_m':20,'points':[[.001,0],[.002,0]]}]
        sources = [build(d)[0] for d in (a,b)]
        patch, manifest, raw = prepare([a,b],sources,1024,administration_union=True,contour_noding=True)
        sources.append(patch)
        reconstructed = b''.join(sources[e['source']][e['source_offset']:e['source_offset']+e['bytes']] for e in manifest['extents'])
        self.assertEqual(reconstructed,raw)
        self.assertTrue(manifest['administration_union'])
        self.assertTrue(manifest['contour_noding'])
        self.assertIn('geos',manifest['administration_union_engine'])

    def test_sdk_internal_seam(self):
        if inspector is None: self.skipTest('explicit SDK inspector required')
        merged = union_administration([feature(0,0,.001,.001),feature(.001,0,.002,.001)])
        lines = [{'id':1,'elevation_m':20,'points':[[0,0],[.003,0]]},
                 {'id':2,'elevation_m':20,'points':[[.001,0],[.002,0]]}]
        contours = stitch_contours(lines,node_intersections=True)
        raw,_ = build({'name':'union-test','administration':merged,'contours':contours,'page_bytes':1024})
        with tempfile.TemporaryDirectory(prefix='tmap-admin-union-') as directory:
            package = Path(directory) / 'map.tmap'
            package.write_bytes(raw)
            result = subprocess.run([str(inspector),str(package),'region','.001','.0005'],capture_output=True,text=True,check=True)
            self.assertIn('region=country / district',result.stdout)
            self.assertIn('boundary=0',result.stdout)
            self.assertIn('result=ok',result.stdout)
            drawing = json.loads(subprocess.run([str(inspector),str(package),'measure-contours','.001','.0005','16'],capture_output=True,text=True,check=True).stdout)
            self.assertEqual(drawing['result'],'ok')
            self.assertGreater(drawing['segments'],0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--inspector',type=Path)
    parser.add_argument('--evidence',type=Path)
    arguments,remaining = parser.parse_known_args()
    inspector = arguments.inspector.resolve() if arguments.inspector else None
    program = unittest.main(argv=[sys.argv[0],*remaining],exit=False)
    if not program.result.wasSuccessful(): sys.exit(1)
    if arguments.evidence:
        import shapely
        root = Path(__file__).resolve().parents[1]
        report = {'date_utc':datetime.now(timezone.utc).isoformat(), 'tests':program.result.testsRun,
                  'skipped':len(program.result.skipped), 'shapely':shapely.__version__, 'geos':shapely.geos_version_string,
                  'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in
                      ('tools/union_regional_administration.py','tools/stitch_regional_contours.py','tests/test_regional_admin_union.py')},
                  'inspector_sha256':hashlib.sha256(inspector.read_bytes()).hexdigest() if inspector else None,
                  'scope':'Synthetic producer geometry and actual SDK internal-seam query. Not real regional survey, physical MCU or host native GEOS peak memory measurement.'}
        arguments.evidence.write_text(json.dumps(report,indent=2)+'\n')
