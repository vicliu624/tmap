"""Streaming snapshot OSM PBF decoder for host-side import (not MCU code).

Wire fields follow the official OSM-binary schemas. No XML staging file or
protobuf runtime is required. Encoded headers/decoded blobs have explicit limits;
the downstream graph producer still retains a region's graph in host memory.
"""
import bz2
import importlib
import io
import itertools
import lzma
import struct
import xml.etree.ElementTree as ET
import zlib

MAX_HEADER = 64*1024
MAX_RAW = 32*1024*1024
MAX_BLOB = 64*1024*1024
MAX_LZMA_MEMORY = 64*1024*1024  # Decoder dictionary/state, independently of output size.


def varint(data, position):
    value = 0
    for shift in range(0,70,7):
        if position == len(data): raise ValueError("truncated protobuf varint")
        byte = data[position]; position += 1
        if shift == 63 and byte > 1: raise ValueError("protobuf uint64 overflow")
        value |= (byte & 127) << shift
        if byte < 128: return value,position
    raise ValueError("invalid protobuf varint")


def message(raw):
    data = memoryview(raw); position = 0; fields = {}
    while position < len(data):
        key,position = varint(data,position); number,wire = key>>3,key&7
        if not 0 < number < 2**29: raise ValueError("invalid protobuf field")
        if wire == 0: value,position = varint(data,position)
        elif wire in (1,5):
            size = 8 if wire == 1 else 4
            if size > len(data)-position: raise ValueError("truncated fixed field")
            value = data[position:position+size]; position += size
        elif wire == 2:
            size,position = varint(data,position)
            if size > len(data)-position: raise ValueError("truncated bytes field")
            value = data[position:position+size]; position += size
        else: raise ValueError("unsupported protobuf group/wire type")
        fields.setdefault(number,[]).append((wire,value))
    return fields


def integer(fields, number, default=None):
    values = fields.get(number)
    if not values:
        if default is None: raise ValueError("missing protobuf integer field "+str(number))
        return default
    wire,value = values[-1]
    if wire != 0: raise ValueError("integer field has wrong wire type")
    return value


def strings(fields, number):
    for wire,value in fields.get(number,[]):
        if wire != 2: raise ValueError("bytes field has wrong wire type")
        yield value


def submessage(fields, number):
    values = list(strings(fields,number))
    if not values: raise ValueError("missing protobuf message field "+str(number))
    return message(values[0] if len(values) == 1 else b"".join(values))


def numbers(fields, number, zigzag=False):
    for wire,value in fields.get(number,[]):
        if wire == 0: values = (value,)
        elif wire == 2:
            def packed(data):
                at = 0
                while at < len(data):
                    item,at = varint(data,at); yield item
            values = packed(value)
        else: raise ValueError("numeric array has wrong wire type")
        for item in values: yield (item>>1)^-(item&1) if zigzag else item


def signed(value): return value-2**64 if value >= 2**63 else value
def zigzag(value): return (value>>1)^-(value&1)


def delta(values):
    total = 0
    for value in values:
        total += value
        if not -2**63 <= total < 2**63: raise ValueError("PBF delta overflow")
        yield total


def table_string(table, index):
    if not 0 <= index < len(table): raise ValueError("string-table index out of range")
    return table[index]


def add_tags(element, fields, table):
    missing = object()
    for key,value in itertools.zip_longest(numbers(fields,2),numbers(fields,3),fillvalue=missing):
        if key is missing or value is missing or not key: raise ValueError("invalid tag key/value arrays")
        ET.SubElement(element,"tag",k=table_string(table,key),v=table_string(table,value))


def coordinate(value, offset, granularity, latitude):
    nano = offset+granularity*value
    bound = (90 if latitude else 180)*10**9
    if not -bound <= nano <= bound: raise ValueError("coordinate outside WGS84")
    return str(nano/10**9)


def visibility(fields):
    if 4 in fields and integer(submessage(fields,4),6,1) != 1:
        raise ValueError("deleted/history object in snapshot PBF")


