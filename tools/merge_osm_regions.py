"""Host-only raw regional topology union before junction/turn graph construction.

Stable OSM identities connect seams. No proximity joins or device-side ID tables.
Overlapping entities must agree semantically at E7, including ordered members.
"""
import hashlib
from pathlib import Path
import xml.etree.ElementTree as ET
from tmap_pack import point


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        while chunk := source.read(65536):
            value.update(chunk)
    return value.hexdigest()


def canonical(element):
    kind, identity = element.tag, int(element.attrib['id'])
    if kind not in ('node', 'way', 'relation') or identity <= 0:
        raise ValueError('invalid stable OSM regional identity')
    tags = {}
    for tag in element.findall('tag'):
        key, value = tag.attrib['k'], tag.attrib['v']
        if key in tags:
            raise ValueError('duplicate regional OSM tag')
        tags[key] = value
    if kind == 'node':
        geometry = point([float(element.attrib['lon']), float(element.attrib['lat'])])
    elif kind == 'way':
        geometry = tuple(int(n.attrib['ref']) for n in element.findall('nd'))
        if any(n <= 0 for n in geometry):
            raise ValueError('invalid regional way node reference')
    else:
        geometry = tuple((m.attrib['type'], int(m.attrib['ref']), m.attrib.get('role', '')) for m in element.findall('member'))
        if any(t not in ('node', 'way', 'relation') or n <= 0 for t, n, role in geometry):
            raise ValueError('invalid regional relation member')
    return (kind, identity), (geometry, tuple(sorted(tags.items())))


def union_records(sources, max_entities=1_000_000, max_references=8_000_000):
    if type(max_entities) is not int or max_entities < 1 or type(max_references) is not int or max_references < 1:
        raise ValueError('invalid regional entity budget')
    records = {}
    references = 0
    for source in sources:
        for element in source:
            key, value = canonical(element)
            if key in records:
                if records[key] != value:
                    raise ValueError(f'conflicting regional OSM {key[0]} {key[1]}')
            else:
                if len(records) >= max_entities:
                    raise ValueError('regional entity budget exceeded')
                references += len(value[0]) if key[0] != 'node' else 0
                if references > max_references:
                    raise ValueError('regional reference budget exceeded')
                records[key] = value
    for (kind, identity), (geometry, tags) in sorted(records.items()):
        attributes = {'id':str(identity)}
        if kind == 'node':
            latitude, longitude = geometry
            attributes.update(lat=str(latitude/1e7), lon=str(longitude/1e7))
        element = ET.Element(kind, attributes)
        if kind == 'way':
            for node in geometry:
                ET.SubElement(element, 'nd', ref=str(node))
        elif kind == 'relation':
            for member_kind, member, role in geometry:
                ET.SubElement(element, 'member', type=member_kind, ref=str(member), role=role)
        for key, value in tags:
            ET.SubElement(element, 'tag', k=key, v=value)
        yield element


def import_regions(paths, allowed_modes=3, condition_instant=None, runtime_conditions=False,
                   max_entities=1_000_000, input_format='auto', max_references=8_000_000):
    from import_osm import xml_records, import_records
    from osm_pbf import iter_pbf
    paths = [Path(path) for path in paths]
    if len(paths) < 2 or len(set(p.resolve() for p in paths)) != len(paths):
        raise ValueError('regional import needs distinct source paths')
    identities = [{'path':str(p.resolve()), 'bytes':p.stat().st_size, 'sha256':digest(p)} for p in paths]
    readers = []
    for path in paths:
        format_name = ('pbf' if path.suffix.lower() == '.pbf' else 'xml') if input_format == 'auto' else input_format
        if format_name not in ('xml', 'pbf'):
            raise ValueError('invalid regional source format')
        readers.append(iter_pbf(path) if format_name == 'pbf' else xml_records(path))
    graph = import_records(union_records(readers, max_entities, max_references), allowed_modes, condition_instant, runtime_conditions)
    if any(p.stat().st_size != identity['bytes'] or digest(p) != identity['sha256'] for p, identity in zip(paths, identities)):
        raise ValueError('regional source changed during import')
    return graph, identities
