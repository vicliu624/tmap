#!/usr/bin/env python3
"""Dependency-free writer for the TMAP 2.0 outdoor profile.

Input JSON uses WGS84 [longitude, latitude], south-to-north DEM rows, meters.
Desktop tool: memory use is proportional to input; embedded readers are bounded.
"""
from __future__ import annotations
import argparse
import hashlib
import heapq
import json
import math
import os
from pathlib import Path
import struct
import tempfile

PAGE, PAYLOAD = 4096, 4064
DEFAULT_PAGE_BYTES = 4096
TERRAIN, DEM_INDEX, DEM_DATA = 1000, 1001, 1002
CONTOUR_INDEX, CONTOUR_DATA = 1010, 1011
CONTOUR_OVERVIEW_INDEX, CONTOUR_OVERVIEW_DATA = 1012, 1013
GRAPH, NODES, EDGES, SHAPES, RESTRICTIONS, SPATIAL, REFS = range(1100, 1107)
ROAD_NAMES = 1107
EDGE_TERRAIN_INDEX, EDGE_TERRAIN_DATA, EDGE_TERRAIN_POLICY = 1108, 1109, 1110
LANDMARK_POLICY, LANDMARK_DISTANCES = 1111, 1112
ADMIN_INDEX, ADMIN_RECORDS, ADMIN_GEOMETRY, ADMIN_PATHS = 1200,1201,1202,1203
U32MAX = 2**32 - 1


def _crc_table():
    table = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0x82F63B78 if value & 1 else 0)
        table.append(value)
    return table


CRC_TABLE = _crc_table()


def crc32c(data):
    value = U32MAX
    for byte in data:
        value = CRC_TABLE[(value ^ byte) & 255] ^ (value >> 8)
    return value ^ U32MAX


def e7(value):
    if not math.isfinite(float(value)):
        raise ValueError("non-finite coordinate")
    return round(float(value) * 10_000_000)


def point(value):
    lon, lat = value
    p = e7(lat), e7(lon)
    if not -900_000_000 <= p[0] <= 900_000_000 or not -1_800_000_000 <= p[1] <= 1_800_000_000:
        raise ValueError("coordinate outside WGS84")
    return p


def bbox(points):
    return min(p[1] for p in points), min(p[0] for p in points), max(p[1] for p in points), max(p[0] for p in points)


def union(boxes):
    return min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes)


def varint(value):
    if not -(2**31) <= value < 2**31:
        raise ValueError("geometry delta exceeds signed 32 bits; split the feature")
    n = (value << 1) ^ (value >> 31)
    result = bytearray()
    while n >= 128:
        result.append((n & 127) | 128)
        n >>= 7
    result.append(n)
    return result


def spatial_tree(leaves, fanout=8):
    """Packed tree: parent precedes contiguous children; each row is 48 bytes."""
    if not leaves:
        return b"", 0
    level = [dict(box=item[0], offset=item[1], size=item[2], count=item[3], level=0,
                  mask=item[4] if len(item)>4 else None) for item in leaves]
    while len(level) > 1:
        level.sort(key=lambda x: ((x["box"][0]+x["box"][2]), (x["box"][1]+x["box"][3])))
        parents = []
        for i in range(0,len(level),fanout):
            children = level[i:i+fanout]
            mask = 0
            for child in children:
                if child["mask"] is None: mask = None; break
                mask |= child["mask"]
            parents.append(dict(box=union([x["box"] for x in children]),children=children,level=children[0]["level"]+1,mask=mask))
        level = parents
    records = [level[0]]
    for node in records:
        if node["level"]:
            node["first"] = len(records)
            records.extend(node["children"])
    output = bytearray()
    for node in records:
        if node["level"]:
            first, count, offset, size = node["first"], len(node["children"]), 0, 0
        else:
            first, count, offset, size = 0, node["count"], node["offset"], node["size"]
        output.extend(struct.pack("<4iIHBBQIIQ", *node["box"], first, count, node["level"], int(node["mask"] is not None), offset, size, node["mask"] or 0, 0))
    return bytes(output), len(records)


