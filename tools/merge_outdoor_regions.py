"""Merge aligned outdoor documents without adding device-side region state."""
import copy
import hashlib
import json
from merge_regions import merge_graphs, regional_contract
from tmap_pack import e7, point, extract_contours
from resample_regional_dem import resample_dem
from stitch_regional_contours import stitch_contours


def merge_dem(dems, max_cells=4_000_000):
    if not dems: return None
    if not isinstance(max_cells, int) or max_cells < 4: raise ValueError('invalid DEM cell budget')
    first = dems[0]
    step = tuple(map(e7, first['step']))
    policy = (int(first['resolution_m']), int(first.get('vertical_datum', 0)),
              float(first.get('profile_spacing_m', first['resolution_m'])), float(first.get('profile_threshold_m', 3)))
    if min(step) <= 0: raise ValueError('invalid DEM step')
    grids = []
    for dem in dems:
        rows = dem['heights']
        if len(rows) < 2 or len(rows[0]) < 2 or any(len(r) != len(rows[0]) for r in rows):
            raise ValueError('invalid regional DEM dimensions')
        current = (int(dem['resolution_m']), int(dem.get('vertical_datum', 0)),
                   float(dem.get('profile_spacing_m', dem['resolution_m'])), float(dem.get('profile_threshold_m', 3)))
        if tuple(map(e7, dem['step'])) != step or current != policy:
            raise ValueError('incompatible DEM grid/datum/profile policy; explicit resampling required')
        south, west = point(dem['origin'])
        grids.append((west, south, len(rows[0]), len(rows), dem))
    west, south = min(g[0] for g in grids), min(g[1] for g in grids)
    east = max(x + (nx - 1) * step[0] for x, y, nx, ny, dem in grids)
    north = max(y + (ny - 1) * step[1] for x, y, nx, ny, dem in grids)
    nx, ny = (east - west) // step[0] + 1, (north - south) // step[1] + 1
    if east > 1_800_000_000 or north > 900_000_000: raise ValueError('DEM exceeds geographic bounds')
    if any((x - west) % step[0] or (y - south) % step[1] for x, y, _, _, _ in grids):
        raise ValueError('regional DEM origins are not aligned')
    if nx * ny > max_cells: raise ValueError('merged DEM cell budget exceeded')
    heights = [[None] * nx for _ in range(ny)]
    for x, y, width, height, dem in grids:
        dx, dy = (x - west) // step[0], (y - south) // step[1]
        for row in range(height):
            for column in range(width):
                value = dem['heights'][row][column]
                if value is None: continue
                previous = heights[dy + row][dx + column]
                if previous is not None and previous != value: raise ValueError('conflicting regional DEM overlap')
                heights[dy + row][dx + column] = value
    identities = sorted(hashlib.sha256(json.dumps(d, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest() for d in dems)
    result = copy.deepcopy(first)
    result['origin'] = [west / 1e7, south / 1e7]
    result['step'] = [step[0] / 1e7, step[1] / 1e7]
    result['heights'] = heights
    result['block_side'] = min(int(d.get('block_side', 32)) for d in dems)
    result['source'] = 'regional-dem-sha256:' + hashlib.sha256('\n'.join(identities).encode()).hexdigest()
    return result


def merge_outdoor_documents(documents, page_bytes=4096, max_dem_cells=4_000_000, dem_resampling=None, administration_union=False, contour_noding=False, graph_override=None):
    if not isinstance(administration_union, bool): raise ValueError('administration_union must be a boolean')
    if not isinstance(contour_noding, bool): raise ValueError('contour_noding must be a boolean')
    graphs = [d['graph'] for d in documents]
    if graph_override is not None:
        if regional_contract(graphs) != regional_contract([graph_override]):
            raise ValueError('raw seam graph has incompatible regional contract')
        graph = copy.deepcopy(graph_override)
    else: graph = merge_graphs(graphs)
    result = {'name': 'virtual-regions', 'graph': graph, 'page_bytes': page_bytes}
    dems = [d['dem'] for d in documents if 'dem' in d]
    merged = merge_dem(dems, max_dem_cells) if dem_resampling is None else resample_dem(dems, dem_resampling, max_dem_cells)
    if merged is not None: result['dem'] = merged
    # A common auto-generated layer follows the composed DEM, so resampling and
    # contour geometry share one surface. Explicit source lines stay authoritative.
    contours, administration = {}, {}
    terrain_documents = [d for d in documents if 'dem' in d or 'contours' in d]
    common_contours = merged is not None and all(d.get('contours') is None and d.get('generate_contours', True) and 'dem' in d for d in terrain_documents)
    intervals = {int(d.get('contour_interval_m', 10)) for d in terrain_documents}
    common_contours = common_contours and len(intervals) == 1
    if common_contours:
        for line in extract_contours(merged, next(iter(intervals))):
            contours.setdefault(json.dumps(line, sort_keys=True), line)
    for document in documents:
        lines = document.get('contours')
        if lines is None and not common_contours and 'dem' in document and document.get('generate_contours', True):
            lines = extract_contours(document['dem'], int(document.get('contour_interval_m', 10)))
        for line in lines or []:
            contours.setdefault(json.dumps(line, sort_keys=True), copy.deepcopy(line))
        for feature in document.get('administration', []):
            administration.setdefault(json.dumps(feature, sort_keys=True), copy.deepcopy(feature))
    if contours or dems or any('contours' in d for d in documents):
        result['contours'] = stitch_contours([contours[k] for k in sorted(contours)],node_intersections=contour_noding)
    if administration:
        result['administration'] = [administration[k] for k in sorted(administration)]
        if administration_union:
            from union_regional_administration import union_administration
            result['administration'] = union_administration(result['administration'])
    for key, default in (('contour_lod', True), ('contour_pixel_error', .75), ('contour_storage_zoom', 16)):
        values = [d.get(key, default) for d in documents if 'dem' in d or 'contours' in d]
        if values:
            if any(v != values[0] for v in values): raise ValueError('incompatible contour storage policy')
            result[key] = values[0]
    landmarks = max((d['graph'].get('landmarks', 0) for d in documents), default=0)
    if landmarks: result['graph']['landmarks'] = landmarks
    return result
