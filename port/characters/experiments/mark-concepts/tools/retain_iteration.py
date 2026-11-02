#!/usr/bin/env python3
"""Retain one reviewed Mark export and unchanged production supporting actors."""
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
STUDY = Path(__file__).resolve().parents[1]
CONCEPTS = ('a_compact', 'b_portrait', 'c_slender', 'd_angular')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('concept', choices=CONCEPTS)
    parser.add_argument('iteration', type=int, choices=(1, 2, 3))
    parser.add_argument('--mark', type=Path, required=True, help='Export directory containing scene.l64, scene.json and textures/')
    parser.add_argument('--concept-image', type=Path)
    parser.add_argument('--direct-3d', action='store_true', help='Concept authored directly as editable 3D geometry')
    parser.add_argument('--changes', required=True, help='Concrete modeling changes in this round')
    args = parser.parse_args()
    destination = STUDY / 'options' / f'{args.concept}_i{args.iteration}'
    if destination.exists():
        parser.error(f'Retained iteration already exists: {destination}')
    if not args.direct_3d and args.concept_image is None:
        parser.error('Provide --concept-image or --direct-3d')
    for path in (args.mark/'scene.l64', args.mark/'scene.json') + ((args.concept_image,) if args.concept_image else ()):
        if not path.is_file():
            parser.error(f'Missing input: {path}')
    data = (args.mark/'scene.l64').read_bytes()
    if data[:4] != b'L643':
        parser.error('Mark export must be L643')
    generated = ROOT/'port/world3d/generated'
    for slug in ('dr_mason', 'ronald'):
        if not (generated/'maps'/f'actor_{slug}.l64').is_file():
            parser.error(f'Missing production actor: {slug}')
    destination.mkdir(parents=True)
    shutil.copytree(args.mark, destination/'mark')
    hashes = {}
    for slug in ('dr_mason', 'ronald'):
        actor = destination/slug
        (actor/'textures').mkdir(parents=True)
        source = generated/'maps'/f'actor_{slug}.l64'
        shutil.copy2(source, actor/'scene.l64')
        raw = source.read_bytes()
        _, _, texture_count = struct.unpack_from('>4sHH', raw)
        for i in range(texture_count):
            name = raw[8+32*i:8+32*(i+1)].split(b'\0')[0].decode().replace('.sprite', '.png')
            shutil.copy2(generated/'textures'/name, actor/'textures'/name)
        hashes[slug] = hashlib.sha256(raw).hexdigest()
    if args.concept_image:
        shutil.copy2(args.concept_image, destination/('concept'+args.concept_image.suffix))
    record = dict(id=destination.name, label=destination.name, concept=args.concept,
                  iteration=args.iteration, concept_method='direct 3D authoring; no generated raster art' if args.direct_3d else 'raster concept reference', changes=args.changes, supporting_actor_sha256=hashes,
                  mark_sha256=hashlib.sha256(data).hexdigest(), review_status='capture pending')
    (destination/'option.json').write_text(json.dumps(record, indent=2)+'\n')
    print(destination)


if __name__ == '__main__':
    main()
