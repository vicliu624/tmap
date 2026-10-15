"""Paged road-rule payloads; graph bit16 requires runtime-aware readers."""
import struct
from compile_road_conditions import encode_road_program
from compile_barrier_conditions import encode_barrier_program


def road_rule_sections(edges, max_programs=65536, max_records=4000000):
    if not any('road_tags' in edge or edge.get('barrier_tags') for edge in edges): return []
    references, descriptors, records, barriers = bytearray(), bytearray(), bytearray(), bytearray()
    identities = {}
    def store(tags,reverse=False,barrier=False):
        encoder = (lambda offset: encode_barrier_program(tags,offset)) if barrier else (
            lambda offset: encode_road_program(tags,reverse,offset))
        canonical,payload = encoder(0)
        identity = canonical,payload
        row = identities.get(identity)
        if row is None:
            row = len(identities)
            if row >= max_programs or row >= 0xffffffff: raise ValueError('road program budget exceeded')
            if len(records)//24 + len(payload)//24 > max_records: raise ValueError('road payload budget exceeded')
            descriptor,payload = encoder(len(records))
            identities[identity] = row
            descriptors.extend(descriptor)
            records.extend(payload)
        return row
    for edge in edges:
        endpoint_barriers = edge.get('barrier_tags',[])
        if not isinstance(endpoint_barriers,list) or len(endpoint_barriers) > 2: raise ValueError('invalid endpoint barrier count')
        rows = [store(tags,barrier=True) for tags in endpoint_barriers]
        rows.extend([0xffffffff]*(2-len(rows)))
        barriers.extend(struct.pack('<II',*rows))
        if 'road_tags' not in edge:
            references.extend(struct.pack('<I',0xffffffff))
            continue
        if 'road_reverse' not in edge: raise ValueError('dynamic road requires explicit direction')
        row = store(edge['road_tags'],edge['road_reverse'])
        references.extend(struct.pack('<I',row))
    sections = [(1116,bytes(references),len(edges)),(1117,bytes(descriptors),len(identities)),
                (1118,bytes(records),len(records)//24)]
    if any(edge.get('barrier_tags') for edge in edges): sections.append((1119,bytes(barriers),len(edges)))
    return sections
