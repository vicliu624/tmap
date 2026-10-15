from pathlib import Path
import copy
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from virtual_regions import prepare
from merge_regions import merge_graphs
from merge_outdoor_regions import merge_dem, merge_outdoor_documents
from resample_regional_dem import resample_dem
from stitch_regional_contours import stitch_contours
from tmap_pack import build


class VirtualTests(unittest.TestCase):
    def test_contour_stitching(self):
        def line(points,height=20): return {'id':1,'elevation_m':height,'points':points}
        a = line([[0,0],[.001,0],[.002,0]])
        b = line([[.003,0],[.002,0],[.001,0]])
        merged = stitch_contours([a,b])
        self.assertEqual(len(merged),1)
        self.assertEqual(merged[0]['points'],[[0.,0.],[.001,0.],[.002,0.],[.003,0.]])
        self.assertEqual(merged,stitch_contours([b,a,a]))
        self.assertEqual(len(stitch_contours([a,line([[.002,0],[.003,0]],40)])),2)
        self.assertEqual(len(stitch_contours([a,line([[.003,0],[.004,0]])])),2)
        junction = stitch_contours([a,line([[.001,0],[.001,.001]])])
        self.assertEqual(len(junction),3)
        self.assertEqual(sum(len(l['points'])-1 for l in junction),3)
        loop = stitch_contours([line([[0,0],[.001,0],[.001,.001]]),line([[.001,.001],[0,.001],[0,0]])])
        self.assertEqual(len(loop),1)
        self.assertEqual(loop[0]['points'][0],loop[0]['points'][-1])
        self.assertEqual(len(loop[0]['points']),5)
        with self.assertRaises(ValueError): stitch_contours([a,b],3)

    def test_explicit_resampling(self):
        coarse = {'origin':[0,0],'step':[.002,.002],'resolution_m':200,'heights':[[0,20],[40,60]],'source':'coarse'}
        fine = {'origin':[0,0],'step':[.001,.001],'resolution_m':100,'heights':[[0,10,20],[20,30,40],[40,50,60]],'source':'fine'}
        target = {'origin':[0,0],'step':[.0005,.0005],'columns':5,'rows':5,'resolution_m':200}
        result = resample_dem([coarse,fine],target)
        self.assertEqual(result['heights'],[[x*5+y*10 for x in range(5)] for y in range(5)])
        self.assertEqual(result,resample_dem([fine,coarse],target))
        with self.assertRaises(ValueError): resample_dem([coarse,fine],target,max_cells=24)
        with self.assertRaises(ValueError): resample_dem([coarse,fine],target,max_samples=49)
        shifted = copy.deepcopy(fine); shifted['origin'] = [.0005,.0005]
        shifted['heights'] = [[15,25,35],[35,45,55],[55,65,75]]
        expanded = dict(target,columns=6,rows=6)
        unaligned = resample_dem([coarse,shifted],expanded)
        self.assertEqual(unaligned['heights'][1][1],15)
        self.assertIsNone(unaligned['heights'][0][5])
        with self.assertRaises(ValueError): resample_dem([coarse,shifted],target)
        holes = copy.deepcopy(coarse); holes['heights'][1][1] = None
        sampled = resample_dem([holes],target)
        self.assertEqual(sampled['heights'][0][0],0)
        self.assertEqual(sampled['heights'][0][4],20)
        self.assertIsNone(sampled['heights'][1][1])
        self.assertEqual(resample_dem([holes,fine],target)['heights'][1][1],15)
        conflicting = copy.deepcopy(fine); conflicting['heights'][1][1] += 1
        with self.assertRaises(ValueError): resample_dem([coarse,conflicting],target)
        self.assertEqual(resample_dem([coarse,conflicting],dict(target,overlap_tolerance_cm=100))['heights'][2][2],31)
        wrong = copy.deepcopy(fine); wrong['vertical_datum'] = 1
        with self.assertRaises(ValueError): resample_dem([coarse,wrong],target)
        with self.assertRaises(ValueError): resample_dem([coarse,fine],dict(target,resolution_m=100))
        self.assertEqual(coarse['heights'],[[0,20],[40,60]])
        ties = copy.deepcopy(coarse); ties['heights'] = [[0,1],[0,1]]
        self.assertEqual(resample_dem([ties],target)['heights'][0], [0,0,0,1,1])
        mixed_policy = copy.deepcopy(fine); mixed_policy['profile_threshold_m'] = 7
        with self.assertRaises(ValueError): resample_dem([coarse,mixed_policy],target)
        self.assertEqual(resample_dem([coarse,mixed_policy],dict(target,profile_threshold_m=4))['profile_threshold_m'],4)
        with self.assertRaises(ValueError): resample_dem([coarse],dict(target,profile_spacing_m=0))

    def test_outdoor_merge_and_failures(self):
        a = {"graph":{"topology_epoch":"outdoor","nodes":[{"id":1,"point":[0,0]},{"id":2,"point":[.001,0]}],"edges":[{"id":"a","from":1,"to":2}]},
             "dem":{"origin":[0,0],"step":[.001,.001],"resolution_m":100,"heights":[[0,10],[20,30]],"source":"a"},
             "administration":[{"path":["country","west"],"rings":[[[0,0],[.001,0],[.001,.001],[0,.001],[0,0]]]}]}
        b = copy.deepcopy(a)
        b['graph']['nodes'] = [{"id":2,"point":[.001,0]},{"id":3,"point":[.002,0]}]
        b['graph']['edges'] = [{"id":"b","from":2,"to":3}]
        b['dem'].update(origin=[.001,0],heights=[[10,40],[30,50]],source='b')
        b.pop('administration')
        merged = merge_outdoor_documents([a,b],1024)
        self.assertEqual(merged['dem']['heights'],[[0,10,40],[20,30,50]])
        self.assertEqual(merged['administration'],a['administration'])
        self.assertTrue(merged['contours'])
        self.assertEqual(merge_dem([b['dem'],a['dem']])['heights'],merged['dem']['heights'])
        self.assertEqual(merge_dem([b['dem'],a['dem']])['source'],merged['dem']['source'])
        sources = [build(d)[0] for d in (a,b)]
        patch, manifest, expected = prepare([a,b],sources,1024)
        self.assertEqual(expected,build(merged)[0])
        self.assertTrue(all(manifest['outdoor_capabilities'].values()))
        files = sources+[patch]
        self.assertEqual(b''.join(files[e['source']][e['source_offset']:e['source_offset']+e['bytes']] for e in manifest['extents']),expected)
        for key,value in (('step',[.002,.001]),('origin',[.0005,0]),('vertical_datum',1),('profile_threshold_m',9)):
            bad = copy.deepcopy(b['dem']); bad[key] = value
            with self.assertRaises(ValueError): merge_dem([a['dem'],bad])
        bad = copy.deepcopy(b['dem']); bad['heights'][0][0] = 99
        with self.assertRaises(ValueError): merge_dem([a['dem'],bad])
        with self.assertRaises(ValueError): merge_dem([a['dem'],b['dem']],5)
        gap = copy.deepcopy(b['dem']); gap['origin'] = [.003,0]
        self.assertIsNone(merge_dem([a['dem'],gap])['heights'][0][2])
        self.assertEqual(a['dem']['heights'],[[0,10],[20,30]])

    def test_reconstruct_and_conflicts(self):
        a = {"name":"a","graph":{"topology_epoch":"same","nodes":[{"id":1,"point":[0,0]},{"id":2,"point":[.001,0]}],
             "edges":[{"id":"a","from":1,"to":2,"name":"trail"}]}}
        b = {"name":"b","graph":{"topology_epoch":"same","nodes":[{"id":2,"point":[.001,0]},{"id":3,"point":[.002,0]}],
             "edges":[{"id":"b","from":2,"to":3,"name":"trail"}]}}
        for page_bytes in (1024,2048,4096):
            a["page_bytes"] = b["page_bytes"] = page_bytes
            sources = [build(d)[0] for d in (a,b)]
            patch,manifest,expected = prepare([a,b],sources,page_bytes)
            sources.append(patch)
            actual = b"".join(sources[e["source"]][e["source_offset"]:e["source_offset"]+e["bytes"]] for e in manifest["extents"])
            self.assertEqual(actual,expected)
            self.assertEqual(actual,build({"name":"virtual-regions","graph":merge_graphs([a["graph"],b["graph"]]),"page_bytes":page_bytes})[0])
            self.assertGreater(manifest["reused_bytes"],0)
            self.assertEqual(manifest["patch_bytes"]+manifest["reused_bytes"],len(actual))
        changed = copy.deepcopy(b); changed["graph"]["topology_epoch"] = "different"
        with self.assertRaises(ValueError): prepare([a,changed],[build(d)[0] for d in (a,changed)])
        damaged = bytearray(sources[0]); damaged[-1] ^= 1
        with self.assertRaises(ValueError): prepare([a,b],[bytes(damaged),build(b)[0]])


if __name__ == "__main__": unittest.main()
