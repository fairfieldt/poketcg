"""One opaque skin pigment for solid meshes and painted facial backgrounds.

Face textures retain their original eye whites, ink, glasses and mouth pixels.
Only the background pigment changes. Non-Color input makes Blender's Fast64
texture graph use the same palette values as solid materials and native RGBA16.
"""
from collections import Counter
import hashlib
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'port/frontends/web/tools'))
from png import read_png,write_png


def rgba16_pigment(rgb):
    """Nearest RGB5 value expanded exactly as the RDP expands a texture."""
    levels=[(v<<3)|(v>>2) for v in range(32)]
    return tuple(min(levels,key=lambda value:abs(value-round(c*255))) for c in rgb)


def image_rows(path):
    width,height,rows,palette=read_png(path)
    if palette:rows=[[tuple(palette[p])+(255,) for p in row] for row in rows]
    return width,height,[[tuple(p) if len(p)==4 else tuple(p)+(255,) for p in row] for row in rows]


def features_digest(rows,background):
    return hashlib.sha256(b''.join(bytes(p) if p!=background else bytes(4) for row in rows for p in row)).hexdigest()


def prepare_skin(sources,textures,texture_root):
    import bpy
    from fast64.fast64_internal.f3d.f3d_material import update_all_node_values
    materials={slot.material for obj in sources for slot in obj.material_slots if slot.material}
    skins=[m for m in materials if m.get('role')=='skin' or m.name.casefold().startswith('skin')]
    assert len(skins)==1,[(m.name,m.get('role')) for m in materials]
    skin=skins[0]
    pigment=rgba16_pigment(skin.diffuse_color[:3])
    color=tuple(c/255 for c in pigment)
    skin.diffuse_color=(*color,1)
    skin.f3d_mat.prim_color=(*color,1)
    skin.f3d_mat.default_light_color=(*color,1)
    skin.f3d_mat.ambient_light_color=(*(c*.55 for c in color),1)
    skin['skin_rgb8']=pigment
    with bpy.context.temp_override(material=skin):update_all_node_values(skin,bpy.context)
    report=dict(rgb8=list(pigment),solid_material=skin.name,faces=[])
    if 'native_shade_floor' in skin:
        report['shade_floor']=float(skin['native_shade_floor'])
    for mat in sorted(materials,key=lambda m:m.name):
        index=int(mat.get('texture_id',-1))
        if index<0:continue
        old_name=textures[index]
        if mat.get('role')!='face' and 'face' not in old_name and not old_name.startswith('fc_'):continue
        width,height,rows=image_rows(Path(texture_root)/(old_name+'.png'))
        background=Counter(p for row in rows for p in row).most_common(1)[0][0]
        assert background[3]==255 and width==height==32
        original_features=features_digest(rows,background)
        updated=[[(pigment+(255,)) if p==background else p for p in row] for row in rows]
        assert features_digest(updated,pigment+(255,))==original_features
        # Palette-specific names allow distinct character tones to share the
        # same facial style without overwriting each other's texture.
        style=old_name.removeprefix('face_').removesuffix('_face')
        if old_name.startswith('fc_'):
            style=old_name.split('_',2)[2]
        name='fc_'+''.join(f'{c:02x}' for c in pigment)+'_'+style
        assert len(name+'.sprite')<32,name
        path=Path(texture_root)/(name+'.png')
        write_png(path,width,height,[bytes(c for p in row for c in p) for row in updated])
        image=bpy.data.images.load(str(path),check_existing=False)
        image.name=name
        image.colorspace_settings.name='Non-Color'
        image.pack()
        mat.f3d_mat.tex0.tex=image
        mat.diffuse_color=(1,1,1,1)
        mat.f3d_mat.prim_color=(1,1,1,1)
        mat.f3d_mat.default_light_color=(1,1,1,1)
        mat.f3d_mat.ambient_light_color=(.55,.55,.55,1)
        mat['native_lit_texture']=True
        mat['skin_rgb8']=pigment
        with bpy.context.temp_override(material=mat):update_all_node_values(mat,bpy.context)
        assert image.colorspace_settings.name=='Non-Color'
        textures[index]=name
        report['faces'].append(dict(texture=name,background_rgb8=list(pigment),
            feature_sha256=original_features,colorspace='Non-Color',directional_shade=True,
            **({'shade_floor':float(mat['native_shade_floor'])} if 'native_shade_floor' in mat else {})))
    assert report['faces']
    return report
