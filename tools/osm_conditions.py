"""Strict host-only calendar OSM condition evaluation at an explicit local instant.

Supported conditions: date/month/weekday selectors and clock ranges, including overnight.
No holidays, solar times, weather, vehicle dimensions or implicit timezone.
All clauses are validated, including inactive ones. Last matching clause wins.
"""
import re
from datetime import date, datetime, timedelta

DAYS = {name: i for i, name in enumerate(("Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"))}
MONTHS = {name:i for i,name in enumerate(("Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"))}


def cycle_set(text, names):
    pattern = "(?:"+"|".join(names)+")"
    if not re.fullmatch(pattern+"(?:[-,]"+pattern+")*",text):
        raise ValueError("unsupported calendar selector: " + text)
    result = set()
    for item in text.split(","):
        bounds = item.split("-")
        if len(bounds) > 2: raise ValueError("invalid calendar range")
        start,end = names[bounds[0]],names[bounds[-1]]
        result.update((start+i) % len(names) for i in range((end-start) % len(names)+1))
    return result


def parse_instant(value):
    instant = datetime.fromisoformat(value)
    if instant.tzinfo is None or instant.utcoffset() is None:
        raise ValueError("condition snapshot requires an explicit UTC offset")
    return instant


def date_prefix(condition):
    months = "(?:"+"|".join(MONTHS)+")"
    pattern = (r"(?:(\d{4})\s+)?("+months+r")\s+(\d{2})"
               r"(?:-(?:(\d{4})\s+)?(?:("+months+r")\s+)?(\d{2}))?(?=\s|$)")
    match = re.match(pattern,condition)
    if not match: return None,condition
    year,month,day,end_year,end_month,end_day = match.groups()
    start = (MONTHS[month]+1,int(day))
    end = (MONTHS[end_month or month]+1,int(end_day or day))
    if year is None:
        if end_year is not None: raise ValueError("mixed annual and absolute date endpoints")
        # Leap reference validates recurring Feb 29 without accepting Feb 30.
        date(2000,*start); date(2000,*end)
        def selected(instant):
            value = (instant.month,instant.day)
            return start <= value <= end if start <= end else value >= start or value <= end
    else:
        first = date(int(year),*start)
        last = date(int(end_year or year),*end)
        if last < first: raise ValueError("absolute date range must be ordered")
        def selected(instant): return first <= instant.date() <= last
    return selected,condition[match.end():].strip()


def matches(condition, instant):
    condition = re.sub(r"\s*([,-])\s*",r"\1",condition.strip())
    if condition == "24/7": return True
    dates,remaining = date_prefix(condition)
    words = remaining.split()
    if not words and dates is None: raise ValueError("empty condition")
    months,days = set(range(12)),set(range(7))
    if dates is None and words[0][:3] in MONTHS: months = cycle_set(words.pop(0),MONTHS)
    if words and words[0][:2] in DAYS: days = cycle_set(words.pop(0),DAYS)
    time_text = words.pop(0) if words else None
    if words or (time_text is not None and not re.fullmatch(r"[0-9:,\-]+",time_text)):
        raise ValueError("unsupported OSM condition: " + condition)
    periods = []
    if time_text:
        for period in time_text.split(","):
            if not re.fullmatch(r"\d{2}:\d{2}-\d{2}:\d{2}", period):
                raise ValueError("invalid condition time range")
            values = []
            for clock in period.split("-"):
                hour, minute = map(int, clock.split(":"))
                if minute > 59 or hour > 24 or (hour == 24 and minute):
                    raise ValueError("invalid condition clock")
                values.append(hour*60 + minute)
            if values[0] == values[1] or values[0] == 1440:
                raise ValueError("empty condition range or invalid 24:00 start")
            periods.append(values)
    minute = instant.hour*60 + instant.minute + instant.second/60
    def selected(day): return (dates(day) if dates else day.month-1 in months) and day.weekday() in days
    if not periods: return selected(instant)
    for start,end in periods:
        if start < end:
            if selected(instant) and start <= minute < end: return True
        else:
            # After midnight belongs to the interval started on the previous day.
            if (selected(instant) and minute >= start) or (selected(instant-timedelta(days=1)) and minute < end):
                return True
    return False


def resolve_tags(tags, instant, allowed_modes):
    keys = {"access:conditional"}
    if allowed_modes & 1:
        keys.update(("foot:conditional", "oneway:foot:conditional", "restriction:foot:conditional"))
    if allowed_modes & 2:
        keys.update(("bicycle:conditional", "vehicle:conditional", "oneway:conditional",
                     "oneway:bicycle:conditional", "restriction:conditional", "restriction:bicycle:conditional"))
    result = dict(tags)
    for key in keys:
        if key not in tags: continue
        selected = None
        for clause in tags[key].split(";"):
            match = re.fullmatch(r"\s*([a-z_0-9\-]+)\s*@\s*(?:\(([^()]*)\)|([^()]+))\s*", clause)
            if not match: raise ValueError("unsupported conditional clause: " + clause)
            value = match[1]
            base = key.removesuffix(":conditional")
            valid = ({"yes", "no", "-1"} if base.startswith("oneway") else
                     {"none", "no_left_turn", "no_right_turn", "no_straight_on", "no_u_turn",
                      "only_left_turn", "only_right_turn", "only_straight_on", "only_u_turn", "no_entry", "no_exit"}
                     if base.startswith("restriction") else
                     {"yes", "designated", "permissive", "official", "no", "private", "destination",
                      "customers", "delivery", "agricultural", "forestry", "dismount"})
            if value not in valid: raise ValueError("unsupported conditional value: " + value)
            if matches(match[2] if match[2] is not None else match[3], instant): selected = match[1]
        del result[key]
        if selected is not None:
            base = key.removesuffix(":conditional")
            result[base] = selected
    return result
