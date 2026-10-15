"""Bounded property programs for dynamic road access/direction evaluation.

This does not filter topology or encode a TMAP section. Producers must retain both
potential directions until the runtime program has evaluated all seven properties.
"""
from compile_calendar import compile_calendar_rules
import struct
from import_osm import FOOT, BIKE, ALLOWED, DENIED

PROPERTIES = ('access', 'vehicle', 'foot', 'bicycle', 'oneway',
              'oneway:bicycle', 'oneway:foot')
ACCESS = {**dict.fromkeys(ALLOWED, 2), **dict.fromkeys(DENIED, 1), 'dismount': 1}
DIRECTION = {'no': 1, 'yes': 2, '-1': 3}


def compile_road_conditions(tags):
    """Return highway defaults and seven explicit fallback/record programs.

    Access: inherit=0, denied=1, allowed=2. Direction: inherit=0, both=1,
    forward=2, backward=3. Inactive conditions retain each property's fallback;
    inheritance is applied only after every property finishes evaluation.
    """
    highway = int(tags.get('highway') in FOOT) | (int(tags.get('highway') in BIKE) << 1)
    programs = []
    for key in PROPERTIES:
        direction = key.startswith('oneway')
        codes = DIRECTION if direction else ACCESS
        fallback = 1 if key == 'oneway:foot' else (
            2 if key == 'oneway' and tags.get('junction') == 'roundabout' else 1 if key == 'oneway' else 0)
        if key in tags:
            value = tags[key]
            if direction: value = {'1': 'yes', 'true': 'yes', '0': 'no', 'false': 'no'}.get(value, value)
            if value not in codes: raise ValueError('unsupported road property: '+key+'='+value)
            fallback = codes[value]
            # The established producer treats general/vehicle dismount as inherited.
            if key in ('access', 'vehicle') and value == 'dismount': fallback = 0
        expression = tags.get(key+':conditional')
        conditional_codes = codes
        if key in ('access','vehicle'):
            conditional_codes = dict(codes, dismount=0)
        records = compile_calendar_rules(expression, conditional_codes) if expression is not None else b''
        programs.append({'property': key, 'fallback': fallback, 'records': records})
    return highway, programs


def encode_road_program(tags, reverse=False, record_offset=0, max_records=4096):
    """Encode a 120-byte descriptor plus its 24-byte records.

    Header: version u32=1, flags u8=0, highway mask u8, reverse u8, reserved u8=0.
    Seven ranges: logical record-section byte offset u64, count u32, fallback u32.
    This is a wire primitive; graph metadata must advertise runtime rules before
    a producer can publish a navigable package containing these descriptors.
    """
    if type(reverse) is not bool or type(record_offset) is not int or not 0 <= record_offset <= 0xffffffffffffffff:
        raise ValueError('invalid road program direction/offset')
    if type(max_records) is not int or not 0 <= max_records <= 0xffffffff:
        raise ValueError('invalid road record budget')
    highway, programs = compile_road_conditions(tags)
    descriptor = bytearray(struct.pack('<IBBBB',1,0,highway,int(reverse),0))
    records = bytearray()
    for program in programs:
        count = len(program['records'])//24
        if (len(records)//24)+count > max_records: raise ValueError('road record budget exceeded')
        offset = record_offset+len(records)
        if offset+len(program['records']) > 0xffffffffffffffff: raise ValueError('road record offset overflow')
        descriptor.extend(struct.pack('<QII',offset,count,program['fallback']))
        records.extend(program['records'])
    return bytes(descriptor),bytes(records)
