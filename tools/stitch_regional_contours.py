"""Deterministic exact-E7 contour stitching on the producer."""
from tmap_pack import point


def stitch_contours(lines, max_segments=4_000_000, node_intersections=False):
    if not isinstance(max_segments, int) or max_segments < 1: raise ValueError('invalid contour segment budget')
    if not isinstance(node_intersections, bool): raise ValueError('contour noding must be a boolean')
    levels = {}
    count = 0
    for line in lines:
        coordinates = line['points']
        if len(coordinates) < 2: raise ValueError('contour needs at least two points')
        count += len(coordinates) - 1
        if count > max_segments: raise ValueError('contour stitching segment budget exceeded')
        elevation = int(line['elevation_m'])
        edges = levels.setdefault(elevation, set())
        previous = point(coordinates[0])
        for coordinate in coordinates[1:]:
            current = point(coordinate)
            if current != previous: edges.add(tuple(sorted((previous, current))))
            previous = current
    if node_intersections:
        try:
            from shapely.geometry import LineString
            from shapely.ops import unary_union
        except ImportError as error:
            raise ValueError('contour_noding requires the optional Shapely producer dependency') from error
        noded_count = 0
        for elevation, edges in levels.items():
            if not edges: continue
            merged = unary_union([LineString(edge) for edge in sorted(edges)])
            parts = [merged] if merged.geom_type == 'LineString' else list(merged.geoms) if merged.geom_type == 'MultiLineString' else []
            if not parts: raise ValueError('contour noding did not produce line geometry')
            replacement = set()
            for part in parts:
                coordinates = [(round(lat), round(lon)) for lat,lon in part.coords]
                for a,b in zip(coordinates,coordinates[1:]):
                    if a != b: replacement.add(tuple(sorted((a,b))))
            noded_count += len(replacement)
            if noded_count > max_segments: raise ValueError('noded contour output segment budget exceeded')
            levels[elevation] = replacement
    output = []
    for elevation, edges in sorted(levels.items()):
        adjacent = {}
        for a, b in edges:
            adjacent.setdefault(a, set()).add(b)
            adjacent.setdefault(b, set()).add(a)
        used, paths = set(), []
        def trace(start, following):
            path = [start]
            previous, current = start, following
            while True:
                edge = tuple(sorted((previous, current)))
                if edge in used: break
                used.add(edge)
                path.append(current)
                if len(adjacent[current]) != 2: break
                choices = adjacent[current] - {previous}
                following = next(iter(choices))
                previous, current = current, following
            return min(tuple(path), tuple(reversed(path)))
        # Endpoints/junctions delimit chains. A junction is never guessed through.
        for start in sorted(adjacent):
            if len(adjacent[start]) == 2: continue
            for following in sorted(adjacent[start]):
                if tuple(sorted((start, following))) not in used:
                    paths.append(trace(start, following))
        # All remaining edges form closed degree-two cycles.
        for start in sorted(adjacent):
            for following in sorted(adjacent[start]):
                if tuple(sorted((start, following))) not in used:
                    paths.append(trace(start, following))
        for path in sorted(paths):
            output.append({'id':len(output)+1,'elevation_m':elevation,
                           'points':[[lon/1e7,lat/1e7] for lat,lon in path]})
    return output