def dem_sections(dem):
    west, south = point(dem["origin"])[::-1]
    dx, dy = map(e7, dem["step"])
    rows = dem["heights"]
    ny, nx = len(rows), len(rows[0]) if rows else 0
    side = int(dem.get("block_side", 32))
    resolution = int(dem["resolution_m"])
    datum = int(dem.get("vertical_datum", 0))
    if nx < 2 or ny < 2 or dx <= 0 or dy <= 0 or not 2 <= side <= 64 or resolution <= 0:
        raise ValueError("invalid DEM dimensions, step, block size or resolution")
    if any(len(row) != nx for row in rows):
        raise ValueError("ragged DEM")
    if west+(nx-1)*dx > 1_800_000_000 or south+(ny-1)*dy > 900_000_000:
        raise ValueError("DEM exceeds WGS84 bounds")
    source = hashlib.sha256(str(dem.get("source", "unspecified")).encode()).digest()[:16]
    # Explicit layout is 64 bytes: 36..47 are reserved.
    meta = struct.pack("<4i2I2H3I", west, south, dx, dy, nx, ny, side, 1, resolution, datum, 0) + bytes(8) + source
    index, data = bytearray(), bytearray()
    for y in range(0, ny, side):
        for x in range(0, nx, side):
            w, h = min(side, nx-x), min(side, ny-y)
            start = len(data)
            for row in rows[y:y+h]:
                for z in row[x:x+w]:
                    value = -32768 if z is None else round(float(z))
                    if z is not None and (not math.isfinite(float(z)) or not -32767 <= value <= 32767):
                        raise ValueError("DEM height exceeds signed-meter encoding")
                    data.extend(struct.pack("<h", value))
            index.extend(struct.pack("<IIHHIQII", x, y, w, h, 0, start, len(data)-start, 0))
    return [(TERRAIN, meta, 1), (DEM_INDEX, bytes(index), len(index)//32), (DEM_DATA, bytes(data), nx*ny)]


def extract_contours(dem, interval=10):
    """Marching squares, center saddle decider, stitched shared E7 endpoints.

    Missing cells are skipped. No extrapolation through holes. Levels below sea
    level are supported. Desktop preprocessing; not run on the embedded device.
    """
    if interval <= 0:
        raise ValueError("contour interval must be positive")
    rows = dem["heights"]
    origin = point(dem["origin"])
    dx, dy = map(e7, dem["step"])
    valid = [float(z) for row in rows for z in row if z is not None]
    if not valid:
        return []
    output, next_id = [], 1
    for elevation in range(math.ceil(min(valid)/interval)*interval, math.floor(max(valid)/interval)*interval+1, interval):
        segments = []
        for y in range(len(rows)-1):
            for x in range(len(rows[0])-1):
                zs = [rows[y][x], rows[y][x+1], rows[y+1][x+1], rows[y+1][x]]
                if any(z is None for z in zs):
                    continue
                coords = [(origin[0]+y*dy, origin[1]+x*dx), (origin[0]+y*dy, origin[1]+(x+1)*dx),
                          (origin[0]+(y+1)*dy, origin[1]+(x+1)*dx), (origin[0]+(y+1)*dy, origin[1]+x*dx)]
                crossings = {}
                for edge in range(4):
                    other = (edge+1) % 4
                    if (zs[edge] >= elevation) == (zs[other] >= elevation):
                        continue
                    t = (elevation-zs[edge])/(zs[other]-zs[edge])
                    crossings[edge] = tuple(round(coords[edge][i]+(coords[other][i]-coords[edge][i])*t) for i in range(2))
                if len(crossings) == 2:
                    pairs = [tuple(crossings)]
                elif len(crossings) == 4:
                    center_high = sum(zs)/4 >= elevation
                    pairs = [(0, 1), (2, 3)] if (zs[0] >= elevation) == center_high else [(3, 0), (1, 2)]
                else:
                    continue
                for a, b in pairs:
                    if crossings[a] != crossings[b]:
                        segments.append((crossings[a], crossings[b]))
        adjacency = {}
        for i, (a, b) in enumerate(segments):
            adjacency.setdefault(a, []).append(i)
            adjacency.setdefault(b, []).append(i)
        used = set()
        seeds = sorted(adjacency, key=lambda p: (len(adjacency[p]) != 1, p))
        for seed in seeds:
            while any(i not in used for i in adjacency[seed]):
                path, current = [seed], seed
                while True:
                    candidates = [i for i in adjacency[current] if i not in used]
                    if not candidates:
                        break
                    i = candidates[0]
                    used.add(i)
                    a, b = segments[i]
                    current = b if a == current else a
                    path.append(current)
                if len(path) >= 2:
                    output.append({"id": next_id, "elevation_m": elevation,
                                   "points": [[p[1]/1e7, p[0]/1e7] for p in path]})
                    next_id += 1
    return output


def contour_lod(coords, pixel_error=0.75, max_zoom=18):
    if not math.isfinite(pixel_error) or not 0 < pixel_error <= 2:
        raise ValueError("contour LOD pixel error must be in (0,2]")
    projected = []
    for lat, lon in coords:
        if abs(lat) > 850_511_287: raise ValueError("contour LOD requires Web Mercator latitude coverage")
        projected.append((lon/3_600_000_000, -math.asinh(math.tan(lat/1e7*math.pi/180))/(2*math.pi)))
    levels = [19]*len(coords)
    levels[0] = levels[-1] = 12
    for zoom in (12, 14, 16, 18):
        if zoom > max_zoom: break
        tolerance2 = (pixel_error/(256*2**zoom))**2
        stack = [(0, len(coords)-1)]
        while stack:
            first, last = stack.pop()
            a, b = projected[first], projected[last]
            dx, dy = b[0]-a[0], b[1]-a[1]
            squared = dx*dx+dy*dy
            best, selected = tolerance2, None
            for i in range(first+1, last):
                p = projected[i]
                t = min(1, max(0, ((p[0]-a[0])*dx+(p[1]-a[1])*dy)/squared)) if squared else 0
                distance2 = (p[0]-a[0]-t*dx)**2+(p[1]-a[1]-t*dy)**2
                if distance2 > best:
                    best, selected = distance2, i
            if selected is not None:
                levels[selected] = min(levels[selected], zoom)
                stack.extend(((first, selected), (selected, last)))
    return [(p, level) for p, level in zip(coords, levels) if level <= max_zoom]


def split_contour(detailed, zoom):
    scale = 2**zoom
    def xy(p):
        lat,lon = p
        return (lon/3_600_000_000+0.5)*scale,(1-math.asinh(math.tan(lat/1e7*math.pi/180))/math.pi)/2*scale
    def geographic(x,y):
        return round(math.atan(math.sinh(math.pi*(1-2*y/scale)))*180/math.pi*1e7),round((x/scale-0.5)*3_600_000_000)
    current, chain = None, []
    for (a,la),(b,lb) in zip(detailed,detailed[1:]):
        ax,ay = xy(a); bx,by = xy(b)
        breaks = [0.,1.]
        for start,end in ((ax,bx),(ay,by)):
            if abs(end-start) < 1e-12: continue
            first,last = math.floor(min(start,end))+1,math.ceil(max(start,end))
            if last-first > 4096: raise ValueError("contour segment spans too many storage cells")
            breaks.extend((grid-start)/(end-start) for grid in range(first,last))
        breaks = sorted(set(breaks))
        for lo,hi in zip(breaks,breaks[1:]):
            if hi-lo < 1e-12: continue
            mid = (lo+hi)/2
            key = math.floor(ax+(bx-ax)*mid),math.floor(ay+(by-ay)*mid)
            p = a if lo == 0 else geographic(ax+(bx-ax)*lo,ay+(by-ay)*lo)
            q = b if hi == 1 else geographic(ax+(bx-ax)*hi,ay+(by-ay)*hi)
            left,right = (p,la if lo == 0 else 12),(q,lb if hi == 1 else 12)
            if p == q: continue
            if key == current and chain and chain[-1][0] == p: chain.append(right)
            else:
                if chain: yield current,chain
                current,chain = key,[left,right]
    if chain: yield current,chain


def contour_sections(contours, lod=True, pixel_error=0.75, storage_zoom=16, max_zoom=18,
                     index_type=CONTOUR_INDEX,data_type=CONTOUR_DATA):
    if not 12 <= storage_zoom <= 18: raise ValueError("contour storage zoom must be in 12..18")
    chunks = []
    for feature in contours:
        coords = [point(p) for p in feature["points"]]
        if len(coords) < 2 or int(feature["id"]) <= 0:
            raise ValueError("contour needs ID and at least two points")
        detailed = contour_lod(coords,pixel_error,max_zoom) if lod else [(p, 12) for p in coords]
        elevation = int(feature["elevation_m"])
        band = next((v for v in (100,50,20,10,5) if elevation%v == 0),1)
        mask = 1 << ((elevation%100)//5) if elevation%5 == 0 else 1 << 31
        pieces = split_contour(detailed,storage_zoom) if lod else [((0,0),detailed)]
        for cell,piece in pieces:
            for start in range(0,len(piece)-1,127):
                chunk = piece[start:start+128]
                encoded = bytearray()
                lat = lon = 0
                for i,(p,level) in enumerate(chunk):
                    encoded.extend(varint(p[0]-lat)); encoded.extend(varint(p[1]-lon))
                    if lod: encoded.append(12 if i in (0,len(chunk)-1) else level)
                    lat,lon = p
                record = struct.pack("<QiIII",int(feature["id"]),elevation,len(chunk),len(encoded),int(lod))+encoded
                chunks.append((cell,band,bbox([p for p,_ in chunk]),record,mask))
    chunks.sort(key=lambda x: (x[0],-x[1],x[2][0]+x[2][2],x[2][1]+x[2][3]))
    leaves, data = [], bytearray()
    i = 0
    while i < len(chunks):
        end = i+1
        while end < min(i+16,len(chunks)) and chunks[end][:2] == chunks[i][:2]: end += 1
        group = chunks[i:end]
        start = len(data)
        mask = 0
        for _,_,_,record,part_mask in group:
            data.extend(record)
            mask |= part_mask
        leaves.append((union([item[2] for item in group]),start,len(data)-start,len(group),mask if lod else None))
        i = end
    tree, count = spatial_tree(leaves)
    return [(index_type, tree, count), (data_type, bytes(data), len(chunks))]


def distance(a, b):
    lat1, lon1 = map(lambda v: v/1e7*math.pi/180, a)
    lat2, lon2 = map(lambda v: v/1e7*math.pi/180, b)
    h = math.sin((lat2-lat1)/2)**2+math.cos(lat1)*math.cos(lat2)*math.sin((lon2-lon1)/2)**2
    return 12_742_000*math.asin(math.sqrt(min(1, max(0, h))))


def elevation_at(dem, p):
    lat, lon = p
    origin = point(dem["origin"])
    dx, dy = map(e7, dem["step"])
    rows = dem["heights"]
    x, y = (lon-origin[1])/dx, (lat-origin[0])/dy
    if x < 0 or y < 0 or x > len(rows[0])-1 or y > len(rows)-1: return None
    ix, iy = min(int(x), len(rows[0])-2), min(int(y), len(rows)-2)
    fx, fy = x-ix, y-iy
    weights = [(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy]
    value = 0
    for n, w in enumerate(weights):
        if w == 0: continue
        z = rows[iy+n//2][ix+n%2]
        if z is None: return None
        value += round(float(z))*w
    return value


def edge_terrain(dem, path):
    spacing = float(dem.get("profile_spacing_m", dem["resolution_m"]))
    threshold = float(dem.get("profile_threshold_m", 3))
    if not math.isfinite(spacing) or spacing <= 0 or not math.isfinite(threshold) or threshold < 0:
        raise ValueError("invalid road terrain sampling policy")
    accumulated, ascent, descent, accepted = 0., 0., 0., None
    samples = []
    for segment, (a, b) in enumerate(zip(path, path[1:])):
        length = distance(a, b)
        steps = max(1, math.ceil(length/spacing))
        if steps > 10_000_000: raise ValueError("road terrain sampling budget exceeded")
        for i in range(0 if segment == 0 else 1, steps+1):
            t = i/steps
            p = tuple(round(a[k]+(b[k]-a[k])*t) for k in range(2))
            z = elevation_at(dem, p)
            if z is None: return None
            if accepted is None: accepted = z
            elif abs(z-accepted) >= threshold:
                delta = z-accepted
                ascent += max(0, delta); descent += max(0, -delta); accepted = z
            sample = (round((accumulated+length*t)*100), round(ascent*100), round(descent*100))
            if samples and samples[-1][0] == sample[0]: samples[-1] = sample
            else: samples.append(sample)
        accumulated += length
    return samples


def reverse_terrain(profile):
    """Reverse one cumulative model; swap gain/loss without resampling DEM."""
    if profile is None: return None
    length,ascent,descent = profile[-1]
    return [(length-distance,descent-down,ascent-up) for distance,up,down in reversed(profile)]


def landmark_sections(nodes, edge_data, count):
    if not isinstance(count,int) or not 0 <= count <= 8:
        raise ValueError("landmark count must be 0..8")
    if not count: return []
    count = min(count,len(nodes))
    forward, reverse = [[] for _ in nodes], [[] for _ in nodes]
    for offset in range(0,len(edge_data),48):
        a,b = struct.unpack_from("<II",edge_data,offset)
        weight = struct.unpack_from("<I",edge_data,offset+12)[0]
        forward[a].append((b,weight)); reverse[b].append((a,weight))
    def distances(start,adjacency):
        result = [U32MAX]*len(nodes); result[start] = 0; queue = [(0,start)]
        while queue:
            cost,node = heapq.heappop(queue)
            if cost != result[node]: continue
            for other,weight in adjacency[node]:
                value = cost+weight
                if value < result[other]:
                    result[other] = value; heapq.heappush(queue,(value,other))
        return result
    # Deterministic spread across spatially sorted nodes; exact directed distances.
    order = sorted(range(len(nodes)),key=lambda i:tuple(nodes[i]["point"]))
    landmarks = [order[round(i*(len(order)-1)/max(1,count-1))] for i in range(count)]
    rows = [(distances(i,forward),distances(i,reverse)) for i in landmarks]
    data = b"".join(struct.pack("<II",a[node],b[node]) for node in range(len(nodes)) for a,b in rows)
    identity = hashlib.sha256(edge_data).digest()[:16]
    return [(LANDMARK_POLICY,struct.pack("<4I16s",count,len(nodes),1,0,identity),1),
            (LANDMARK_DISTANCES,data,len(nodes))]


def graph_sections(graph, dem=None):
    nodes = graph["nodes"]
    if not nodes:
        raise ValueError("graph needs nodes")
    ids = [int(n["id"]) for n in nodes]
    if len(set(ids)) != len(ids) or any(i <= 0 for i in ids):
        raise ValueError("node IDs must be positive and unique")
    node_index = {i: n for n, i in enumerate(ids)}
    coords = [point(n["point"]) for n in nodes]
    edges = sorted(graph.get("edges", []), key=lambda e: (node_index[int(e["from"])], str(e["id"])))
    edge_index = {str(e["id"]): i for i, e in enumerate(edges)}
    if len(edge_index) != len(edges):
        raise ValueError("edge IDs must be unique")
    for e in edges:
        opposite = edge_index.get(str(e.get("opposite", "")))
        if opposite is not None:
            other = edges[opposite]
            if int(e["from"]) != int(other["to"]) or int(e["to"]) != int(other["from"]) or str(other.get("opposite")) != str(e["id"]):
                raise ValueError("opposite edges must be reciprocal and join reversed endpoints")
            a = [point(p) for p in e.get("points", [nodes[node_index[int(e["from"])]]["point"], nodes[node_index[int(e["to"])]]["point"]])]
            b = [point(p) for p in other.get("points", [nodes[node_index[int(other["from"])]]["point"], nodes[node_index[int(other["to"])]]["point"]])]
            if a != b[::-1]: raise ValueError("opposite edges must share reversed geometry")
    shape_data, edge_data, refs, names = bytearray(), bytearray(), bytearray(), bytearray()
    terrain_index, terrain_data = bytearray(), bytearray()
    name_refs = {}
    shapes = {}
    terrain_profiles = {}
    leaves = []
    outgoing = [[] for _ in nodes]
    for i, edge in enumerate(edges):
        a, b = node_index[int(edge["from"])], node_index[int(edge["to"])]
        path = tuple(point(p) for p in edge.get("points", [nodes[a]["point"], nodes[b]["point"]]))
        if len(path) < 2 or path[0] != coords[a] or path[-1] != coords[b]:
            raise ValueError("edge geometry must run from source node to target node")
        if any(abs(x[1]-y[1]) > 1_800_000_000 for x,y in zip(path,path[1:])):
            raise ValueError("antimeridian-crossing road geometry needs a supported split profile")
        access = int(edge.get("access", 3))
        if access not in (1, 2, 3):
            raise ValueError("invalid access mask")
        reverse = tuple(reversed(path))
        key, reversed_flag = (reverse, 1) if reverse < path else (path, 0)
        if key not in shapes:
            shapes[key] = len(shape_data)
            shape_data.extend(b"".join(struct.pack("<2i", *p) for p in key))
        length = int(round(sum(distance(x, y) for x, y in zip(key, key[1:]))*100))
        if not 0 < length <= U32MAX:
            raise ValueError("edge length outside encoding")
        opposite = edge_index.get(str(edge.get("opposite", "")), U32MAX)
        profile = None
        if dem is not None:
            if key in terrain_profiles:
                cached = terrain_profiles[key]
                if cached is not None:
                    offset,count,direction = cached
                    profile = [struct.unpack_from("<III",terrain_data,offset+j*12) for j in range(count)]
                    if direction != reversed_flag: profile = reverse_terrain(profile)
            else:
                profile = edge_terrain(dem,key)
                if reversed_flag: profile = reverse_terrain(profile)
                terrain_profiles[key] = (len(terrain_data),len(profile),reversed_flag) if profile else None
        if ("ascent_m" in edge) != ("descent_m" in edge):
            raise ValueError("explicit edge terrain needs both ascent and descent")
        known = profile is not None or "ascent_m" in edge
        ascent, descent = profile[-1][1:] if profile else [int(round(float(edge.get(k, 0))*100)) for k in ("ascent_m", "descent_m")]
        if min(ascent, descent) < 0:
            raise ValueError("negative ascent/descent")
        terrain_index.extend(struct.pack("<QII", len(terrain_data) if profile else 0, len(profile) if profile else 0, 0))
        if profile:
            terrain_data.extend(b"".join(struct.pack("<III", *sample) for sample in profile))
        name = str(edge.get("name", ""))
        if name and name not in name_refs:
            encoded_name = name.encode("utf-8")
            if len(encoded_name) > 512 or "\0" in name:
                raise ValueError("road name exceeds UTF-8 limit or contains NUL")
            name_refs[name] = len(names)+1
            names.extend(struct.pack("<H", len(encoded_name))+encoded_name)
        edge_data.extend(struct.pack("<IIBBHIIIQIIQ", a, b, access, int(edge.get("road_class", 0)), reversed_flag | (int(known) << 1),
                                     length, ascent, descent, shapes[key], len(path), opposite, name_refs.get(name, 0)))
        outgoing[a].append(i)
        start = len(refs); refs.extend(struct.pack("<I", i))
        leaves.append((bbox(path), start, 4, 1))
    node_data = bytearray()
    first = 0
    for i, node in enumerate(nodes):
        height = node.get("elevation_m", elevation_at(dem, coords[i]) if dem else None)
        node_data.extend(struct.pack("<Q2iIIiI", ids[i], *coords[i], first, len(outgoing[i]),
                                     round(height) if height is not None else 0, int(height is not None) | (int(bool(node.get("boundary",False))) << 1)))
        first += len(outgoing[i])
    restrictions = []
    per_incoming = {}
    for restriction in graph.get("restrictions", []):
        a, b = edge_index[str(restriction["from"])], edge_index[str(restriction["to"])]
        if int(edges[a]["to"]) != int(edges[b]["from"]):
            raise ValueError("restriction edges do not meet")
        modes = int(restriction.get("access", 3))
        kind = {"no": 0, "only": 1}[restriction.get("kind", "no")]
        if modes not in (1, 2, 3):
            raise ValueError("invalid restriction mode")
        per_incoming[a] = per_incoming.get(a, 0)+1
        if per_incoming[a] > 64:
            raise ValueError("more than 64 restrictions per incoming edge")
        restrictions.append((a, b, modes, kind))
    restriction_data = b"".join(struct.pack("<IIBBHI", a, b, mode, kind, 0, 0) for a, b, mode, kind in sorted(restrictions))
    tree, count = spatial_tree(leaves)
    epoch = graph.get("topology_epoch")
    identity = "static-foot-bike-v1:"+str(epoch) if epoch else json.dumps(graph,sort_keys=True)
    if "travel_modes" in graph or "condition_snapshot" in graph:
        identity += ":profile:"+json.dumps({"travel_modes":graph.get("travel_modes",3),
                                          "condition_snapshot":graph.get("condition_snapshot")},sort_keys=True)
    if graph.get('via_restrictions'): identity += ':history:'+json.dumps(graph['via_restrictions'],sort_keys=True)
    if any('road_tags' in e for e in edges):
        identity += ':runtime-roads:'+json.dumps([(str(e['id']),e.get('road_tags'),e.get('road_reverse'))
                                                for e in edges if 'road_tags' in e],sort_keys=True)
    if any(e.get('barrier_tags') for e in edges):
        identity += ':runtime-barriers:'+json.dumps([(str(e['id']),e.get('barrier_tags'))
                                                   for e in edges if e.get('barrier_tags')],sort_keys=True)
    if graph.get('conditional_turns'): identity += ':runtime-turns:'+json.dumps(graph['conditional_turns'],sort_keys=True)
    topology = hashlib.sha256(identity.encode()).digest()[:16]
    history_modes = 0
    for rule in graph.get('via_restrictions',[]): history_modes |= rule.get('access',3) & graph.get('travel_modes',3)
    from road_rule_sections import road_rule_sections
    road_sections = road_rule_sections(edges)
    from turn_rule_sections import turn_rule_sections
    turn_sections = turn_rule_sections(graph,edges,edge_index)
    if turn_sections:
        from compile_turn_conditions import compile_turn_conditions
        for rule in graph.get('conditional_turns',[]):
            if compile_turn_conditions(rule['tags'],rule['mode']) is not None:
                history_modes |= (1 if rule['mode'] == 'foot' else 2) & graph.get('travel_modes',3)
    if (road_sections or turn_sections) and graph.get('condition_snapshot'):
        raise ValueError('runtime road rules cannot use a filtered conditional snapshot')
    meta = struct.pack("<4I16s", len(nodes), len(edges), 3 | (history_modes << 8) | (65536 if road_sections else 0) |
                       (131072 if any(s[0] == 1119 for s in road_sections) else 0) |
                       (262144 if turn_sections else 0), int(bool(epoch)), topology)
    sections = [(GRAPH, meta, 1), (NODES, bytes(node_data), len(nodes)), (EDGES, bytes(edge_data), len(edges)),
            (SHAPES, bytes(shape_data), len(shapes)), (RESTRICTIONS, restriction_data, len(restrictions)),
            (SPATIAL, tree, count), (REFS, bytes(refs), len(edges)), (ROAD_NAMES, bytes(names), len(name_refs))]
    sections.extend(landmark_sections(nodes,edge_data,graph.get("landmarks",0)))
    from route_history_compile import route_history_sections
    sections.extend(route_history_sections(graph, edges, edge_index))
    sections.extend(road_sections)
    sections.extend(turn_sections)
    if "travel_modes" in graph or "condition_snapshot" in graph:
        modes = graph.get("travel_modes",3)
        if type(modes) is not int or modes not in (1,2,3): raise ValueError("invalid routing travel modes")
        seconds, offset, flags = 0, 0, 0
        if "condition_snapshot" in graph:
            from osm_conditions import parse_instant
            instant = parse_instant(graph["condition_snapshot"])
            seconds = int(instant.timestamp())
            offset = int(instant.utcoffset().total_seconds())
            if seconds < 0: raise ValueError("routing snapshots before Unix epoch unsupported")
            flags = 1
        sections.append((1113,struct.pack("<IIQiI",1,flags,seconds,offset,modes),1))
    if dem:
        policy = struct.pack("<4I16s", round(float(dem.get("profile_spacing_m", dem["resolution_m"]))*100),
                             round(float(dem.get("profile_threshold_m",3))*100), int(dem.get("vertical_datum",0)),0,
                             hashlib.sha256(str(dem.get("source", "unspecified")).encode()).digest()[:16])
        sections.extend([(EDGE_TERRAIN_INDEX, bytes(terrain_index), len(edges)),
                         (EDGE_TERRAIN_DATA, bytes(terrain_data), len(terrain_data)//12), (EDGE_TERRAIN_POLICY, policy, 1)])
    return sections


def administrative_sections(features):
    if not features: raise ValueError("administration needs features")
    records,geometry,paths = bytearray(),bytearray(),bytearray()
    references,leaves,boxes = {},[],[]
    for feature in features:
        segments = [str(s).strip() for s in feature["path"]]
        if not 1 <= len(segments) <= 32 or any(not s or "/" in s or "\0" in s for s in segments):
            raise ValueError("invalid administrative hierarchy segments")
        path = " / ".join(segments).encode("utf-8")
        if len(path) > 512: raise ValueError("administrative path too long")
        if path not in references:
            references[path] = len(paths)+1
            paths.extend(struct.pack("<H",len(path))+path)
        rings = [tuple(point(p) for p in ring) for ring in feature["rings"]]
        if not rings or any(len(r) < 4 or r[0] != r[-1] for r in rings):
            raise ValueError("administrative rings must be closed with >=4 vertices")
        if any(a == b or abs(a[1]-b[1]) > 1_800_000_000 for ring in rings for a,b in zip(ring,ring[1:])):
            raise ValueError("zero-length/antimeridian administrative edge")
        if any(sum(a[1]*b[0]-b[1]*a[0] for a,b in zip(ring,ring[1:])) == 0 for ring in rings):
            raise ValueError("zero-area administrative ring")
        box = bbox([p for ring in rings for p in ring]); start = len(geometry)
        for ring in rings:
            geometry.extend(struct.pack("<I",len(ring)))
            geometry.extend(b"".join(struct.pack("<2i",*p) for p in ring))
        offset = len(records)
        records.extend(struct.pack("<4iQIIQB7x",*box,start,len(rings),sum(map(len,rings)),references[path],len(segments)))
        leaves.append((box,offset,48,1)); boxes.append(box)
    tree,count = spatial_tree(leaves)
    return [(ADMIN_INDEX,tree,count),(ADMIN_RECORDS,bytes(records),len(features)),
            (ADMIN_GEOMETRY,bytes(geometry),sum(len(f["rings"]) for f in features)),(ADMIN_PATHS,bytes(paths),len(references))],union(boxes)


def paged(section_type, raw, page_bytes=4096):
    output = bytearray()
    capacity = page_bytes-32
    for i, offset in enumerate(range(0, len(raw), capacity)):
        payload = raw[offset:offset+capacity]
        page = bytearray(page_bytes)
        struct.pack_into("<4s4I", page, 0, b"TPG2", section_type, i, len(payload), 0)
        page[32:32+len(payload)] = payload
        struct.pack_into("<I", page, 16, crc32c(page))
        output.extend(page)
    return bytes(output)


def build(document):
    page_bytes = document.get("page_bytes",DEFAULT_PAGE_BYTES)
    if page_bytes not in (1024,2048,4096): raise ValueError("page_bytes must be 1024/2048/4096")
    sections, capabilities, covered = [], 0, []
    if "dem" in document:
        dem = document["dem"]
        sections.extend(dem_sections(dem)); capabilities |= 1
        origin = point(dem["origin"]); dx, dy = map(e7, dem["step"])
        covered.append((origin[1], origin[0], origin[1]+(len(dem["heights"][0])-1)*dx, origin[0]+(len(dem["heights"])-1)*dy))
    contours = document.get("contours")
    if contours is None and "dem" in document and document.get("generate_contours", True):
        contours = extract_contours(document["dem"], int(document.get("contour_interval_m", 10)))
    if contours is not None:
        sections.extend(contour_sections(contours, bool(document.get("contour_lod",True)),
                                        float(document.get("contour_pixel_error",0.75)),int(document.get("contour_storage_zoom",16)))); capabilities |= 2
        if document.get("contour_lod",True) and document.get("contour_overview",True):
            overview = [f for f in contours if int(f["elevation_m"])%20 == 0 or int(f["elevation_m"])%50 == 0]
            sections.extend(contour_sections(overview,True,float(document.get("contour_pixel_error",0.75)),14,14,
                                            CONTOUR_OVERVIEW_INDEX,CONTOUR_OVERVIEW_DATA))
        covered.extend(bbox([point(p) for p in f["points"]]) for f in contours)
    if "graph" in document:
        sections.extend(graph_sections(document["graph"], document.get("dem"))); capabilities |= 4
        covered.append(bbox([point(n["point"]) for n in document["graph"]["nodes"]]))
    if "administration" in document:
        administrative,box = administrative_sections(document["administration"])
        sections.extend(administrative); covered.append(box); capabilities |= 8
    if not sections or not covered:
        raise ValueError("empty package")
    box = tuple(map(e7, document["bounds"])) if "bounds" in document else union(covered)
    if box[0] == box[2]: box = (box[0]-1, box[1], box[2]+1, box[3])
    if box[1] == box[3]: box = (box[0], box[1]-1, box[2], box[3]+1)
    if box[0] >= box[2] or box[1] >= box[3] or not -1_800_000_000 <= box[0] < box[2] <= 1_800_000_000 or not -900_000_000 <= box[1] < box[3] <= 900_000_000:
        raise ValueError("invalid package bounds")
    sections.sort()
    directory, body, report = bytearray(), bytearray(), {}
    if len(sections) > 64: raise ValueError("more than 64 outdoor sections")
    body_offset = ((page_bytes+len(sections)*64+page_bytes-1)//page_bytes)*page_bytes
    offset = body_offset
    for section_type, raw, count in sections:
        payload = paged(section_type, raw,page_bytes)
        directory.extend(struct.pack("<IHHII6Q", section_type, 1, 2, section_type, 0, offset, len(payload), count, 0, len(raw), 0))
        report[str(section_type)] = {"logical_bytes": len(raw), "stored_bytes": len(payload), "records": count}
        body.extend(payload); offset += len(payload)
    header = bytearray(body_offset)
    header[:8] = b"TMAP\r\n\x1a\n"
    struct.pack_into("<HHIIIQQQII", header, 8, 2, 0, 256, 0x01020304, page_bytes, 0, offset, page_bytes, len(sections), 64)
    identity = hashlib.sha256(str(document.get("name", "outdoor")).encode()).digest()[:16]
    header[56:72] = identity
    header[72:88] = hashlib.sha256(directory+body).digest()[:16]
    header[88:104] = hashlib.sha256(str(document.get("series", "outdoor-v2")).encode()).digest()[:16]
    struct.pack_into("<Q", header, 104, int(document.get("revision", 1)))
    struct.pack_into("<4iII", header, 120, *box, sum(1 << z for z in range(12, 19)), capabilities)
    struct.pack_into("<I", header, 164, crc32c(directory))
    struct.pack_into("<I", header, 160, crc32c(header[:256]))
    header[page_bytes:page_bytes+len(directory)] = directory
    return bytes(header+body), {"bytes": offset, "page_bytes":page_bytes,"capabilities": capabilities, "sections": report}


def write(document, output):
    data, report = build(document)
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=destination.name+".", delete=False) as file:
            name = file.name
            file.write(data); file.flush(); os.fsync(file.fileno())
        os.replace(name, destination)
    finally:
        if name and os.path.exists(name): os.unlink(name)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--page-bytes",type=int,choices=(1024,2048,4096))
    args = parser.parse_args()
    document = json.loads(args.input.read_text(encoding="utf-8"))
    if args.page_bytes: document["page_bytes"] = args.page_bytes
    report = write(document, args.output)
    text = json.dumps(report, indent=2)
    if args.report: args.report.write_text(text+"\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
