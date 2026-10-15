"""Barrier access programs share the bounded seven-property road wire format."""
from compile_road_conditions import encode_road_program

def encode_barrier_program(tags, record_offset=0, max_records=4096):
    barrier = tags.get('barrier')
    if not barrier: raise ValueError('barrier program requires a barrier type')
    # Match the established conservative producer's baseline mode mask.
    highway = 'path' if barrier in ('gate','lift_gate','bollard','cycle_barrier','entrance') else (
        'steps' if barrier in ('stile','kissing_gate') else 'construction')
    policy = {'highway':highway,'oneway':'no','oneway:foot':'no','oneway:bicycle':'no'}
    for key in ('access','vehicle','foot','bicycle'):
        for suffix in ('',':conditional'):
            if key+suffix in tags: policy[key+suffix] = tags[key+suffix]
    return encode_road_program(policy,False,record_offset,max_records)
