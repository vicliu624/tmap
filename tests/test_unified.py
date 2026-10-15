import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compose_map import compose
from tmap_pack import write, crc32c
from make_fixtures import document

probe, fixture_root = sys.argv[1:]
root = Path(fixture_root)
base = root/'legacy.tmap'
data = bytearray(base.read_bytes())
count = struct.unpack_from('<I',data,48)[0]
directory = bytearray(data[4096:4096+count*64])
for identity in range(500,550):
    directory.extend(struct.pack('<IHHII6Q',900,1,0,identity,0,8192,0,0,0,0,0))
struct.pack_into('<I',data,48,len(directory)//64)
data[4096:4096+len(directory)] = directory
struct.pack_into('<I',data,164,crc32c(directory))
struct.pack_into('<I',data,160,0)
struct.pack_into('<I',data,160,crc32c(data[:256]))
source = root/'unified-display-fixture.tmap'; source.write_bytes(data)
outdoor = root/'unified-outdoor-fixture.tmap'
settings = document(); settings['page_bytes']=4096; settings['dem_lod_maximum_zoom']=18
write(settings,outdoor)
combined = root/'unified-fixture.tmap'; combined.unlink(missing_ok=True)
report = compose([source],combined,outdoor)
assert report['sections'] > 64
subprocess.run([probe,str(source),str(combined)],check=True)
try:
    compose([source],combined,outdoor)
    raise AssertionError('Existing output was overwritten')
except FileExistsError:
    pass
