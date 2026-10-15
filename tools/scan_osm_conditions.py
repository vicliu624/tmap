"""Inventory applicable raw conditions without resolving or altering source tags."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from import_osm import xml_records, tags
from osm_pbf import iter_pbf

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    keys={'access:conditional','foot:conditional','bicycle:conditional','vehicle:conditional',
          'oneway:conditional','oneway:foot:conditional','oneway:bicycle:conditional',
          'restriction:conditional','restriction:foot:conditional','restriction:bicycle:conditional'}
    counts=Counter(); entities=Counter()
    records=iter_pbf(args.source) if args.source.suffix.lower()=='.pbf' else xml_records(args.source)
    for entity in records:
        entities[entity.tag]+=1
        for key,value in tags(entity).items():
            if key in keys:
                counts[entity.tag,key,value]+=1
                if len(counts)>1000: raise ValueError('condition inventory budget exceeded')
    digest=hashlib.sha256()
    with args.source.open('rb') as source:
        for chunk in iter(lambda:source.read(65536),b''): digest.update(chunk)
    report={'source':args.source.name,'source_bytes':args.source.stat().st_size,'source_sha256':digest.hexdigest(),
            'entities':dict(entities),'conditions':[dict(entity=entity,key=key,expression=value,count=count)
                for (entity,key,value),count in sorted(counts.items())],
            'scope':'Raw tag inventory from immutable local snapshot; occurrence is not proof of routable relevance or supported grammar.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
