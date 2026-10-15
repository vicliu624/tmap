#!/usr/bin/env python3
"""Build a topology-remapped virtual outdoor package backed by region files.

Host-only preparation. Equal complete pages are borrowed from source packages;
changed pages go into a patch file. This makes no claim that tiny regions save
storage: remapped records can require almost an entire merged package as patch.
Compatible aligned DEM, contours and administrative geometry are retained.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from merge_regions import merge_graphs, regional_contract
from merge_outdoor_regions import merge_outdoor_documents
from tmap_pack import build


def binary_manifest(manifest):
    sources,extents = manifest["sources"],manifest["extents"]
    header = struct.pack("<8sHHIIIQ32s",b"TMV1\r\n\x1a\n",1,64,len(sources),len(extents),0,
                         manifest["virtual_bytes"],bytes.fromhex(manifest["virtual_sha256"]))
    source_rows = b"".join(struct.pack("<Q32s",s["bytes"],bytes.fromhex(s["sha256"])) for s in sources)
    extent_rows = b"".join(struct.pack("<IIQQQ",e["source"],0,e["virtual_offset"],e["source_offset"],e["bytes"]) for e in extents)
    return header+source_rows+extent_rows


def prepare(documents, packages, page_bytes=4096, max_dem_cells=4_000_000, dem_resampling=None, administration_union=False, contour_noding=False,
            raw_source_groups=None, runtime_conditions=False, max_region_entities=1_000_000, max_region_references=8_000_000):
    if len(documents) != len(packages) or len(documents) < 2:
        raise ValueError("at least two paired regional documents/packages required")
    epoch,snapshot,modes = regional_contract([d['graph'] for d in documents])
    for document, package in zip(documents,packages):
        expected,_ = build(document)
        if expected != package: raise ValueError("regional document does not reproduce package")
    raw_identities = None
    graph_override = None
    if raw_source_groups is not None:
        if len(raw_source_groups) != len(documents) or any(not group for group in raw_source_groups):
            raise ValueError('raw source groups must match regional documents')
        from merge_osm_regions import union_records, digest
        from import_osm import import_records, xml_records
        from osm_pbf import iter_pbf
        from osm_conditions import parse_instant
        instant = parse_instant(snapshot) if snapshot is not None else None
        def raw_graph(paths):
            readers = [iter_pbf(path) if Path(path).suffix.lower()=='.pbf' else xml_records(path) for path in paths]
            return import_records(union_records(readers,max_region_entities,max_region_references), modes or 3, instant, runtime_conditions)
        def normalized(graph):
            return {k:sorted(v,key=lambda row:json.dumps(row,sort_keys=True)) if k in ('nodes','edges','restrictions','via_restrictions','conditional_turns') else v
                    for k,v in graph.items() if k not in ('topology_epoch','landmarks')}
        paths = list(dict.fromkeys(str(Path(path).resolve()) for group in raw_source_groups for path in group))
        raw_identities = [{'path':path,'bytes':Path(path).stat().st_size,'sha256':digest(path)} for path in paths]
        for group,document in zip(raw_source_groups,documents):
            if normalized(raw_graph(group)) != normalized(document['graph']):
                raise ValueError('raw sources do not reproduce regional graph semantics')
        graph_override = raw_graph(paths)
        graph_override['topology_epoch'] = epoch
        graph_override['landmarks'] = max(d['graph'].get('landmarks',0) for d in documents)
        if any(Path(row['path']).stat().st_size != row['bytes'] or digest(row['path']) != row['sha256'] for row in raw_identities):
            raise ValueError('raw seam source changed during production')
    merged_document = merge_outdoor_documents(documents, page_bytes, max_dem_cells, dem_resampling, administration_union, contour_noding, graph_override)
    graph = merged_document['graph']
    merged,report = build(merged_document)
    lookup = {}
    for source, package in enumerate(packages):
        for offset in range(0,len(package)-page_bytes+1,page_bytes):
            page = package[offset:offset+page_bytes]
            lookup.setdefault(hashlib.sha256(page).digest(),[]).append((source,offset,page))
    patch = bytearray(); extents = []; reused = 0
    for offset in range(0,len(merged),page_bytes):
        page = merged[offset:offset+page_bytes]
        match = next(((source,start) for source,start,candidate in lookup.get(hashlib.sha256(page).digest(),[])
                      if candidate == page),None)
        if match is None:
            source,start = len(packages),len(patch); patch.extend(page)
        else:
            source,start = match; reused += len(page)
        if extents and extents[-1]["source"] == source and extents[-1]["source_offset"]+extents[-1]["bytes"] == start:
            extents[-1]["bytes"] += len(page)
        else:
            extents.append({"source":source,"virtual_offset":offset,"source_offset":start,"bytes":len(page)})
    if len(extents) > 65536: raise ValueError("virtual extent capacity exceeded")
    sources = [{"bytes":len(p),"sha256":hashlib.sha256(p).hexdigest()} for p in packages+[bytes(patch)]]
    manifest = {"format":"tmap-virtual-graph-v1","page_bytes":page_bytes,"virtual_bytes":len(merged),
                "virtual_sha256":hashlib.sha256(merged).hexdigest(),"topology_epoch":graph["topology_epoch"],
                "condition_snapshot":graph.get("condition_snapshot"),"travel_modes":graph.get("travel_modes"),
                "sources":sources,"extents":extents,"reused_bytes":reused,"patch_bytes":len(patch),
                "scope":"remapped graph; DEM mosaic with optional explicit bounded resampling; regional contour/admin geometry retained",
                "outdoor_capabilities":{"dem":"dem" in merged_document,"contours":"contours" in merged_document,"administration":"administration" in merged_document},
                "dem_resampling":dem_resampling,
                "administration_union":administration_union,
                "contour_stitching":"exact-E7-degree-two-v1",
                "contour_noding":contour_noding,
                "extent_count":len(extents)}
    if raw_identities is not None:
        manifest['raw_seam_sources'] = raw_identities
        manifest['raw_source_groups'] = [[str(Path(p).resolve()) for p in group] for group in raw_source_groups]
        manifest['raw_seam_policy'] = 'Each original source group reproduces its regional graph; stable OSM union rebuilds junctions/restrictions before remapping. No proximity joins.'
        manifest['runtime_conditions'] = runtime_conditions
        manifest['max_region_entities'] = max_region_entities
        manifest['max_region_references'] = max_region_references
    if administration_union or contour_noding:
        import shapely
        manifest['geometry_engine'] = {'shapely':shapely.__version__, 'geos':shapely.geos_version_string}
        if administration_union: manifest['administration_union_engine'] = manifest['geometry_engine']
    return bytes(patch),manifest,merged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config",type=Path,help="JSON regions list, each with document/package paths")
    parser.add_argument("--manifest",type=Path,required=True)
    parser.add_argument("--patch",type=Path,required=True)
    parser.add_argument("--binary-manifest",type=Path)
    parser.add_argument("--page-bytes",type=int,choices=(1024,2048,4096),default=4096)
    parser.add_argument("--max-dem-cells",type=int,default=4_000_000)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8")); base = args.config.parent
    documents = [json.loads((base/r["document"]).read_text(encoding="utf-8")) for r in config["regions"]]
    packages = [(base/r["package"]).read_bytes() for r in config["regions"]]
    groups = [[base/p for p in group] for group in config['raw_source_groups']] if 'raw_source_groups' in config else None
    patch,manifest,_ = prepare(documents,packages,args.page_bytes,args.max_dem_cells,config.get('dem_resampling'),config.get('administration_union',False),config.get('contour_noding',False),
        groups,config.get('runtime_conditions',False),config.get('max_region_entities',1_000_000),config.get('max_region_references',8_000_000))
    for source,region in zip(manifest["sources"],config["regions"]): source["path"] = str((base/region["package"]).resolve())
    manifest["sources"][-1]["path"] = str(args.patch.resolve())
    args.patch.write_bytes(patch)
    if args.binary_manifest:
        raw = binary_manifest(manifest)
        args.binary_manifest.write_bytes(raw)
        manifest["binary_manifest_identity"] = {"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest()}
    args.manifest.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:manifest[k] for k in ("virtual_bytes","reused_bytes","patch_bytes")}))


if __name__ == "__main__": main()
