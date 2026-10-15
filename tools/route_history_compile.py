"""Compile finite route-prefix restrictions into sparse paged transitions."""
import struct


def route_history_sections(graph, edges, edge_index, max_transitions=4_000_000, max_dynamic_matches=4_000_000):
    raw_rules = graph.get('via_restrictions', [])
    dynamic_rules = graph.get('conditional_turns',[])
    if not raw_rules and not dynamic_rules: return []
    if len(raw_rules)+len(dynamic_rules) > 4096: raise ValueError('route history rule budget exceeded')
    rules = set()
    for rule in raw_rules:
        sequence = tuple(edge_index[str(e)] for e in rule['edges'])
        if not 2 <= len(sequence) <= 64: raise ValueError('route history length outside 2..64')
        if any(int(edges[a]['to']) != int(edges[b]['from']) for a,b in zip(sequence,sequence[1:])):
            raise ValueError('route history edges do not connect')
        kind, mode = rule.get('kind','no'), rule.get('access',3)
        if kind not in ('no','only') or type(mode) is not int or mode not in (1,2,3):
            raise ValueError('invalid route history rule')
        mode &= graph.get('travel_modes',3)
        if mode: rules.add((sequence,kind,mode))
    from compile_turn_conditions import compile_turn_conditions
    dynamic_paths = []
    relation_row = 0
    for rule in dynamic_rules:
        if compile_turn_conditions(rule['tags'],rule['mode']) is None: continue
        row = relation_row
        relation_row += 1
        mode = (1 if rule['mode'] == 'foot' else 2) & graph.get('travel_modes',3)
        if not mode: continue
        sequence = tuple(edge_index[str(e)] for e in rule['edges'])
        if not 2 <= len(sequence) <= 64: raise ValueError('conditional history length outside 2..64')
        if any(int(edges[a]['to']) != int(edges[b]['from']) for a,b in zip(sequence,sequence[1:])):
            raise ValueError('conditional history edges do not connect')
        rules.add((sequence,'context',mode))
        dynamic_paths.append((sequence,row,rule['group'],mode))
    if not rules: return []
    prefixes = {()}
    for sequence, _, _ in rules:
        prefixes.update(sequence[:i] for i in range(1,len(sequence)))
    states = sorted(prefixes)
    alphabet = sorted({e for sequence,_,_ in rules for e in sequence})
    if len(states)*len(alphabet) > max_transitions or len(states)*len(edges) >= 0xffffffff:
        raise ValueError('route history transition/key budget exceeded')
    if type(max_dynamic_matches) is not int or not 0 <= max_dynamic_matches <= 0xffffffff:
        raise ValueError('invalid conditional history match budget')
    potential = len(states)*sum(len(path)-1 for path,_,_,_ in dynamic_paths)
    if potential > max_dynamic_matches: raise ValueError('conditional history match work budget exceeded')
    indices = {s:i for i,s in enumerate(states)}
    if len(states) > 65536: raise ValueError('route history state budget exceeded')
    no_end, only_targets = {}, {1:{},2:{}}
    for sequence,kind,mask in rules:
        if kind == 'no': no_end[sequence] = no_end.get(sequence,0) | mask
        elif kind == 'only':
            for length in range(1,len(sequence)):
                for mode in (1,2):
                    if mask & mode: only_targets[mode].setdefault(sequence[:length],set()).add(sequence[length])
    rows, transitions = bytearray(), bytearray()
    for state in states:
        only = {1:{},2:{}}
        for length in range(1,len(state)+1):
            prefix = state[-length:]
            for mode in (1,2):
                if prefix in only_targets[mode]: only[mode][prefix] = only_targets[mode][prefix]
        default = sum(mode for mode in (1,2) if not only[mode])
        first = len(transitions)//16
        for edge in alphabet:
            word = state+(edge,)
            next_state = next(indices[word[start:]] for start in range(len(word)+1) if word[start:] in indices)
            forbidden = 0
            for length in range(1,len(word)+1): forbidden |= no_end.get(word[-length:],0)
            allowed = 0
            for mode in (1,2):
                if any(edge not in choices for choices in only[mode].values()): continue
                if forbidden & mode: continue
                allowed |= mode
            transitions.extend(struct.pack('<4I',edge,next_state,allowed,0))
        rows.extend(struct.pack('<QII',first,len(alphabet),default))
    sections = [(1114,bytes(rows),len(states)),(1115,bytes(transitions),len(transitions)//16)]
    if dynamic_paths:
        match_index, matches = bytearray(),bytearray()
        for state in states:
            active = []
            for sequence,row,group,mode in dynamic_paths:
                for length in range(1,min(len(state),len(sequence)-1)+1):
                    if state[-length:] == sequence[:length]:
                        active.append((group,mode,length,row,sequence[length],int(length+1 == len(sequence))))
            first = len(matches)//16
            for _,_,length,row,next_edge,complete in sorted(active):
                if len(matches)//16 >= max_dynamic_matches: raise ValueError('conditional history match storage budget exceeded')
                matches.extend(struct.pack('<4I',row,length,next_edge,complete))
            match_index.extend(struct.pack('<QII',first,len(active),0))
        sections.extend([(1124,bytes(match_index),len(states)),(1125,bytes(matches),len(matches)//16)])
    return sections
