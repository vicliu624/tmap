"""Compose verified real HGT crop and original OSM administration for Center."""
import argparse
import hashlib
import json
from pathlib import Path
from import_hgt import read_hgt


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('hgt',type=Path); parser.add_argument('administration',type=Path)
    parser.add_argument('graph_document',type=Path); parser.add_argument('output',type=Path)
    args=parser.parse_args()
    graph=json.loads(args.graph_document.read_text())['graph']
    points=[p for e in graph['edges'] for p in e.get('points',[])]
    bounds=[min(p[0] for p in points)-.001,min(p[1] for p in points)-.001,
            max(p[0] for p in points)+.001,max(p[1] for p in points)+.001]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    identity=sha(args.hgt)
    doc=read_hgt(args.hgt,bounds,source='Mapzen Terrain Tiles Skadi N47E009 SHA256:'+identity,vertical_datum=1)
    admin=json.loads(args.administration.read_text(encoding='utf-8'))
    doc['administration']=admin['administration']
    doc['administrative_source']=admin['administrative_source']
    doc['terrain_source']={'url':'https://s3.amazonaws.com/elevation-tiles-prod/skadi/N47/N47E009.hgt.gz',
        'sha256':identity,'bytes':args.hgt.stat().st_size,'vertical_datum_name':'WGS84/EGM96 (application datum ID 1)',
        'attribution':'Mapzen Terrain Tiles. Europe terrain produced using Copernicus data and information funded by the European Union - EU-DEM layers; SRTM/GMTED2010 data courtesy of the U.S. Geological Survey.',
        'provider_attribution':'https://github.com/tilezen/joerd/blob/master/docs/attribution.md',
        'scope':'Provider merged terrain tile; individual pixel source/survey accuracy not independently established. Sample spacing is not ground accuracy.'}
    doc['name']='Liechtenstein real terrain and original OSM administration'
    args.output.write_text(json.dumps(doc,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({'columns':len(doc['dem']['heights'][0]),'rows':len(doc['dem']['heights']),
                      'crop':bounds,'administrative_components':len(doc['administration']),'document_sha256':sha(args.output)}))


if __name__=='__main__': main()
