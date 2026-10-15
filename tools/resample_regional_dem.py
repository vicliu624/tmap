"""Explicit bounded host DEM resampling; no MCU resampling state."""
from fractions import Fraction
import hashlib
import json
import math
from tmap_pack import e7, point


def resample_dem(dems, target, max_cells=4_000_000, max_samples=32_000_000):
    if not dems: raise ValueError('resampling requires source DEMs')
    max_samples = target.get('max_sample_evaluations', max_samples)
    if not isinstance(max_cells, int) or not isinstance(max_samples, int) or min(max_cells, max_samples) < 1:
        raise ValueError('invalid resampling budgets')
    width, height = target['columns'], target['rows']
    if not isinstance(width, int) or not isinstance(height, int) or min(width, height) < 2:
        raise ValueError('invalid target grid dimensions')
    if width * height > max_cells or width * height * len(dems) > max_samples:
        raise ValueError('DEM resampling work/storage budget exceeded')
    south, west = point(target['origin'])
    dx, dy = map(e7, target['step'])
    if min(dx, dy) <= 0 or west + (width - 1) * dx > 1_800_000_000 or south + (height - 1) * dy > 900_000_000:
        raise ValueError('invalid target grid bounds/step')
    datum = int(dems[0].get('vertical_datum', 0))
    if any(int(d.get('vertical_datum', 0)) != datum for d in dems):
        raise ValueError('vertical datum conversion is required before resampling')
    resolution = int(target['resolution_m'])
    if not 1 <= resolution <= 0xffffffff or any(not 1 <= int(d['resolution_m']) <= 0xffffffff for d in dems) or resolution < max(int(d['resolution_m']) for d in dems):
        raise ValueError('target resolution must not overstate source detail')
    tolerance = target.get('overlap_tolerance_cm', 0)
    if not isinstance(tolerance, int) or tolerance < 0: raise ValueError('invalid overlap tolerance')
    thresholds = {float(d.get('profile_threshold_m', 3)) for d in dems}
    if 'profile_threshold_m' not in target and len(thresholds) != 1:
        raise ValueError('explicit common profile threshold required')
    threshold = float(target.get('profile_threshold_m', next(iter(thresholds))))
    spacing = float(target.get('profile_spacing_m', resolution))
    side = int(target.get('block_side', 32))
    if not math.isfinite(threshold) or threshold < 0 or not math.isfinite(spacing) or spacing <= 0 or not 2 <= side <= 64:
        raise ValueError('invalid target DEM profile/block policy')
    grids = []
    identities = []
    for dem in dems:
        identity = hashlib.sha256(json.dumps(dem, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()
        identities.append(identity)
        lat, lon = point(dem['origin'])
        sx, sy = map(e7, dem['step'])
        rows = dem['heights']
        if min(sx, sy) <= 0 or len(rows) < 2 or len(rows[0]) < 2 or any(len(r) != len(rows[0]) for r in rows):
            raise ValueError('invalid source grid')
        east, north = lon + (len(rows[0]) - 1) * sx, lat + (len(rows) - 1) * sy
        if lon < west or lat < south or east > west + (width - 1) * dx or north > south + (height - 1) * dy:
            raise ValueError('target grid does not cover source region')
        grids.append((int(dem['resolution_m']), sx * sy, identity, lon, lat, sx, sy, rows))
    grids.sort(key=lambda g: g[:3])
    def sample(grid, x, y):
        _, _, _, lon, lat, sx, sy, rows = grid
        x, y = x - lon, y - lat
        nx, ny = len(rows[0]), len(rows)
        if x < 0 or y < 0 or x > (nx - 1) * sx or y > (ny - 1) * sy: return None
        column, row = min(x // sx, nx - 2), min(y // sy, ny - 2)
        fx, fy = x - column * sx, y - row * sy
        numerator = 0
        for yy, wy in ((row, sy - fy), (row + 1, fy)):
            for xx, wx in ((column, sx - fx), (column + 1, fx)):
                if not wx or not wy: continue
                value = rows[yy][xx]
                if value is None: return None
                if not math.isfinite(float(value)) or not -32767 <= round(float(value)) <= 32767:
                    raise ValueError('invalid source elevation')
                numerator += round(float(value)) * wx * wy
        return Fraction(numerator, sx * sy)
    heights = []
    for row in range(height):
        values = []
        for column in range(width):
            selected = None
            for grid in grids:
                value = sample(grid, west + column * dx, south + row * dy)
                if value is None: continue
                if selected is not None and abs(value - selected) * 100 > tolerance:
                    raise ValueError('conflicting resampled overlap')
                if selected is None: selected = value
            values.append(None if selected is None else round(selected))
        heights.append(values)
    result = {key: target[key] for key in ('origin', 'step', 'resolution_m')}
    result.update(heights=heights, vertical_datum=datum, block_side=side,
                  profile_spacing_m=spacing, profile_threshold_m=threshold)
    provenance = {'sources': sorted(identities), 'target': target,
                  'method': 'bilinear-E7-rational-signed-meter-half-even-v1'}
    result['source'] = 'resampled-regions-sha256:' + hashlib.sha256(json.dumps(provenance, sort_keys=True, allow_nan=False).encode()).hexdigest()
    return result
