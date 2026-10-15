"""Wire fixtures exercise the decoder independently of its parsing helpers."""
from pathlib import Path
import bz2
import importlib.util
import lzma
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from import_osm import import_xml, import_pbf
from osm_pbf import decode_blob, iter_pbf, primitives, message
from test_importers import OSM, via_osm


def var(value):
    value &= (1 << 64)-1
    result = bytearray()
    while value > 127:
        result.append((value & 127)|128); value >>= 7
    result.append(value)
    return bytes(result)


def field(number, value):
    return var(number*8)+(var(value)) if isinstance(value,int) else var(number*8+2)+var(len(value))+value


def zz(value): return (value << 1) ^ (value >> 63)
def packed(number, values): return field(number,b"".join(var(v) for v in values))
def deltas(values):
    previous = 0; result = []
    for value in values:
        result.append(zz(value-previous)); previous = value
    return result


def block(kind, raw, encoding=3):
    compressor = {1:lambda x:x,3:zlib.compress,4:lzma.compress,5:bz2.compress}[encoding]
    blob = field(2,len(raw))+field(encoding,compressor(raw))
    header = field(1,kind.encode())+field(3,len(blob))
    return struct.pack(">I",len(header))+header+blob


def fixture(dense, encoding=3, xml=OSM):
    root = ET.fromstring(xml)
    table = [""]
    for element in root:
        for tag in element.findall("tag"):
            for value in tag.attrib.values():
                if value not in table: table.append(value)
        for member in element.findall("member"):
            if member.attrib["role"] not in table: table.append(member.attrib["role"])
    def tags(element):
        ts = element.findall("tag")
        return packed(2,[table.index(t.attrib["k"]) for t in ts])+packed(3,[table.index(t.attrib["v"]) for t in ts])
    # Non-default offsets and negative deltas catch coordinate and signed decoding.
    def coord(node,key): return round(float(node.attrib[key])*1e9/100)-1000
    group = b""
    nodes = root.findall("node")[::-1]
    if dense:
        raw = packed(1,deltas([int(n.attrib["id"]) for n in nodes]))
        raw += packed(8,deltas([coord(n,"lat") for n in nodes]))
        raw += packed(9,deltas([coord(n,"lon") for n in nodes]))
        key_values = []
        for n in nodes:
            for tag in n.findall('tag'):
                key_values.extend((table.index(tag.attrib['k']),table.index(tag.attrib['v'])))
            key_values.append(0)
        raw += packed(10,key_values)
        group += field(2,raw)
    else:
        for n in nodes:
            group += field(1,field(1,zz(int(n.attrib["id"])))+tags(n)+field(8,zz(coord(n,"lat")))+field(9,zz(coord(n,"lon"))))
    for way in root.findall("way"):
        raw = field(1,int(way.attrib["id"]))+tags(way)
        raw += packed(8,deltas([int(n.attrib["ref"]) for n in way.findall("nd")]))
        group += field(3,raw)
    for relation in root.findall("relation"):
        members = relation.findall("member")
        raw = field(1,int(relation.attrib["id"]))+tags(relation)
        raw += packed(8,[table.index(m.attrib["role"]) for m in members])
        raw += packed(9,deltas([int(m.attrib["ref"]) for m in members]))
        raw += packed(10,[("node","way","relation").index(m.attrib["type"]) for m in members])
        group += field(4,raw)
    raw = field(1,b"".join(field(1,s.encode()) for s in table))+field(2,group)
    raw += field(19,100000)+field(20,100000)
    return block("OSMHeader",field(4,b"OsmSchema-V0.6")+field(4,b"DenseNodes"),encoding)+block("OSMData",raw,encoding)


class PbfTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec('lz4'), 'optional host lz4 unavailable')
    def test_optional_lz4_explicit_capacity(self):
        import lz4.block
        for raw in (b'', b'x', b'abc' * 100):
            stream = lz4.block.compress(raw, store_size=False)
            self.assertEqual(decode_blob(field(2,len(raw))+field(6,stream)), raw)
        for declared in (0, 1):
            stream = lz4.block.compress(b'x'*100, store_size=False)
            with self.assertRaises(ValueError): decode_blob(field(2,declared)+field(6,stream))
        with self.assertRaises(ValueError):
            decode_blob(field(2,0)+field(6,struct.pack('<I',1<<30)+b'x'))

    @unittest.skipUnless(importlib.util.find_spec('zstandard'), 'optional host zstandard unavailable')
    def test_optional_zstd_frame_limits(self):
        import zstandard as zstd
        for include_size in (False, True):
            for raw in (b'', b'x', b'abc' * 100):
                stream = zstd.ZstdCompressor(write_content_size=include_size).compress(raw)
                self.assertEqual(decode_blob(field(2,len(raw))+field(7,stream)), raw)
                with self.assertRaises(ValueError):
                    decode_blob(field(2,len(raw))+field(7,stream+b'trailing'))
                with self.assertRaises(ValueError):
                    decode_blob(field(2,len(raw))+field(7,stream+stream))
                for checksum in (False, True):
                    checked = zstd.ZstdCompressor(write_content_size=include_size, write_checksum=checksum).compress(raw)
                    self.assertEqual(decode_blob(field(2,len(raw))+field(7,checked)), raw)
                    with self.assertRaises(ValueError):
                        decode_blob(field(2,len(raw))+field(7,checked[:-1]))
            stream = zstd.ZstdCompressor(write_content_size=include_size).compress(b'x'*100)
            for declared in (0, 1):
                with self.assertRaises(ValueError): decode_blob(field(2,declared)+field(7,stream))
        # A non-single-segment frame header declaring a 128 MiB window; no
        # compressor or large decompressor allocation is needed to reject it.
        with self.assertRaisesRegex(ValueError, 'window memory limit'):
            decode_blob(field(2,1)+field(7,b'\x28\xb5\x2f\xfd\x00\x88\x01\x00\x00'))

    def test_lzma_dictionary_memory_limit(self):
        # Patch the legacy LZMA dictionary header, without constructing a huge
        # compressor or allowing the test itself to allocate a huge dictionary.
        stream = bytearray(lzma.compress(b'x', format=lzma.FORMAT_ALONE))
        self.assertEqual(decode_blob(field(2,1)+field(4,bytes(stream))), b'x')
        struct.pack_into('<I', stream, 1, 1 << 30)
        with self.assertRaisesRegex(ValueError, 'decoder memory limit'):
            decode_blob(field(2,1)+field(4,bytes(stream)))

    def test_runtime_conditional_relation_parity(self):
        documents = (OSM.replace('</relation>','<tag k="restriction:bicycle:conditional" v="none @ (Mo)"/></relation>'),
            via_osm().replace('</relation>','<tag k="restriction:foot:conditional" v="none @ (Mo)"/></relation>'))
        with tempfile.TemporaryDirectory() as directory:
            for raw in documents:
                xml = Path(directory)/'turn.osm'; xml.write_text(raw,encoding='utf-8')
                expected = import_xml(xml,runtime_conditions=True)
                self.assertTrue(expected['conditional_turns'])
                for dense in (False,True):
                    pbf = Path(directory)/'turn.pbf'; pbf.write_bytes(fixture(dense,xml=raw))
                    self.assertEqual(import_pbf(pbf,runtime_conditions=True),expected)

    def test_runtime_barrier_parity(self):
        raw = OSM.replace('<node id="2" lon="0.001" lat="0"/>',
            '<node id="2" lon="0.001" lat="0"><tag k="barrier" v="gate"/>'
            '<tag k="access:conditional" v="no @ (Mo)"/></node>')
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory)/'barrier.osm'; xml.write_text(raw,encoding='utf-8')
            expected = import_xml(xml,runtime_conditions=True)
            for dense in (False,True):
                pbf = Path(directory)/'barrier.pbf'; pbf.write_bytes(fixture(dense,xml=raw))
                self.assertEqual(import_pbf(pbf,runtime_conditions=True),expected)

    def test_runtime_road_topology_parity(self):
        raw = OSM.replace('<tag k="oneway" v="yes"/>','<tag k="oneway" v="yes"/>'
            '<tag k="oneway:bicycle:conditional" v="no @ (Mo)"/>')
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory)/'runtime.osm'; xml.write_text(raw,encoding='utf-8')
            expected = import_xml(xml,2,runtime_conditions=True)
            self.assertEqual(len([e for e in expected['edges'] if e['id'].startswith('10:')]),4)
            for dense in (False,True):
                pbf = Path(directory)/'runtime.pbf'; pbf.write_bytes(fixture(dense,xml=raw))
                self.assertEqual(import_pbf(pbf,2,runtime_conditions=True),expected)

    def test_conditional_node_snapshot_parity(self):
        from osm_conditions import parse_instant
        raw = OSM.replace('<node id="2" lon="0.001" lat="0"/>',
            '<node id="2" lon="0.001" lat="0"><tag k="barrier" v="gate"/>'
            '<tag k="foot:conditional" v="no @ (Mo)"/></node>')
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory)/'gate.osm'; xml.write_text(raw,encoding='utf-8')
            for dense in (False,True):
                pbf = Path(directory)/'gate.pbf'; pbf.write_bytes(fixture(dense,xml=raw))
                with self.assertRaisesRegex(ValueError,'conditional barrier'): import_pbf(pbf,1)
                with self.assertRaisesRegex(ValueError,'no routable roads'):
                    import_pbf(pbf,1,parse_instant('2026-10-12T09:00:00+02:00'))
                instant = parse_instant('2026-10-13T09:00:00+02:00')
                self.assertEqual(import_pbf(pbf,1,instant),import_xml(xml,1,instant))
                self.assertEqual(import_pbf(pbf,2),import_xml(xml,2))

    def test_via_way_parity(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory)/'via.osm'; xml.write_text(via_osm())
            expected = import_xml(xml)
            self.assertTrue(expected['via_restrictions'])
            for dense in (False,True):
                pbf = Path(directory)/'via.pbf'; pbf.write_bytes(fixture(dense,xml=via_osm()))
                self.assertEqual(import_pbf(pbf),expected)
    def test_xml_parity(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory)/"map.osm"; xml.write_text(OSM,encoding="utf-8")
            expected = import_xml(xml)
            for dense in (False,True):
                for encoding in (1,3,4,5):
                    with self.subTest(dense=dense,encoding=encoding):
                        pbf = Path(directory)/"map.pbf"; pbf.write_bytes(fixture(dense,encoding))
                        self.assertEqual(import_pbf(pbf),expected)

    def test_truncated_and_feature_checks(self):
        good = fixture(True)
        bad = [b"",good[:3],good[:-1],struct.pack(">I",65537),
               block("OSMData",b""),block("OSMHeader",field(4,b"HistoricalInformation"))]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"bad.pbf"
            for data in bad:
                path.write_bytes(data)
                with self.assertRaises(ValueError): list(iter_pbf(path))

    def test_wire_and_compression_validation(self):
        for data in (b"\x00",b"\x0a\x05x",b"\x08"+b"\xff"*10):
            with self.assertRaises(ValueError): message(data)
        for data in (field(2,1)+field(3,zlib.compress(b"x"*1000)),
                     field(2,1)+field(3,zlib.compress(b"x")+b"trailing"),
                     field(2,1)+field(1,b"xx"),field(1,b"x")+field(3,b"x")):
            with self.assertRaises(ValueError): decode_blob(data)
        table = field(1,field(1,b""))
        dense = packed(1,[zz(1)])+packed(8,[0])+packed(9,[0])+packed(10,[1])
        with self.assertRaises(ValueError): list(primitives(table+field(2,field(2,dense))))


if __name__ == "__main__": unittest.main()
