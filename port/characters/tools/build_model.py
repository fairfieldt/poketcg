"""Build exactly one distinct character, export it, then render its review image.

blender -b --python-exit-code 1 --python tools/build_model.py -- SLUG
Shared mesh datablocks are linked from assets/parts.blend. Color variants and
poses reuse those exact meshes and never create another authored model.
"""
import bpy
import hashlib
import json
import math
import shutil
import sys
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(REPO/'port/tools'))
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
sys.path.insert(0,str(REPO/'port/frontends/n64/lightning-characters/tools'))
from designs import DESIGNS,BODIES
from parts import box,rings,spike
from png import write_png
import export_scene
import render_maps
from skin import prepare_skin
from exterior import exterior_sources
from lead_faces import paint as paint_lead_face

slug=sys.argv[sys.argv.index('--')+1]
catalog=json.loads((ROOT/'catalog.json').read_text())
family=next(f for f in catalog['families'] if f['slug']==slug)
appearances=[a for a in catalog['appearances'] if a['model']==slug]
out=ROOT/'assets/models'/slug
out.mkdir(parents=True,exist_ok=True)
(ROOT/'assets/textures').mkdir(exist_ok=True)
if 'fast64' not in bpy.context.preferences.addons:
    bpy.ops.preferences.addon_enable(module='fast64')
from fast64.fast64_internal.f3d.f3d_material import update_all_node_values
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.gameEditorMode='Homebrew';scene.f3d_type='F3DEX2/LX2'
scene.render.engine='BLENDER_EEVEE'
scene.view_settings.view_transform='Standard'
scene.world.color=(.12,.14,.18)
sources=[]
textures=[]
model=family['runtime_model']
spec=DESIGNS.get(slug,{})
scale=BODIES.get(spec.get('body'),(1,1,1))
sx,sy,sz=scale
game=render_maps.Game(str(REPO))
palettes=game.obj_palettes(game.consts['palette_constants.asm']['PALETTE_OVERWORLD_OAM'])


def accent_for(a):
    frame=game.anim_first_frame(a['anim'])
    return tuple(c/255 for c in palettes[frame[0][3]&7][2])


accent_rgb=accent_for(appearances[0])
with bpy.data.libraries.load(str(ROOT/'assets/parts.blend'),link=True) as (src,dst):
    dst.meshes=[n for n in src.meshes if n.startswith('part_')]
with bpy.data.libraries.load(str(ROOT/'assets/parts.blend'),link=False) as (src,dst):
    dst.materials=['Character Solid Template','Character Texture Template']
    # Fast64 switches between these graphs by name while updating materials.
    dst.node_groups=src.node_groups
templates=dst.materials
linked={m.name:m for m in bpy.data.meshes if m.library}


def material(name,rgb,image=None):
    mat=templates[1 if image else 0].copy();mat.name=slug+' · '+name
    mat['role']=name;mat['texture_id']=-1
    mat.diffuse_color=(*rgb,1)
    f=mat.f3d_mat
    f.prim_color=(*rgb,1);f.rdp_settings.g_fog=False
    f.rdp_settings.g_cull_back=False;f.rdp_settings.g_shade_smooth=False
    f.default_light_color=(*rgb,1);f.ambient_light_color=(*(c*.55 for c in rgb),1)
    if image:
        f.tex0.tex=image;f.tex0.tex_format='RGBA16';f.tex0.S.clamp=True;f.tex0.T.clamp=True
        mat['texture_id']=len(textures);textures.append(image.name)
    with bpy.context.temp_override(material=mat):update_all_node_values(mat,bpy.context)
    return mat


skin=material('skin',(.89,.67,.39))
ink=material('ink',(.065,.06,.065))
white=material('white',(.92,.91,.86))
silver=material('silver',(.63,.64,.65))
accent=material('accent',accent_rgb)
trousers=material('trousers',(.16,.18,.22))
hair_mat=material('hair',(.065,.045,.035))
shadow=material('shadow',(.055,.025,.11))


