"""Compile ordered general/mode turn properties without conflating none/inherit."""
from compile_calendar import compile_calendar_rules
import struct

TURN_VALUES = ('none','no_left_turn','no_right_turn','no_straight_on','no_u_turn',
               'no_entry','no_exit','only_left_turn','only_right_turn','only_straight_on','only_u_turn')
TURN_CODES = {name:code for code,name in enumerate(TURN_VALUES)}
INHERIT = 11

def compile_turn_conditions(tags, mode):
    if mode not in ('foot','bicycle'): raise ValueError('unsupported conditional turn mode')
    if mode in {v.strip() for v in tags.get('except','').split(';')}: return None
    general = 'restriction' if mode == 'bicycle' else None
    specific = 'restriction:'+mode
    programs = []
    for key,fallback in ((general,0),(specific,INHERIT)):
        if key is None:
            programs.append({'fallback':0,'records':b''})
            continue
        if key in tags:
            if tags[key] not in TURN_CODES: raise ValueError('unsupported turn property: '+tags[key])
            fallback = TURN_CODES[tags[key]]
        expression = tags.get(key+':conditional')
        records = compile_calendar_rules(expression,TURN_CODES) if expression is not None else b''
        programs.append({'fallback':fallback,'records':records})
    return programs

def encode_turn_program(tags,mode,record_offset=0,max_records=4096):
    if type(record_offset) is not int or not 0 <= record_offset <= 0xffffffffffffffff:
        raise ValueError('invalid turn record offset')
    if type(max_records) is not int or not 0 <= max_records <= 0xffffffff:
        raise ValueError('invalid turn record budget')
    programs = compile_turn_conditions(tags,mode)
    if programs is None: return None
    descriptor = bytearray(struct.pack('<IBBH',1,1 if mode == 'foot' else 2,0,0))
    records = bytearray()
    for p in programs:
        count = len(p['records'])//24
        if len(records)//24+count > max_records: raise ValueError('turn record budget exceeded')
        offset = record_offset+len(records)
        if offset+len(p['records']) > 0xffffffffffffffff: raise ValueError('turn record offset overflow')
        descriptor.extend(struct.pack('<QII',offset,count,p['fallback']))
        records.extend(p['records'])
    return bytes(descriptor),bytes(records)
