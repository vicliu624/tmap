"""Compile the supported OSM calendar grammar to compact MCU conjunctions."""
import re
import struct
from datetime import datetime, timezone
from osm_conditions import DAYS, MONTHS, cycle_set, matches


def compile_calendar(condition):
    if len(condition) > 2048: raise ValueError('calendar expression budget exceeded')
    condition = re.sub(r'\s*([,-])\s*',r'\1',condition.strip())
    matches(condition,datetime(2000,1,1,tzinfo=timezone.utc))  # Validate all selectors.
    predicate = {'first_date':0,'last_date':0,'months':4095,'first_minute':0,'last_minute':1440,'weekdays':127,'date_kind':0}
    if condition == '24/7': return [predicate]
    months = '(?:'+'|'.join(MONTHS)+')'
    pattern = (r'(?:(\d{4})\s+)?('+months+r')\s+(\d{2})'
               r'(?:-(?:(\d{4})\s+)?(?:('+months+r')\s+)?(\d{2}))?(?=\s|$)')
    date = re.match(pattern,condition)
    if date:
        year,month,day,end_year,end_month,end_day = date.groups()
        predicate['date_kind'] = 2 if year is not None else 1
        predicate['first_date'] = (int(year)*512 if year else 0)+(MONTHS[month]+1)*32+int(day)
        predicate['last_date'] = (int(end_year or year)*512 if year else 0)+(MONTHS[end_month or month]+1)*32+int(end_day or day)
        condition = condition[date.end():].strip()
    words = condition.split()
    if not date and words and words[0][:3] in MONTHS:
        predicate['months'] = sum(1 << m for m in cycle_set(words.pop(0),MONTHS))
    if words and words[0][:2] in DAYS:
        predicate['weekdays'] = sum(1 << d for d in cycle_set(words.pop(0),DAYS))
    if not words: return [predicate]
    periods = words[0].split(',')
    if len(periods) > 32: raise ValueError('calendar period budget exceeded')
    output = []
    for period in periods:
        first,last = period.split('-')
        row = dict(predicate)
        row['first_minute'] = sum(int(v)*scale for v,scale in zip(first.split(':'),(60,1)))
        row['last_minute'] = sum(int(v)*scale for v,scale in zip(last.split(':'),(60,1)))
        output.append(row)
    return output


def compile_calendar_rules(expression, action_codes):
    """Compile ordered clauses; action meanings remain explicit caller policy."""
    clauses = expression.split(';')
    if len(expression) > 65536 or len(clauses) > 64: raise ValueError('calendar clause budget exceeded')
    output = bytearray()
    for ordinal,clause in enumerate(clauses):
        match = re.fullmatch(r'\s*([a-z_0-9\-]+)\s*@\s*(?:\(([^()]*)\)|([^()]+))\s*',clause)
        if not match or match[1] not in action_codes: raise ValueError('unsupported conditional clause/action')
        value = action_codes[match[1]]
        if type(value) is not int or not 0 <= value <= 0xffffffff: raise ValueError('invalid calendar action code')
        for p in compile_calendar(match[2] if match[2] is not None else match[3]):
            output.extend(struct.pack('<IIHHHBBII',p['first_date'],p['last_date'],p['months'],p['first_minute'],p['last_minute'],p['weekdays'],p['date_kind'],ordinal,value))
    return bytes(output)