def add(data,name,mat,loc=(0,0,0),part=0,pivot=(0,0,0),s=(1,1,1),component=None):
    obj=bpy.data.objects.new(slug+' · '+name,data)
    scene.collection.objects.link(obj)
    obj.location=(loc[0]*sx,-loc[1]*sy,loc[2]*sz)
    obj.scale=tuple(a*b for a,b in zip(s,scale))
    obj.material_slots[0].link='OBJECT';obj.material_slots[0].material=mat
    obj['export_source']=True;obj['model']=model;obj['part']=part
    obj['pivot']=(pivot[0]*sx,pivot[1]*sy,pivot[2]*sz)
    obj['character_name']=slug
    if component:obj['component']=component
    sources.append(obj)
    return obj


def part(key,mat,loc=(0,0,0),joint=0,pivot=(0,0,0),s=(1,1,1),name=None):
    return add(linked['part_'+key],name or key,mat,loc,joint,pivot,s)


def mesh(name,geometry,mat,component=None):
    verts,faces=geometry
    data=bpy.data.meshes.new(slug+'_'+name)
    data.from_pydata([(x,-y,z) for x,y,z in verts],[],[tuple(reversed(f)) for f in faces])
    data.materials.append(mat)
    uv=data.uv_layers.new()
    for poly in data.polygons:
        for j,li in enumerate(poly.loop_indices):uv.data[li].uv=((0,0),(1,0),(1,1),(0,1))[j%4]
    return add(data,name,mat,component=component)


def block(name,loc,size,mat,joint=0,pivot=(0,0,0)):
    data=bpy.data.meshes.new(slug+'_'+name)
    verts,faces=box(size)
    data.from_pydata([(x,-y,z) for x,y,z in verts],[],[tuple(reversed(f)) for f in faces])
    data.materials.append(mat);data.uv_layers.new()
    return add(data,name,mat,loc,joint,pivot)


def plane(name,verts,mat):return mesh(name,(verts,[tuple(range(len(verts)))]),mat)