def primitives(raw):
    block = message(raw)
    table = [bytes(s).decode("utf-8") for s in strings(submessage(block,1),1)]
    if not table or table[0] != "": raise ValueError("PBF string table must start with empty entry")
    granularity = integer(block,17,100)
    if not 0 < granularity < 2**31: raise ValueError("invalid granularity")
    lat_offset,lon_offset = signed(integer(block,19,0)),signed(integer(block,20,0))
    missing = object()
    for group_raw in strings(block,2):
        group = message(group_raw)
        for raw_node in strings(group,1):
            node = message(raw_node); visibility(node)
            identity = zigzag(integer(node,1))
            if identity <= 0: raise ValueError("nonpositive snapshot node ID")
            element = ET.Element("node",id=str(identity),
                lat=coordinate(zigzag(integer(node,8)),lat_offset,granularity,True),
                lon=coordinate(zigzag(integer(node,9)),lon_offset,granularity,False))
            add_tags(element,node,table); yield element
        for raw_dense in strings(group,2):
            dense = message(raw_dense)
            visible = list(numbers(submessage(dense,5),6)) if 5 in dense else []
            if any(v != 1 for v in visible): raise ValueError("history visibility in snapshot DenseNodes")
            tags = iter(numbers(dense,10)); first = next(tags,missing)
            tagged = first is not missing
            if tagged: tags = itertools.chain((first,),tags)
            count = 0
            arrays = (delta(numbers(dense,1,True)),delta(numbers(dense,8,True)),delta(numbers(dense,9,True)))
            for identity,lat,lon in itertools.zip_longest(*arrays,fillvalue=missing):
                if any(v is missing for v in (identity,lat,lon)) or identity <= 0: raise ValueError("invalid dense coordinate arrays")
                element = ET.Element("node",id=str(identity),lat=coordinate(lat,lat_offset,granularity,True),
                                     lon=coordinate(lon,lon_offset,granularity,False))
                if tagged:
                    while True:
                        key = next(tags,missing)
                        if key is missing: raise ValueError("missing DenseNodes tag delimiter")
                        if key == 0: break
                        value = next(tags,missing)
                        if value is missing: raise ValueError("missing DenseNodes tag value")
                        ET.SubElement(element,"tag",k=table_string(table,key),v=table_string(table,value))
                count += 1; yield element
            if next(tags,missing) is not missing or (visible and len(visible) != count): raise ValueError("extra dense tag/visibility data")
        for raw_way in strings(group,3):
            way = message(raw_way); visibility(way)
            identity = signed(integer(way,1))
            if identity <= 0: raise ValueError("nonpositive snapshot way ID")
            element = ET.Element("way",id=str(identity)); add_tags(element,way,table)
            refs = list(delta(numbers(way,8,True)))
            if any(r <= 0 for r in refs): raise ValueError("nonpositive way reference")
            if 9 in way or 10 in way:
                lat,lon = list(delta(numbers(way,9,True))),list(delta(numbers(way,10,True)))
                if len(lat) != len(refs) or len(lon) != len(refs): raise ValueError("invalid LocationsOnWays arrays")
                for y,x in zip(lat,lon): coordinate(y,lat_offset,granularity,True); coordinate(x,lon_offset,granularity,False)
            for ref in refs: ET.SubElement(element,"nd",ref=str(ref))
            yield element
        for raw_relation in strings(group,4):
            relation = message(raw_relation); visibility(relation)
            identity = signed(integer(relation,1))
            if identity <= 0: raise ValueError("nonpositive snapshot relation ID")
            element = ET.Element("relation",id=str(identity)); add_tags(element,relation,table)
            arrays = (numbers(relation,8),delta(numbers(relation,9,True)),numbers(relation,10))
            for role,ref,kind in itertools.zip_longest(*arrays,fillvalue=missing):
                if any(v is missing for v in (role,ref,kind)) or kind not in (0,1,2) or ref <= 0: raise ValueError("invalid relation members")
                ET.SubElement(element,"member",role=table_string(table,role),ref=str(ref),type=("node","way","relation")[kind])
            yield element


