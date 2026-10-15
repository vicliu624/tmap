"""Bounded conditional relation paths and shared turn programs, graph bit18."""
import struct
from compile_turn_conditions import encode_turn_program

def turn_rule_sections(graph,edges,edge_index,max_rules=4096,max_programs=65536,max_records=4000000):
    rules = graph.get('conditional_turns',[])
    if not isinstance(rules,list): raise ValueError('conditional turn rules must be a list')
    if len(rules) > max_rules: raise ValueError('conditional turn rule budget exceeded')
    if not rules: return []
    descriptors,records,relations,sequences = bytearray(),bytearray(),bytearray(),bytearray()
    identities,groups,paths = {},{},{}
    for rule in rules:
        mode = rule['mode']
        encoded = encode_turn_program(rule['tags'],mode)
        if encoded is None: continue
        group = rule['group']
        if type(group) is not int or not 0 < group <= 0xffffffffffffffff:
            raise ValueError('conditional turn group requires a positive uint64 identity')
        sequence = rule['edges']
        if not isinstance(sequence,list) or not 2 <= len(sequence) <= 64:
            raise ValueError('conditional turn path requires 2..64 directed edges')
        rows = tuple(edge_index[str(edge)] for edge in sequence)
        mask = 1 if mode == 'foot' else 2
        if any(not int(edges[row].get('access',3)) & mask for row in rows):
            raise ValueError('conditional turn path is outside the mode topology')
        if any(int(edges[a]['to']) != int(edges[b]['from']) for a,b in zip(rows,rows[1:])):
            raise ValueError('disconnected conditional turn path')
        identity = encoded
        program = identities.get(identity)
        if program is None:
            program = len(identities)
            if program >= max_programs or program >= 0xffffffff: raise ValueError('turn program budget exceeded')
            if len(records)//24+len(encoded[1])//24 > max_records: raise ValueError('turn condition budget exceeded')
            descriptor,payload = encode_turn_program(rule['tags'],mode,len(records))
            identities[identity] = program
            descriptors.extend(descriptor); records.extend(payload)
        group_key = group,mode
        if group_key in groups and groups[group_key] != program:
            raise ValueError('conditional turn group has inconsistent programs')
        groups[group_key] = program
        offset = paths.get(rows)
        if offset is None:
            offset = len(sequences)
            paths[rows] = offset
            sequences.extend(struct.pack('<'+'I'*len(rows),*rows))
        relations.extend(struct.pack('<QIIQ',offset,len(rows),program,group))
    if not relations: return []
    return [(1120,bytes(descriptors),len(identities)),(1121,bytes(records),len(records)//24),
            (1122,bytes(relations),len(relations)//24),(1123,bytes(sequences),len(sequences)//4)]
