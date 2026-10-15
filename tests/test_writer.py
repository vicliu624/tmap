import copy
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from tmap_pack import build, crc32c, extract_contours, contour_lod, edge_terrain, reverse_terrain, graph_sections
from unittest.mock import patch
from make_fixtures import document


class WriterTests(unittest.TestCase):
    def test_directory_format_bound(self):
        for count in (32, 37, 64, 65):
            sections = [(2000+i, b'', 0) for i in range(count)]
            with patch('tmap_pack.contour_sections', return_value=sections):
                doc = {'contours':[{'id':1,'elevation_m':10,'points':[[0,0],[1,1]]}], 'contour_lod':False, 'bounds':[0,0,1,1]}
                if count == 65:
                    with self.assertRaisesRegex(ValueError, '64 outdoor sections'):
                        build(doc)
                else:
                    raw,_ = build(doc)
                    self.assertEqual(struct.unpack_from('<I',raw,48)[0], count)

    def test_bidirectional_terrain_uses_one_model(self):
        samples = [(0,0,0),(31,10,5),(117,16,29)]
        self.assertEqual(reverse_terrain(reverse_terrain(samples)),samples)
        self.assertEqual(reverse_terrain(samples),[(0,0,0),(86,24,6),(117,29,16)])
        self.assertIsNone(reverse_terrain(None))
        doc = document()
        path = [[0,0],[0.0007,0],[0.0011,0],[0.002,0]]
        graph = {"nodes":[{"id":1,"point":path[0]},{"id":2,"point":path[-1]}],
                 "edges":[{"id":"f","from":1,"to":2,"points":path,"opposite":"r"},
                          {"id":"r","from":2,"to":1,"points":path[::-1],"opposite":"f"}]}
        with patch("tmap_pack.edge_terrain",wraps=edge_terrain) as sampler:
            sections = {kind:raw for kind,raw,_ in graph_sections(graph,doc["dem"])}
            self.assertEqual(sampler.call_count,1)
        profiles = []
        for row in range(2):
            offset,count,_ = struct.unpack_from("<QII",sections[1108],row*16)
            profiles.append([struct.unpack_from("<III",sections[1109],offset+i*12) for i in range(count)])
        self.assertEqual(profiles[1],reverse_terrain(profiles[0]))
        self.assertEqual(struct.unpack_from("<III",sections[1102],12),
                         (profiles[0][-1][0],profiles[1][-1][2],profiles[1][-1][1]))

    def test_crc_reference(self):
        self.assertEqual(crc32c(b"123456789"), 0xe3069283)

    def test_page_profiles(self):
        for size in (1024,2048,4096):
            doc = document(); doc["page_bytes"] = size
            raw,report = build(doc)
            self.assertEqual(struct.unpack_from("<I",raw,20)[0],size)
            self.assertEqual(len(raw)%size,0)
            self.assertEqual(report["page_bytes"],size)
            directory = struct.unpack_from("<Q",raw,40)[0]
            count = struct.unpack_from("<I",raw,48)[0]
            self.assertEqual(crc32c(raw[directory:directory+count*64]),struct.unpack_from("<I",raw,164)[0])

    def test_administration_rejects_invalid_input(self):
        feature = {"path":["Country","Province"],"rings":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}
        self.assertTrue(build({"administration":[feature]})[0])
        for path in ([""],["Country/Province"],["x"*513]):
            broken = copy.deepcopy(feature); broken["path"] = path
            with self.assertRaises(ValueError): build({"administration":[broken]})
        for ring in ([[0,0],[1,0],[0,0]], [[0,0],[1,0],[2,0],[0,0]]):
            broken = copy.deepcopy(feature); broken["rings"] = [ring]
            with self.assertRaises(ValueError): build({"administration":[broken]})

    def test_deterministic_and_paged(self):
        raw, report = build(document())
        self.assertEqual(build(document())[0], raw)
        self.assertEqual(report["bytes"], len(raw))
        self.assertEqual(len(raw) % 4096, 0)
        self.assertEqual(raw[8192:8196], b"TPG2")

    def test_reject_invalid_geometry(self):
        doc = document(); doc["graph"]["edges"][0]["points"] = [[0, 0], [1, 1]]
        with self.assertRaises(ValueError): build(doc)

    def test_reject_invalid_dem(self):
        for value in (-40000, 40000, float("nan")):
            doc = document(); doc["dem"]["heights"][0][0] = value
            with self.assertRaises((ValueError, OverflowError)): build(doc)

    def test_missing_cell_not_bridged(self):
        dem = copy.deepcopy(document()["dem"])
        dem["heights"][1][1] = None
        self.assertEqual(extract_contours(dem), [])

    def test_contour_height_on_planar_dem(self):
        dem = document()["dem"]
        contours = extract_contours(dem, 10)
        self.assertTrue(contours)
        for line in contours:
            for lon, lat in line["points"]:
                self.assertAlmostEqual(100+lon*10000+lat*30000, line["elevation_m"], delta=0.005)

    def test_saddle(self):
        dem = copy.deepcopy(document()["dem"])
        dem["heights"] = [[0, 20], [20, 0]]
        lines = [f for f in extract_contours(dem) if f["elevation_m"] == 10]
        self.assertEqual(len(lines), 2)

    def test_lod_preserves_endpoints_and_reduces_straight_lines(self):
        path = [(1000, i*100) for i in range(1000)]
        lod = contour_lod(path)
        self.assertEqual(lod, [(path[0],12),(path[-1],12)])

    def test_road_terrain_unknown_and_complete(self):
        dem = copy.deepcopy(document()["dem"])
        profile = edge_terrain(dem, [(0,0),(0,20000)])
        self.assertEqual(profile[0], (0,0,0))
        self.assertEqual(profile[-1][1], 2000)
        dem["heights"][0][1] = None
        self.assertIsNone(edge_terrain(dem, [(0,0),(0,20000)]))


if __name__ == "__main__": unittest.main()
