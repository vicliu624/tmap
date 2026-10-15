"""Small real v1 schema fixture, without external data dependencies."""
from pathlib import Path
import struct
from tmap_pack import crc32c


def page(kind,count,size,offset=0):
    result = bytearray(4096)
    struct.pack_into("<IHHQIHH",result,0,0x31475054,kind,0,offset,count,size,0)
    return result


def seal(result):
    struct.pack_into("<I",result,48,0)
    struct.pack_into("<I",result,48,crc32c(result))
    return bytes(result)


def write_legacy_administration(output):
    paths = page(10,2,0); refs = page(9,2,16); pois = page(9,2,96)
    tree = page(14,2,32,4096); at = 64
    for i,(longitude,name) in enumerate([(10000,"West"),(30000,"East")]):
        path = ("Country / Province / "+name).encode("utf-8")
        struct.pack_into("<HH",paths,at,len(path),0); paths[at+4:at+4+len(path)] = path
        struct.pack_into("<QBB6x",refs,64+i*16,at,7,0); at += 4+len(path)
        pois[64+i*96:80+i*96] = (i+1).to_bytes(16,"little")
        struct.pack_into("<2i",pois,80+i*96,10000,longitude)
        struct.pack_into("<4iQII",tree,64+i*32,longitude,10000,longitude,10000,i+1,1,0)
    types = [1,2,20,21,22,23,24,30,31,32,40,41,42,50,51]
    data = {20:(seal(pois),2,0),24:(bytes(4096)+seal(tree),2,4096),
            50:(seal(refs),2,0),51:(seal(paths),2,0)}
    directory = bytearray(); body = bytearray(); offset = 8192
    for kind in types:
        payload,count,root = data.get(kind,(b"",0,0))
        directory.extend(struct.pack("<IHHII6Q",kind,1,2 if payload else 0,kind,0,offset,len(payload),count,root,0,0))
        body.extend(payload); offset += len(payload)
    header = bytearray(8192); header[:8] = b"TMAP\r\n\x1a\n"
    struct.pack_into("<HHIIIQQQII",header,8,1,0,256,0x01020304,4096,0,offset,4096,len(types),64)
    struct.pack_into("<4iIIQQ",header,120,0,0,40000,40000,0,0,8192,0)
    struct.pack_into("<2I",header,168,1,2)
    struct.pack_into("<I",header,164,crc32c(directory))
    struct.pack_into("<I",header,160,crc32c(header[:256]))
    header[4096:4096+len(directory)] = directory
    Path(output).write_bytes(header+body)
