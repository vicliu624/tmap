#!/usr/bin/env python3
"""Stream native display and outdoor sections into one indexed TMAP 2.0 file.

The display and outdoor section namespaces retain their established encodings.
No raster payload is wrapped, decoded or copied into a second paging scheme.
"""
import argparse
import hashlib
import json
from pathlib import Path
import os
import struct
import tempfile
from tmap_pack import crc32c

PAGE = 4096


def inspect(path, major):
    with Path(path).open('rb') as source:
        header = bytearray(source.read(256))
        if len(header) != 256 or header[:8] != b'TMAP\r\n\x1a\n':
            raise ValueError('Invalid map header: ' + str(path))
        actual = struct.unpack_from('<HH', header, 8)
        native_display = major == 1 and actual == (2, 0) and struct.unpack_from('<Q', header, 24)[0] == 8 and struct.unpack_from('<I', header, 140)[0] == 16
        if actual != (major, 0) and not native_display:
            raise ValueError('Unexpected input format: ' + str(path))
        if struct.unpack_from('<I', header, 20)[0] != PAGE:
            raise ValueError('Unified native sections require 4096-byte pages')
        if struct.unpack_from('<Q', header, 32)[0] != os.fstat(source.fileno()).st_size:
            raise ValueError('Incomplete source file')
        expected = struct.unpack_from('<I', header, 160)[0]
        struct.pack_into('<I', header, 160, 0)
        if crc32c(header) != expected:
            raise ValueError('Header CRC mismatch')
        count = struct.unpack_from('<I', header, 48)[0]
        if not 1 <= count <= 64:
            raise ValueError('Input section budget exceeded')
        source.seek(PAGE)
        directory = source.read(count * 64)
        if crc32c(directory) != struct.unpack_from('<I', header, 164)[0]:
            raise ValueError('Directory CRC mismatch')
        sections = []
        end = ((PAGE + len(directory) + PAGE - 1) // PAGE) * PAGE
        for at in range(0, len(directory), 64):
            row = bytearray(directory[at:at + 64])
            offset, length = struct.unpack_from('<QQ', row, 16)
            if offset % PAGE or offset < end or offset + length > os.fstat(source.fileno()).st_size:
                raise ValueError('Invalid source section bounds')
            end = offset + length
            sections.append(dict(row=row, source=Path(path), offset=offset, length=length))
        return header, sections


def compose(rasters, output, outdoor=None):
    if not rasters:
        raise ValueError('An authoritative OSM/display package is required')
    header, sections = inspect(rasters[0], 1)
    display_capabilities = struct.unpack_from('<I', header, 176 if struct.unpack_from('<Q', header, 24)[0] == 8 else 140)[0]
    native = {struct.unpack_from('<I', section['row'], 8)[0]: section for section in sections}
    layer_rows = bytearray()
    layer_ids = set()
    for index, path in enumerate(rasters):
        current_header, current = inspect(path, 1)
        if current_header[120:136] != header[120:136]:
            raise ValueError('Raster package coverage must agree')
        descriptor = next(section for section in current if struct.unpack_from('<I', section['row'])[0] == 2)
        with Path(path).open('rb') as source:
            source.seek(descriptor['offset'])
            rows = source.read(descriptor['length'])
        if len(rows) % 96:
            raise ValueError('Invalid layer descriptors')
        for at in range(0, len(rows), 96):
            identity = struct.unpack_from('<I', rows, at)[0]
            if identity in layer_ids:
                raise ValueError('Duplicate raster semantic')
            layer_ids.add(identity)
        layer_rows.extend(rows)
        if index:
            for section in current:
                if struct.unpack_from('<I', section['row'])[0] in (10, 11):
                    identity = struct.unpack_from('<I', section['row'], 8)[0]
                    if identity in native:
                        raise ValueError('Duplicate display section ID')
                    native[identity] = section
    native[2] = dict(row=bytearray(native[2]['row']), data=bytes(layer_rows), length=len(layer_rows))
    struct.pack_into('<Q', native[2]['row'], 32, len(layer_rows) // 96)
    selected = [native[key] for key in sorted(native)]
    capabilities = 16
    if outdoor:
        outdoor_header, outdoor_sections = inspect(outdoor, 2)
        if struct.unpack_from('<Q', outdoor_header, 24)[0]:
            raise ValueError('Expected a native outdoor input, not a composed map')
        selected.extend(outdoor_sections)
        capabilities |= struct.unpack_from('<I', outdoor_header, 140)[0]
        base = struct.unpack_from('<4i', header, 120)
        extra = struct.unpack_from('<4i', outdoor_header, 120)
        struct.pack_into('<4i', header, 120, min(base[0], extra[0]), min(base[1], extra[1]), max(base[2], extra[2]), max(base[3], extra[3]))
    if len(selected) > 128:
        raise ValueError('Unified section budget exceeded')
    body_start = ((PAGE + len(selected) * 64 + PAGE - 1) // PAGE) * PAGE
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise FileExistsError(destination)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.compose-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(bytes(body_start))
            directory = bytearray()
            digest = hashlib.sha256()
            for section in selected:
                offset = stream.tell()
                row = bytearray(section['row'])
                struct.pack_into('<QQ', row, 16, offset, section['length'])
                directory.extend(row)
                if struct.unpack_from('<II', row)[0] == 1:
                    struct.pack_into('<QQ', header, 144, offset, section['length'])
                if 'data' in section:
                    stream.write(section['data']); digest.update(section['data'])
                else:
                    with section['source'].open('rb') as source:
                        source.seek(section['offset']); remaining = section['length']
                        while remaining:
                            chunk = source.read(min(4 * 1024**2, remaining))
                            if not chunk: raise ValueError('Source truncated during composition')
                            stream.write(chunk); digest.update(chunk); remaining -= len(chunk)
                padding = (-stream.tell()) % PAGE
                stream.write(bytes(padding)); digest.update(bytes(padding))
            struct.pack_into('<HH', header, 8, 2, 0)
            struct.pack_into('<Q', header, 24, 8)  # Required native-display directory extension.
            struct.pack_into('<Q', header, 32, stream.tell())
            struct.pack_into('<I', header, 48, len(selected))
            struct.pack_into('<I', header, 176, display_capabilities | (1 if layer_ids else 0))
            struct.pack_into('<I', header, 140, capabilities)
            header[72:88] = hashlib.sha256(directory + digest.digest()).digest()[:16]
            struct.pack_into('<I', header, 164, crc32c(directory))
            struct.pack_into('<I', header, 160, 0)
            struct.pack_into('<I', header, 160, crc32c(header))
            stream.seek(0); stream.write(header)
            stream.seek(PAGE); stream.write(directory)
            stream.flush(); os.fsync(stream.fileno())
        # Linking provides no-replace publication on both Windows and POSIX.
        os.link(temporary, destination)
        report = dict(format='TMAP 2.0 unified native sections', bytes=destination.stat().st_size,
            capabilities=capabilities, sections=len(selected), raster_semantics=sorted(layer_ids),
            sources=[str(Path(path)) for path in rasters], outdoor=str(outdoor) if outdoor else None)
        return report
    finally:
        if temporary: temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raster', action='append', required=True)
    parser.add_argument('--outdoor')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    print(json.dumps(compose(args.raster, args.output, args.outdoor), indent=2))
