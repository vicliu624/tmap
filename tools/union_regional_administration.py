"""Optional producer-only polygon union; MCU representation stays streamed E7."""
from tmap_pack import point


def union_administration(features, max_vertices=4_000_000):
    try:
        from shapely.geometry import Polygon
        from shapely.ops import unary_union
    except ImportError as error:
        raise ValueError('administration_union requires the optional Shapely producer dependency') from error
    if not isinstance(max_vertices, int) or max_vertices < 4: raise ValueError('invalid administration vertex budget')
    groups, vertices = {}, 0
    for feature in features:
        path = tuple(str(s).strip() for s in feature['path'])
        if not path or any(not s or '/' in s or '\0' in s for s in path): raise ValueError('invalid administrative path')
        polygon = None
        for raw_ring in feature['rings']:
            vertices += len(raw_ring)
            if vertices > max_vertices: raise ValueError('administration input vertex budget exceeded')
            ring = [point(p) for p in raw_ring]
            if len(ring) < 4 or ring[0] != ring[-1]: raise ValueError('administrative ring must be closed')
            current = Polygon(ring)
            if current.is_empty or not current.is_valid or current.area == 0: raise ValueError('invalid administrative ring geometry')
            # Match the SDK's even-odd ring semantics, including nested islands.
            polygon = current if polygon is None else polygon.symmetric_difference(current)
        if polygon is None or polygon.is_empty or not polygon.is_valid: raise ValueError('empty/invalid administrative feature')
        groups.setdefault(path, []).append(polygon)
    def canonical(raw):
        coordinates = [(round(x), round(y)) for x, y in raw]
        clean = []
        for p in coordinates:
            if not clean or clean[-1] != p: clean.append(p)
        if clean and clean[-1] == clean[0]: clean.pop()
        if len(clean) < 3: raise ValueError('administration collapsed during E7 quantization')
        def rotate(ring):
            index = ring.index(min(ring))
            return tuple(ring[index:] + ring[:index])
        selected = min(rotate(clean), rotate(list(reversed(clean))))
        return selected + (selected[0],)
    result, output_vertices = [], 0
    for path, polygons in sorted(groups.items()):
        merged = unary_union(polygons)
        parts = [merged] if merged.geom_type == 'Polygon' else list(merged.geoms) if merged.geom_type == 'MultiPolygon' else []
        if not parts or not merged.is_valid: raise ValueError('union did not produce valid polygons')
        rings = []
        for polygon in parts:
            shell = canonical(polygon.exterior.coords)
            holes = sorted(canonical(r.coords) for r in polygon.interiors)
            snapped = Polygon(shell, holes)
            if not snapped.is_valid or snapped.is_empty: raise ValueError('union topology changed during E7 quantization')
            output_vertices += len(shell) + sum(map(len, holes))
            if output_vertices > max_vertices: raise ValueError('administration output vertex budget exceeded')
            rings.append((shell, *holes))
        for polygon in sorted(rings):
            result.append({'path':list(path),'rings':[[[lon/1e7,lat/1e7] for lat,lon in ring] for ring in polygon]})
    return result
