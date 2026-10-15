"""Independent datetime oracle for the MCU calendar predicate evaluator."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compile_calendar import compile_calendar, compile_calendar_rules
from osm_conditions import matches


def verify(probe):
    compiled = compile_calendar_rules('yes @ (Mo 08:00-09:00,14:00-15:00); no @ (24/7)',{'yes':1,'no':0})
    if len(compiled) != 72: raise AssertionError('wrong clause/OR record count')
    rows, expected, groups = [], [], []
    def add(utc, offset, p, result=True, status='ok'):
        rows.append(' '.join(map(str,(utc,offset,p['first_date'],p['last_date'],p['months'],p['first_minute'],p['last_minute'],p['weekdays'],p['date_kind']))))
        expected.append((status,bool(result)))
    # Every civil day of one complete Gregorian cycle, independently encoded.
    day = datetime(2000,1,1,12,tzinfo=timezone.utc)
    finish = datetime(2400,1,1,12,tzinfo=timezone.utc)
    cycle = 0
    while day < finish:
        value = day.year*512+day.month*32+day.day
        add(int(day.timestamp()),0,dict(first_date=value,last_date=value,months=1 << (day.month-1),first_minute=0,last_minute=1440,weekdays=1 << day.weekday(),date_kind=2))
        day += timedelta(days=1); cycle += 1
    conditions = ['24/7','Mo-Fr 08:00-10:00','Fr-Mo','Dec-Mar 23:00-02:00',
                  'Jun 01-Oct 01','Dec 31-Jan 02 23:00-02:00','Feb 29',
                  '2026 Jun 01-15','2026 Dec 31-2027 Jan 02','Mo 23:00-02:00',
                  'Mo 08:00-09:00,14:00-15:00','Jan-Mar Mo,We,Fr 00:00-24:00']
    random_source = random.Random(173)
    instants = [datetime(1970,1,1,tzinfo=timezone.utc),datetime(2000,2,29,1,tzinfo=timezone.utc),
                datetime(2100,3,1,tzinfo=timezone.utc),datetime(2027,1,1,1,tzinfo=timezone.utc),
                datetime(9999,12,30,23,59,59,tzinfo=timezone.utc)]
    for _ in range(250):
        year = random_source.choice((1970,1999,2000,2026,2027,2100,2400,9998))
        instants.append(datetime(year,1,1,tzinfo=timezone.utc)+timedelta(days=random_source.randrange(365),seconds=random_source.randrange(86400)))
    for utc_day in instants:
        for offset in (-43200,0,19800,32430,50400):
            local = utc_day.astimezone(timezone(timedelta(seconds=offset)))
            for condition in conditions:
                predicates = compile_calendar(condition)
                first = len(rows)
                for p in predicates: add(int(utc_day.timestamp()),offset,p)
                groups.append((first,len(rows),matches(condition,local)))
    valid = compile_calendar('24/7')[0]
    for field,value in (('months',0),('months',4096),('weekdays',128),('first_minute',1440),
                        ('last_minute',1441),('date_kind',3),('first_date',1)):
        broken = dict(valid); broken[field] = value
        add(0,0,broken,True,'invalid')
    add(0,86400,valid,True,'invalid')
    add(253402300800,0,valid,True,'invalid')
    add(253402300799,1,valid,True,'invalid')
    bad_date = dict(valid,date_kind=2,first_date=2000*512+2*32+30,last_date=2000*512+2*32+30)
    add(0,0,bad_date,True,'invalid')
    reversed_date = dict(valid,date_kind=2,first_date=2027*512+33,last_date=2026*512+33)
    add(0,0,reversed_date,True,'invalid')
    for condition in ('PH','sunrise-sunset','Feb 30','24:00-24:00','wet','2027 Jan 01-2026 Dec 31'):
        try: compile_calendar(condition)
        except ValueError: pass
        else: raise AssertionError('unsupported/invalid expression accepted')
    text = '\n'.join(rows)+'\n'
    process = subprocess.run([str(probe)],input=text,text=True,capture_output=True,check=True)
    actual = [(r.split()[0],bool(int(r.split()[1]))) for r in process.stdout.splitlines()]
    if len(actual) != len(rows): raise AssertionError('calendar output count mismatch')
    for index in range(cycle):
        if actual[index] != expected[index]: raise AssertionError(f'civil cycle mismatch at row {index}')
    for first,last,wanted in groups:
        if any(status != 'ok' for status,_ in actual[first:last]) or any(value for _,value in actual[first:last]) != wanted:
            raise AssertionError(f'grammar/timezone/overnight mismatch at row {first}')
    for index in range(groups[-1][1],len(rows)):
        if actual[index] != expected[index]: raise AssertionError('invalid input changed output or status')
    return {'cycle_days':cycle,'grammar_cases':len(groups),'predicate_evaluations':len(rows),'predicate_bytes':16,
            'input_sha256':hashlib.sha256(text.encode()).hexdigest(),'probe_sha256':hashlib.sha256(probe.read_bytes()).hexdigest(),
            'scope':'Actual C++ calendar primitive against independent Python datetime over a full 400-year cycle and Python OSM grammar/timezone/overnight cases. Routing edge/turn integration remains unfinished; not hardware memory/latency evidence.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('probe',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    report = verify(args.probe.resolve())
    if args.output: args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"PASS {report['cycle_days']} civil days and {report['grammar_cases']} condition cases")