def decode_blob(raw):
    blob = message(raw)
    kinds = [k for k in (1,3,4,5,6,7) if k in blob]
    if len(kinds) != 1: raise ValueError("PBF blob needs exactly one data encoding")
    kind = kinds[0]
    payloads = list(strings(blob,kind))
    if len(payloads) != 1: raise ValueError("duplicate PBF blob encoding")
    payload = bytes(payloads[0])
    size = signed(integer(blob,2,len(payload) if kind == 1 else -1))
    if not 0 <= size <= MAX_RAW: raise ValueError("PBF raw size limit exceeded")
    if kind == 1: result = payload
    elif kind in (3,4,5):
        decoder = (lzma.LZMADecompressor(memlimit=MAX_LZMA_MEMORY) if kind == 4
                   else zlib.decompressobj() if kind == 3 else bz2.BZ2Decompressor())
        try:
            result = decoder.decompress(payload,size+1)
        except lzma.LZMAError as error:
            raise ValueError('invalid PBF LZMA stream or decoder memory limit exceeded') from error
        if not decoder.eof or decoder.unused_data: raise ValueError("truncated/oversized/trailing compressed PBF data")
    elif kind == 6:
        try: module = importlib.import_module("lz4.block")
        except ImportError as error: raise ValueError("LZ4 PBF requires optional lz4 package") from error
        # Zero asks python-lz4 to trust an embedded size prefix. Always pass a
        # positive explicit capacity, including for a declared empty block.
        try: result = module.decompress(payload,uncompressed_size=max(size,1))
        except module.LZ4BlockError as error: raise ValueError('invalid/oversized PBF LZ4 data') from error
    else:
        try: module = importlib.import_module("zstandard")
        except ImportError as error: raise ValueError("ZSTD PBF requires optional zstandard package") from error
        try:
            parameters = module.get_frame_parameters(payload)
            if parameters.window_size > MAX_RAW:
                raise ValueError('PBF ZSTD window memory limit exceeded')
            if parameters.content_size not in (module.CONTENTSIZE_UNKNOWN, size):
                raise ValueError('PBF ZSTD frame/raw_size mismatch')
            # Independently delimit one frame. Some bindings' one-shot empty
            # decode returns before checking allow_extra_data=False.
            if payload[:4] != b'\x28\xb5\x2f\xfd': raise ValueError('PBF requires a standard ZSTD frame')
            descriptor = payload[4]
            at = 5 + (0 if descriptor & 32 else 1) + (0,1,2,4)[descriptor & 3]
            at += ((1 if descriptor & 32 else 0),2,4,8)[descriptor >> 6]
            while True:
                if at+3 > len(payload): raise ValueError('truncated PBF ZSTD block header')
                block = int.from_bytes(payload[at:at+3], 'little'); at += 3
                kind = (block >> 1) & 3
                if kind == 3: raise ValueError('reserved PBF ZSTD block type')
                at += 1 if kind == 1 else block >> 3
                if at > len(payload): raise ValueError('truncated PBF ZSTD block')
                if block & 1: break
            at += 4 if descriptor & 4 else 0
            if at != len(payload): raise ValueError('truncated/trailing PBF ZSTD frame')
            with module.ZstdDecompressor(max_window_size=MAX_RAW//1024).stream_reader(
                    io.BytesIO(payload),read_across_frames=False) as reader:
                result = reader.read(size+1)
        except module.ZstdError as error: raise ValueError('invalid/oversized/trailing PBF ZSTD data') from error
    if len(result) != size: raise ValueError("PBF raw_size mismatch")
    return result


def read_exact(file, size):
    data = file.read(size)
    if len(data) != size: raise ValueError("truncated PBF block")
    return data


def iter_pbf(path):
    header_seen = False
    with open(path,"rb") as file:
        while True:
            prefix = file.read(4)
            if not prefix: break
            if len(prefix) != 4: raise ValueError("truncated PBF header length")
            length = struct.unpack(">I",prefix)[0]
            if not 0 < length <= MAX_HEADER: raise ValueError("PBF header length limit exceeded")
            header = message(read_exact(file,length))
            kind = bytes(next(strings(header,1),b"")).decode("utf-8")
            size = signed(integer(header,3))
            if not 0 < size <= MAX_BLOB: raise ValueError("PBF blob length limit exceeded")
            raw = read_exact(file,size)
            if not header_seen and kind != "OSMHeader": raise ValueError("PBF must start with OSMHeader")
            if kind == "OSMHeader":
                if header_seen: raise ValueError("duplicate PBF header")
                metadata = message(decode_blob(raw))
                required = {bytes(s).decode("utf-8") for s in strings(metadata,4)}
                if "OsmSchema-V0.6" not in required or required-{"OsmSchema-V0.6","DenseNodes"}:
                    raise ValueError("unsupported required PBF features: "+str(sorted(required)))
                header_seen = True
            elif kind == "OSMData": yield from primitives(decode_blob(raw))
            elif not kind.startswith("_"): raise ValueError("unsupported PBF block type: "+kind)
    if not header_seen: raise ValueError("missing PBF header")
