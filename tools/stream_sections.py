"""Bounded-memory host writer for precompiled native outdoor section files."""
import hashlib
import os
from pathlib import Path
import struct
import tempfile
from tmap_pack import crc32c as reference_crc

try:
    from google_crc32c import value as crc32c
except ImportError:
    crc32c = reference_crc


def write_sections(output, name, bounds_e7, sections, capabilities=1):
    sections = sorted(sections, key=lambda section: section['type'])
    if not 1 <= len(sections) <= 64 or len({item['type'] for item in sections}) != len(sections):
        raise ValueError('Invalid section count or identity')
    destination = Path(output)
    if destination.exists(): raise FileExistsError(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    page = 4096
    start = ((page + len(sections) * 64 + page - 1) // page) * page
    directory = bytearray()
    report = {}
    digest = hashlib.sha256()
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.sections-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(bytes(start))
            for item in sections:
                path = Path(item['path']); logical = path.stat().st_size; offset = stream.tell()
                physical = ((logical + page - 33) // (page - 32)) * page
                directory.extend(struct.pack('<IHHII6Q', item['type'], 1, 2, item['type'], 0,
                    offset, physical, item['count'], 0, logical, 0))
                ordinal = 0
                with path.open('rb') as source:
                    while payload := source.read(page - 32):
                        block = bytearray(page)
                        struct.pack_into('<4s4I', block, 0, b'TPG2', item['type'], ordinal, len(payload), 0)
                        block[32:32 + len(payload)] = payload
                        struct.pack_into('<I', block, 16, crc32c(bytes(block)))
                        stream.write(block); digest.update(block); ordinal += 1
                if stream.tell() - offset != physical: raise ValueError('Section changed during writing')
                report[str(item['type'])] = dict(logical_bytes=logical, stored_bytes=physical, records=item['count'])
            header = bytearray(256); header[:8] = b'TMAP\r\n\x1a\n'
            struct.pack_into('<HHIIIQQQII', header, 8, 2, 0, 256, 0x01020304, page, 0, stream.tell(), page, len(sections), 64)
            header[56:72] = hashlib.sha256(name.encode()).digest()[:16]
            header[72:88] = hashlib.sha256(directory + digest.digest()).digest()[:16]
            header[88:104] = hashlib.sha256(b'center-dem-pyramid').digest()[:16]
            struct.pack_into('<Q', header, 104, 1)
            struct.pack_into('<4iII', header, 120, *bounds_e7, 0, capabilities)
            struct.pack_into('<I', header, 164, crc32c(bytes(directory)))
            struct.pack_into('<I', header, 160, crc32c(bytes(header)))
            stream.seek(0); stream.write(header); stream.seek(page); stream.write(directory)
            stream.flush(); os.fsync(stream.fileno())
        os.link(temporary, destination)
        return dict(bytes=destination.stat().st_size, sections=report, capabilities=capabilities)
    finally:
        if temporary: temporary.unlink(missing_ok=True)
