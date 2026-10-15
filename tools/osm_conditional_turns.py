"""Normalize supported conditional relations without choosing a query time."""
import struct
from compile_turn_conditions import compile_turn_conditions, TURN_VALUES
from osm_via_paths import via_way_sequences

def conditional_turn_modes(tags,allowed_modes):
    result = []
    for mode,mask,keys in (('foot',1,('restriction:foot:conditional',)),
                          ('bicycle',2,('restriction:conditional','restriction:bicycle:conditional'))):
        if not mask & allowed_modes or not any(key in tags for key in keys): continue
        programs = compile_turn_conditions(tags,mode)
        if programs is None: continue
        codes = set()
        for p in programs:
            codes.add(p['fallback'])
            codes.update(struct.unpack_from('<I',p['records'],at+20)[0] for at in range(0,len(p['records']),24))
        values = {TURN_VALUES[code] for code in codes if 0 < code < 11}
        result.append((mode,mask,values))
    return result

def conditional_relation_paths(group,froms,tos,via,tags,modes,way_refs,way_edges,max_paths=4096):
    if type(group) is not int or not 0 < group <= 0xffffffffffffffff:
        raise ValueError('invalid conditional relation group')
    if type(max_paths) is not int or not 0 < max_paths <= 4096:
        raise ValueError('invalid conditional relation path budget')
    if len(froms)*len(tos) > max_paths:
        raise ValueError('conditional relation member-pair budget exceeded')
    output = []
    for mode,mask,values in modes:
        if len(froms) > 1 and any(v != 'no_entry' for v in values): raise ValueError('conditional multiple-from requires no_entry')
        if len(tos) > 1 and any(v != 'no_exit' for v in values): raise ValueError('conditional multiple-to requires no_exit')
        for a in froms:
            if not any(e['access'] & mask for e in way_edges.get(a,[])): continue
            for b in tos:
                if not any(e['access'] & mask for e in way_edges.get(b,[])):
                    if any(v.startswith('only_') for v in values): raise ValueError('conditional only-turn target outside mode topology')
                    continue
                if isinstance(via,tuple):
                    remaining = max_paths-len(output)
                    if not remaining: raise ValueError('conditional relation path budget exceeded')
                    paths = via_way_sequences(a,b,via,way_refs,way_edges,mask,max_paths=remaining)
                else:
                    incoming = (e for e in way_edges.get(a,[]) if e['to'] == via and e['access'] & mask)
                    uturns = {v.endswith('u_turn') for v in values}
                    paths = []
                    for edge in incoming:
                        selected = []
                        for target in way_edges.get(b,[]):
                            if target['from'] != via or not target['access'] & mask: continue
                            if uturns == {True} and target['to'] != edge['from']: continue
                            if selected: raise ValueError('conditional node-via target is ambiguous')
                            selected.append(target)
                        if not selected and any(v.startswith('only_') for v in values): raise ValueError('conditional only-turn target inaccessible')
                        if len(output)+len(paths)+len(selected) > max_paths: raise ValueError('conditional relation path budget exceeded')
                        paths.extend((edge['id'],target['id']) for target in selected)
                if len(output)+len(paths) > max_paths: raise ValueError('conditional relation path budget exceeded')
                output.extend({'edges':list(path),'mode':mode,'tags':dict(tags),'group':group} for path in paths)
    return output