def face(style):
    lead_pixels=paint_lead_face(slug)
    if lead_pixels is not None:
        texture_name='face_'+style
        path=ROOT/'assets/textures'/(texture_name+'.png')
        write_png(path,32,32,[bytes(c for p in row for c in p) for row in lead_pixels])
        image=bpy.data.images.load(str(path),check_existing=False);image.name=texture_name;image.pack()
        return material('face',(1,1,1),image)
    pixels=[[(227,172,100,255) for x in range(32)] for y in range(32)]
    def rect(x0,y0,x1,y1,c):
        for y in range(max(0,y0),min(32,y1)):
            for x in range(max(0,x0),min(32,x1)):pixels[y][x]=(*c,255)
    dark=(32,29,28);cream=(240,229,199)
    for left in (5,20):
        rect(left,12,left+7,22,cream)
        rect(left+3,13,left+5,22,dark)
    if style in ('stern','mature'):
        for x in range(5,13):rect(x,8+(x-5)//3,x+1,10+(x-5)//3,dark)
        for x in range(19,27):rect(x,10-(x-19)//3,x+1,12-(x-19)//3,dark)
    else:
        rect(5,9,12,10,dark);rect(20,9,27,10,dark)
    if 'glasses' in style:
        for l in (3,18):
            rect(l,11,l+11,12,dark);rect(l,23,l+11,24,dark)
            rect(l,11,l+1,24,dark);rect(l+10,11,l+11,24,dark)
        rect(14,15,18,17,dark)
    if style.startswith('old'):
        rect(4,7,12,9,(129,124,117));rect(20,7,28,9,(129,124,117))
        rect(2,25,5,26,(155,108,69));rect(27,25,30,26,(155,108,69))
    if style=='gentle':
        rect(5,11,12,12,dark);rect(20,11,27,12,dark)
    rect(13,27,19,28,dark)
    if style=='smile':rect(14,28,18,29,dark)
    if style=='imakuni':
        rect(0,0,32,32,(227,172,100))
        rect(6,13,10,19,dark);rect(22,13,26,19,dark)
        rect(11,25,22,28,dark);rect(13,25,20,26,cream)
    texture_name='face_'+style
    path=ROOT/'assets/textures'/(texture_name+'.png')
    write_png(path,32,32,[bytes(c for p in row for c in p) for row in pixels])
    image=bpy.data.images.load(str(path),check_existing=False);image.name=texture_name;image.pack()
    return material('face',(1,1,1),image)


def hair(kind,mat):
    wearing_cap=kind=='cap' or spec.get('cap',False)
    if kind=='hood':
        part('hood',ink)
        for s in (-1,1):part('hood_ear',ink,(s*.37,-.02,2.17))
        return
    if kind in ('bald','receding'):
        for s in (-1,1):part('hair_side',white if kind=='bald' else mat,(s*.345,-.05,1.79),s=(.7,.8,.65))
        block('back hair',(0,-.25,1.85),(.60,.11,.23),white if kind=='bald' else mat)
        if kind=='bald':return
    if kind=='flat':part('flat_hair',mat)
    elif kind not in ('bald','receding'):part('hair_cap',mat)
    if kind in ('bob','long','twintails','bun'):
        part('hair_back' if kind=='long' else 'bob_back',mat,s=(.95 if wearing_cap else 1,1,1))
        for s in (-1,1):
            # A capped long-haired character keeps the long side locks below
            # the hat; the old upper corners pierced its sloping crown.
            side_height=1.65 if wearing_cap else 1.73
            side_length=1 if wearing_cap or kind!='long' else 1.35
            part('hair_side',mat,(s*.34,-.01,side_height),s=(.9,1,side_length))
    if kind=='twintails':
        for s in (-1,1):
            part('bun',mat,(s*.40,-.07,2.08),s=(1.05,.9,1.2))
            block('hair tie',(s*.38,.07,2.05),(.12,.065,.06),accent)
    if kind=='bun':part('bun',mat,(0,-.19,2.22))
    if kind in ('sidepart','swept','quiff','short','receding','bob','long') and not wearing_cap:
        height=.20 if kind=='quiff' else .10
        mesh('swept fringe',spike([(-.31,.14,2.02),(.27,.14,2.02),(.24,-.05,2.12+height),(-.20,-.09,2.12)],
                                 (-.28,.30,1.88)),mat)
    if kind in ('spikes','points'):
        n=7 if kind=='spikes' else 2
        for i in range(n):
            x=-.33+i*.66/max(1,n-1)
            height=.18+(.14 if i%2 else 0)
            mesh('hair spike '+str(i),spike([(x-.10,-.17,2.01),(x+.10,-.17,2.01),
                (x+.11,.10,2.01),(x-.11,.10,2.01)],(x*1.3,.01,2.18+height)),mat)
    if kind=='curtains':
        for s in (-1,1):
            mesh('curtain fringe',spike([(s*.02,.13,2.1),(s*.32,.12,2.07),
                 (s*.30,.30,2.02),(s*.05,.31,2.03)],(s*.20,.30,1.74)),mat)
    if wearing_cap:
        part('cap',white if spec.get('hair_color')=='white' else accent)
        part('brim',white if spec.get('hair_color')=='white' else accent,(0,.34,1.98))
        block('cap badge',(0,.305,2.075),(.10,.08,.08),white)
    if spec.get('headband'):
        block('headband',(0,.25,2.0),(.61,.07,.065),accent)
    if spec.get('curls'):
        for s in (-1,1):part('bun',mat,(s*.40,-.02,1.88),s=(.65,.7,1.2))


def outfit(costume):
    female=spec['body'] in ('girl','female','elder_female')
    top=accent
    if costume in ('labcoat','suit','sailor'):top=white
    elif costume in ('bodysuit','vest','jacket','trainer'):top=ink if costume!='vest' else white
    part('torso_female' if female else 'torso',top)
    part('neck',skin,(0,0,1.47))
    if costume in ('skirt','uniform','dress','cardigan','sleeveless'):
        long=costume in ('dress','cardigan','sleeveless')
        part('long_skirt' if long else 'skirt',ink if costume in ('skirt','uniform') else accent)
        part('belt',ink if costume in ('dress','sleeveless') else accent,(0,0,.79),s=(.95,.95,.7))
    if costume in ('labcoat','suit'):
        part('coat_skirt',white)
        plane('shirt opening',[(-.15,.22,1.43),(.15,.22,1.43),(0,.24,1.0)],accent if costume=='labcoat' else ink)
        for s in (-1,1):
            plane('coat lapel',[(s*.11,.221,1.45),(s*.27,.223,1.33),(s*.13,.25,1.18),(0,.25,1.08)],white)
        for z in (.69,.86,1.03):block('coat button',(0,.224,z),(.035,.018,.035),silver)
        block('coat pocket',(.2,.237,.92),(.15,.015,.022),silver)
    if costume in ('vest','jacket','trainer','cardigan'):
        outer=accent
        for s in (-1,1):
            plane('open front panel',[(s*.10,.223,.79),(s*.31,.212,.79),(s*.29,.205,1.42),(s*.13,.211,1.42)],outer)
            plane('outer side',[(s*.31,.212,.79),(s*.31,-.22,.79),(s*.29,-.20,1.42),(s*.29,.205,1.42)],outer)
        block('outer back',(0,-.223,1.10),(.59,.025,.62),outer)
        part('belt',ink,(0,0,.76));block('buckle',(0,.24,.76),(.12,.02,.09),silver)
    if costume in ('uniform','shirt','vest','jacket','sailor','trainer'):
        for s in (-1,1):
            plane('collar',[(s*.1,.21,1.47),(s*.26,.215,1.4),(s*.18,.247,1.27),(s*.07,.249,1.37)],white)
    if costume=='sailor':
        plane('sailor neck',[(-.18,.23,1.42),(0,.25,1.12),(.18,.23,1.42)],accent)
    if costume=='gi':
        plane('open gi neck',[(-.18,.23,1.44),(.18,.23,1.44),(0,.25,1.10)],skin)
        part('belt',ink,(0,0,.83));block('belt knot',(0,.26,.83),(.13,.09,.11),ink)
    if costume=='trainer':
        part('backpack',material('backpack',(.47,.30,.12)),(0,-.34,1.04))
        for s in (-1,1):block('pack strap',(s*.21,.231,1.15),(.05,.023,.44),ink)
    shorts=spec.get('shorts') or costume in ('swimsuit','sailor')
    bare=female or shorts
    for side in (-1,1):
        joint=1 if side<0 else 2; pivot=(side*.17,0,.72)
        leg_mat=skin if bare else ink if costume=='bodysuit' else trousers
        if costume=='trainer':
            # Mark's blue trousers are independent of his red cap/vest palette.
            if 'trainer_blue' not in globals():globals()['trainer_blue']=material('trainer blue',(.08,.22,.62))
            leg_mat=globals()['trainer_blue']
        part('bare_leg' if bare else 'trousers',leg_mat,
             (side*.17,0,.14),joint,pivot)
        if shorts:part('shorts',accent,(side*.17,0,.46),joint,pivot)
        part('sole',ink,(side*.18,.055,.055),joint,pivot)
        part('shoe',white if costume in ('trainer','swimsuit','sailor') else ink,(side*.18,.065,.15),joint,pivot)
        joint=3 if side<0 else 4;pivot=(side*.40,0,1.36)
        bare_arm=costume in ('swimsuit','sleeveless','dress','sailor')
        long_arm=costume in ('labcoat','suit','bodysuit','jacket','cardigan')
        if bare_arm:
            part('bare_arm',skin,(side*.40,0,.82),joint,pivot,s=(side,1,1))
        elif long_arm:
            part('sleeve_long',top if costume not in ('jacket','cardigan') else accent,
                 (side*.40,0,.94),joint,pivot,s=(side,1,1))
        else:
            part('sleeve_short',top,(side*.40,0,1.11),joint,pivot,s=(side,1,1))
            part('forearm',skin,(side*.47,0,.82),joint,pivot,s=(side,1,1))
        part('hand',ink if costume=='bodysuit' else skin,(side*.51,.02,.80),joint,pivot,s=(side,1,1))


def humanoid():
    outfit(spec['costume'])
    part('head',skin)
    part('face',face(spec['face']))
    part('nose',skin)
    for side in (-1,1):part('ear',skin,(side*.355,0,1.755))
    hairmat=accent if spec.get('hair_color')=='accent' else white if spec.get('hair_color')=='white' or spec['hair']=='bun' else hair_mat
    hair(spec['hair'],hairmat)
    part('shadow',shadow)


def props():
    if slug=='torch':
        mesh('brazier',rings([(.03,.22,.16,0,0),(.16,.30,.21,0,0),(.20,.28,.2,0,0)]),ink)
        for i,(x,z) in enumerate([(-.19,.86),(0,1.15),(.19,.94)]):
            mesh('blue flame '+str(i),spike([(x-.13,-.16,.14),(x+.13,-.16,.14),(x+.13,.16,.14),(x-.13,.16,.14)],(x+.04,0,z)),accent)
        mesh('inner flame',spike([(-.16,-.17,.15),(.16,-.17,.15),(.16,.17,.15),(-.16,.17,.15)],(0,.04,.58)),skin)
    else:
        pink=material('card pink',(.97,.42,.58));purple=material('card violet',(.48,.26,.84))
        for q,component in enumerate(('top_right','top_left','bottom_left','bottom_right')):
            verts=[]
            for y in (.06,-.06):
                verts.extend([(0,y,1.0)]+[(.65*math.cos((q+i/4)*math.pi/2),y,1+.65*math.sin((q+i/4)*math.pi/2)) for i in range(5)])
            faces=[tuple(range(6)),tuple(reversed(range(6,12)))]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
            mesh(component+' frame',(verts,faces),pink,component)
            vv=[(0,.068,1.0)]+[(.49*math.cos((q+i/4)*math.pi/2),.068,1+.49*math.sin((q+i/4)*math.pi/2)) for i in range(5)]
            mesh(component+' face',(vv,[tuple(range(6))]),purple,component)
        for side,label in ((-1,'left_spark'),(1,'right_spark')):
            vv=[(side*.98,.04,1), (side*.98,-.04,1)]
            vv.extend([(side*.98+(.22 if i%2==0 else .075)*math.cos(i*math.pi/4),0,1+(.22 if i%2==0 else .075)*math.sin(i*math.pi/4)) for i in range(8)])
            ff=[(0,2+i,2+(i+1)%8) for i in range(8)]+[(1,2+(i+1)%8,2+i) for i in range(8)]
            mesh(label,(vv,ff),pink,label)


if slug in ('dr_mason','ronald'):
    original=REPO/'port/frontends/n64/lightning-characters/assets/models'/slug
    source_file=original/(slug+'.blend')
    with bpy.data.libraries.load(str(source_file),link=False) as (src,dst):dst.objects=src.objects
    for obj in dst.objects:
        if obj.type=='MESH' and obj.get('export_source'):
            scene.collection.objects.link(obj);obj.hide_render=False;obj.hide_set(False)
            obj['model']=model;sources.append(obj)
    source_manifest=json.loads((original/'scene.json').read_text())
    textures=source_manifest['textures']
    for t in textures:
        target=ROOT/'assets/textures'/(t+'.png')
        shutil.copy2(original/'textures'/(t+'.png'),target)
        if 'face' in t:
            pixels=paint_lead_face(slug)
            write_png(target,32,32,[bytes(c for p in row for c in p) for row in pixels])
            image=bpy.data.images.load(str(target),check_existing=False);image.name=t;image.pack()
            # The imported meshes retain their exact UVs and exterior recipe.
            # Only the canonical character's copied facial image is replaced.
            for obj in sources:
                for slot in obj.material_slots:
                    mat=slot.material
                    if mat and int(mat.get('texture_id',-1))==textures.index(t):
                        mat.f3d_mat.tex0.tex=image
                        mat['role']='face'
                        with bpy.context.temp_override(material=mat):update_all_node_values(mat,bpy.context)
else:
    if spec['body']=='prop':props()
    else:humanoid()

# Keep the editable linked pieces, but export their actual exterior surface.
authored_sources=list(sources)
for source in sources:source['character_name']=slug
if slug not in ('torch','legendary_card'):
    sources=exterior_sources(sources,wearing_cap=spec.get('hair')=='cap' or spec.get('cap',False))

# The facial paint and solid head/ears/limbs share the same character pigment.
# Opaque feature pixels remain unchanged; no alpha-test support is required.
skin_palette=None
if slug not in ('torch','legendary_card'):
    skin_palette=prepare_skin(sources,textures,ROOT/'assets/textures')

scene['character_slug']=slug
poses={'standing':dict(rotation=[0,0,0],translation=[0,0,0])}
if slug=='amy':poses['lying']=dict(rotation=[-90,0,0],translation=[0,-.9,.35])
if slug=='murray':poses['west_fixed']=dict(rotation=[0,0,-90],translation=[0,0,0])
scene['character_poses']=json.dumps(poses)
base_manifest=dict(format='L643',textures=textures,actors=[dict(name=slug,model=model,x=0,z=0,heading=0)],colliders=[],
    model=slug,aliases=family['aliases'],runtime_model=model,poses=poses,
    appearances={a['slug']:dict(accent=accent_for(a)) for a in appearances},
    design=spec.get('detail','Approved Lightning concept with owned face and clothing surfaces'),
    surface_ownership=dict(face_and_clothing=True,headwear=bool(spec.get('hair')=='cap' or spec.get('cap',False))),
    sharing=dict(library='../../parts.blend',linked_parts=sum(bool(o.data.library) for o in authored_sources)),
    provenance=dict(sprite=family['sprite'],gfx_sha256=family['gfx_sha256'],fast64='2.6.0'))
if skin_palette:base_manifest['skin_palette']=skin_palette


def export(folder,appearance,pose='standing',component=None):
    folder.mkdir(parents=True,exist_ok=True)
    scene['lightning_manifest']=json.dumps(dict(base_manifest,appearance=appearance,pose=pose,component=component))
    bpy.context.view_layer.update()
    export_scene.OUT=folder
    result=export_scene.export_scene()
    (folder/'world_data.h').unlink()
    return result


stats=export(out,slug)
# A color variant is a material override on this model, never a second .blend.
for a in appearances[1:]:
    accent.diffuse_color=(*accent_for(a),1);accent.update_tag()
    export(out/'variants'/a['slug'],a['slug'])
accent.diffuse_color=(*accent_rgb,1);accent.update_tag()
if slug=='amy':
    root=Matrix.Translation((0,-.9,.35))@Matrix.Rotation(math.radians(-90),4,'X')
    transforms={o:o.matrix_world.copy() for o in sources};pivots={o:tuple(o['pivot']) for o in sources}
    for o in sources:
        o.matrix_world=root@o.matrix_world
        p=root@Vector((o['pivot'][0],-o['pivot'][1],o['pivot'][2]));o['pivot']=(p.x,-p.y,p.z)
    export(out/'poses/lying',slug,'lying')
    for o in sources:o.matrix_world=transforms[o];o['pivot']=pivots[o]
if slug=='legendary_card':
    for component in ('top_left','top_right','bottom_left','bottom_right','left_spark','right_spark'):
        for o in sources:o['export_source']=o.get('component')==component
        export(out/'components'/component,slug,component=component)
    for o in sources:o['export_source']=True
scene['lightning_manifest']=json.dumps(dict(base_manifest,appearance=slug,**{k:stats[k] for k in ('vertices','triangles','batches')}))

# Canonical editable file contains exactly one character at local origin.
bpy.ops.object.camera_add(location=(2.8,-5,2.8))
camera=bpy.context.object;camera.name='Character camera'
camera.rotation_euler=(Vector((0,0,1.15))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=2.95
scene.camera=camera
scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=4
        area.spaces.active.region_3d.view_location=(0,0,1.1)
bpy.ops.wm.save_as_mainfile(filepath=str(out/(slug+'.blend')),relative_remap=True)
scene.render.filepath=str(out/'front.png');bpy.ops.render.render(write_still=True)
camera.location=(-2.8,5,2.8)
camera.rotation_euler=(Vector((0,0,1.15))-camera.location).to_track_quat('-Z','Y').to_euler()
scene.render.filepath=str(out/'back.png');bpy.ops.render.render(write_still=True)
(out/'complete.json').write_text(json.dumps(dict(model=slug,triangles=stats['triangles'],vertices=stats['vertices'],
    batches=stats['batches'],linked_parts=base_manifest['sharing']['linked_parts'],appearances=[a['slug'] for a in appearances]),indent=2)+'\n')
print('MODEL_DONE',slug,stats['triangles'],'triangles',base_manifest['sharing']['linked_parts'],'linked parts',flush=True)
