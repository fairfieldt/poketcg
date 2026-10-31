"""Blender + Fast64: build the two concept characters in a copied Lightning room.

Run with Blender -b --python tools/build_characters.py. The original room is
read-only. Local forward is Blender -Y; runtime forward is +Z.
"""
import bpy
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets'
REPO = ROOT.parents[3]
sys.path.insert(0, str(REPO / 'port/frontends/web/tools'))
from png import write_png
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_scene import export_scene

if 'fast64' not in bpy.context.preferences.addons:
    bpy.ops.preferences.addon_enable(module='fast64')
from fast64.fast64_internal.f3d.f3d_material import createF3DMat, update_all_node_values

bpy.ops.wm.open_mainfile(filepath=str(ROOT.parent / 'lightning/assets/lightning_club.blend'))
scene = bpy.context.scene
scene.gameEditorMode = 'Homebrew'
scene.f3d_type = 'F3DEX2/LX2'
manifest = json.loads(scene['lightning_manifest'])
# Remove the two replaced source models and their linked preview instances.
for obj in list(scene.objects):
    if obj.type == 'MESH' and obj.get('model') in (1, 4):
        bpy.data.objects.remove(obj, do_unlink=True)
for name in ('Mark placement', 'Jennifer placement'):
    if name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)

collection = bpy.data.collections.new('Mason and Ronald · export sources')
scene.collection.children.link(collection)
solid_template = createF3DMat(None, preset='Shaded Solid')
texture_template = createF3DMat(None, preset='Shaded Texture')


def material(name, rgb, image=None):
    mat = (texture_template if image else solid_template).copy()
    mat.name = name
    mat.diffuse_color = (*rgb, 1)
    mat.f3d_mat.prim_color = (*rgb, 1)
    mat.f3d_mat.rdp_settings.g_fog = False
    # Match this room's two-sided runtime materials (lapels/vest are thin panels).
    mat.f3d_mat.rdp_settings.g_cull_back = False
    mat.f3d_mat.rdp_settings.g_shade_smooth = False
    mat.f3d_mat.default_light_color = (*rgb, 1)
    mat.f3d_mat.ambient_light_color = (*(c*.55 for c in rgb), 1)
    mat['texture_id'] = -1
    if image:
        mat.f3d_mat.tex0.tex = image
        mat.f3d_mat.tex0.tex_format = 'RGBA16'
        mat.f3d_mat.tex0.S.clamp = True
        mat.f3d_mat.tex0.T.clamp = True
        mat['texture_id'] = len(manifest['textures'])
        manifest['textures'].append(image.name)
    with bpy.context.temp_override(material=mat):
        update_all_node_values(mat, bpy.context)
    return mat


skin = material('Skin · warm ochre', (.89, .67, .39))
white = material('Lab coat · ivory', (.91, .90, .84))
lapel = material('Collars · white', (.99, .98, .92))
hair_white = material('Mason · silver hair', (.77, .77, .73))
hair_high = material('Mason · white crest', (.94, .93, .86))
hair_dark = material('Ronald · charcoal hair', (.075, .06, .055))
black = material('Shirt and shoe soles', (.085, .085, .09))
brown = material('Mason · trousers', (.24, .205, .17))
gold = material('Mason · ochre shirt', (.44, .30, .15))
blue = material('Ronald · cobalt blue', (.075, .20, .62))
silver = material('Buckle and buttons', (.64, .65, .65))
shadow = material('Opaque contact shadow', (.055, .025, .11))


def face_texture(name, mason):
    """Hand-painted 32x32 RGBA16-safe facial atlas, no alpha or large textures."""
    pixels = [[(227, 172, 100, 255) for x in range(32)] for y in range(32)]
    def rect(x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixels[y][x] = (*color, 255)
    ink, lens, pupil = (34, 30, 27), (225, 218, 187), (35, 36, 37)
    # Simple eyebrows, whites and pupils stay readable on a 320x240 display.
    if mason:
        rect(5, 8, 12, 10, (133, 133, 126))
        rect(20, 8, 27, 10, (133, 133, 126))
        rect(1, 14, 5, 16, ink)
        rect(27, 14, 31, 16, ink)
        for left in (4, 19):
            rect(left, 11, left+10, 23, ink)
            rect(left+2, 13, left+8, 21, lens)
            rect(left+4, 14, left+6, 21, pupil)
        rect(14, 14, 19, 16, ink)
    else:
        for left in (5, 20):
            rect(left, 12, left+7, 22, (244, 236, 207))
            rect(left+3, 12, left+6, 22, pupil)
        for x in range(5, 13):
            yy = 8 + (x-5)//3
            rect(x, yy, x+1, yy+2, ink)
        for x in range(19, 27):
            yy = 10 - (x-19)//3
            rect(x, yy, x+1, yy+2, ink)
    rect(14, 27, 19, 28, (88, 60, 39))
    path = OUT / 'textures' / (name+'.png')
    write_png(path, 32, 32, [bytes(c for p in row for c in p) for row in pixels])
    image = bpy.data.images.load(str(path), check_existing=False)
    image.name = name
    image.pack()
    return material(name, (1, 1, 1), image)


mason_face = face_texture('mason_face', True)
ronald_face = face_texture('ronald_face', False)
objects = {1: [], 4: []}


def mesh(name, verts, faces, mat, model, part=0, pivot=(0, 0, 0), uv=None):
    # Geometry below is authored in X/right, Y/forward, Z/up for readability.
    data = bpy.data.meshes.new(name)
    data.from_pydata([(x, -y, z) for x, y, z in verts], [], [tuple(reversed(f)) for f in faces])
    data.materials.append(mat)
    layer = data.uv_layers.new(name='UVMap')
    for poly in data.polygons:
        for j, li in enumerate(poly.loop_indices):
            vi = data.loops[li].vertex_index
            layer.data[li].uv = uv[vi] if uv else ((0, 0), (1, 0), (1, 1), (0, 1))[j % 4]
    obj = bpy.data.objects.new(name, data)
    collection.objects.link(obj)
    obj['export_source'] = True
    obj['model'], obj['part'], obj['pivot'] = model, part, pivot
    obj['character_name'] = 'Ronald' if model == 1 else 'Dr. Mason'
    objects[model].append(obj)
    return obj


def box(name, center, size, mat, model, part=0, pivot=(0, 0, 0), taper=1):
    x, y, z = center
    a, b, c = (s/2 for s in size)
    verts = [(x+sx*a*(taper if sz>0 else 1), y+sy*b*(taper if sz>0 else 1), z+sz*c)
             for sx, sy, sz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
                                (-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    return mesh(name, verts, [(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)],
                mat, model, part, pivot)


def rings(name, profiles, mat, model, segments=8, part=0, pivot=(0,0,0)):
    # Profiles: z, x radius, y radius, x offset, y offset. Chamfered planes.
    verts = [(cx+rx*math.sin((i+.5)*2*math.pi/segments),
              cy+ry*math.cos((i+.5)*2*math.pi/segments), z)
             for z, rx, ry, cx, cy in profiles for i in range(segments)]
    faces = [tuple(range(segments))]
    for j in range(len(profiles)-1):
        for i in range(segments):
            a, b = j*segments+i, j*segments+(i+1)%segments
            faces.append((a+segments,b+segments,b,a))
    faces.append(tuple(reversed(range((len(profiles)-1)*segments,len(verts)))))
    return mesh(name, verts, faces, mat, model, part, pivot)


def wedge(name, base, tip, mat, model):
    return mesh(name, base+[tip], [(0,3,2,1),(0,1,4),(1,2,4),(2,3,4),(3,0,4)], mat, model)


def head(model, mason):
    prefix = 'Mason' if mason else 'Ronald'
    z = 1.78
    rings(prefix+' faceted head', [(z-.30,.21,.20,0,0),(z-.22,.34,.27,0,0),
          (z+.10,.39,.29,0,0),(z+.30,.31,.26,0,-.02),(z+.34,.17,.16,0,-.04)], skin, model)
    # Six quads follow the head's front planes; UVs form one seamless face.
    xs = [-.31,-.16,.16,.31]
    ys = [.198,.269,.269,.198]
    verts = [(x,y+.004,h) for h in (z-.22,z+.11) for x,y in zip(xs,ys)]
    uv = [((x+.31)/.62, v) for v in (0,1) for x in xs]
    mesh(prefix+' painted face', verts, [(4,5,1,0),(5,6,2,1),(6,7,3,2)],
         mason_face if mason else ronald_face, model, uv=uv)
    # Small nose is silhouette geometry; the eyes and glasses are painted.
    wedge(prefix+' nose', [(-.04,.275,z-.10),(.04,.275,z-.10),(.035,.275,z+.015),(-.035,.275,z+.015)],
          (0,.35,z-.08), skin, model)
    for side in (-1,1):
        rings(prefix+' ear', [(z-.10,.055,.065,side*.355,0),(z+.055,.07,.07,side*.355,0)],
              skin, model, segments=6)
    if mason:
        # Silver side/back hair with the broad swept white crest from the concept.
        rings('Mason hair cap',[(z+.08,.39,.305,0,-.04),(z+.29,.35,.29,0,-.055),
              (z+.38,.22,.19,0,-.045),(z+.40,.06,.07,0,-.05)],hair_white,model)
        wedge('Mason swept crest', [(-.29,.12,z+.27),(.21,.19,z+.27),(.24,-.12,z+.34),(-.23,-.15,z+.34)],
              (-.08,.025,z+.45),hair_high,model)
        for s in (-1,1):
            wedge('Mason temple',[(s*.30,.16,z-.05),(s*.39,.10,z-.05),
                  (s*.39,.09,z+.23),(s*.30,.17,z+.23)],(s*.37,-.12,z+.08),hair_white,model)
    else:
        rings('Ronald hair cap',[(z+.09,.39,.32,0,-.03),(z+.29,.37,.30,0,-.04),
              (z+.38,.25,.22,0,-.03)],hair_dark,model)
        # Large asymmetric polygon spikes; no smooth hair and no strand geometry.
        spikes=[(-.32,-.03, -.57,.00, .26),(-.26,-.17,-.50,-.24,.45),
                (-.08,-.19,-.24,-.30,.61),(.12,-.16,.15,-.28,.66),
                (.29,-.07,.48,-.12,.48),(.32,.11,.59,.16,.30),
                (.12,.14,.31,.38,.46),(-.15,.13,-.31,.34,.47)]
        for i,(x,y,tx,ty,tz) in enumerate(spikes):
            wedge('Ronald spike %02d'%i,[(x-.13,y-.10,z+.24),(x+.13,y-.10,z+.24),
                  (x+.13,y+.10,z+.24),(x-.13,y+.10,z+.24)],(tx,ty,z+tz),hair_dark,model)
        for i,(x,tx,tz) in enumerate([(-.23,-.33,.00),(-.04,-.16,.04),(.18,.08,.08)]):
            wedge('Ronald fringe %d'%i,[(x-.11,.19,z+.27),(x+.13,.19,z+.27),
                  (x+.10,.32,z+.24),(x-.10,.32,z+.24)],(tx,.318,z+tz),hair_dark,model)


def character(model, mason):
    prefix = 'Mason' if mason else 'Ronald'
    cloth, trousers = (white,brown) if mason else (black,blue)
    # Five rigid animation groups match the libdragon renderer's joint IDs.
    rings(prefix+' torso',[(.72,.31,.21,0,0),(1.20,.34,.215,0,0),(1.43,.29,.19,0,0)],cloth,model)
    box(prefix+' neck',(0,0,1.47),(.22,.22,.18),skin,model)
    if mason:
        rings('Mason coat skirt',[(.53,.39,.24,0,-.025),(.83,.35,.23,0,-.025)],white,model)
        mesh('Mason shirt opening',[(-.16,.20,1.42),(.16,.20,1.42),(0,.235,1.02)],[(0,1,2)],gold,model)
        for s in (-1,1):
            mesh('Mason lapel',[(s*.12,.214,1.45),(s*.27,.218,1.33),
                 (s*.13,.242,1.18),(0,.245,1.07)],[(0,1,2,3)],lapel,model)
        for z in (.67,.84,1.01):
            box('Mason coat button',(0,.222,z),(.035,.018,.035),silver,model)
        box('Mason coat pocket',(.20,.228,.91),(.15,.015,.025),hair_white,model)
        box('Mason rear coat seam',(0,-.247,.69),(.014,.012,.24),hair_white,model)
    else:
        # Open sleeveless blue vest over a black shirt; trousers share its blue.
        for s in (-1,1):
            mesh('Ronald vest front',[(s*.11,.219,.79),(s*.31,.205,.79),
                 (s*.29,.19,1.42),(s*.14,.199,1.42)],[(0,1,2,3)],blue,model)
            mesh('Ronald vest side',[(s*.31,.205,.79),(s*.31,-.21,.79),
                 (s*.29,-.19,1.42),(s*.29,.19,1.42)],[(0,1,2,3)],blue,model)
            mesh('Ronald white collar',[(s*.12,.205,1.47),(s*.28,.207,1.42),
                 (s*.20,.245,1.27),(s*.09,.247,1.37)],[(0,1,2,3)],lapel,model)
        box('Ronald vest back',(0,-.218,1.11),(.60,.02,.62),blue,model)
        box('Ronald belt',(0,.01,.76),(.62,.42,.10),black,model)
        box('Ronald buckle',(0,.231,.76),(.13,.025,.09),silver,model)
        box('Ronald vest badge',(-.225,.222,1.25),(.07,.018,.045),lapel,model)
    for s in (-1,1):
        part = 1 if s<0 else 2
        pivot = (s*.17,0,.72)
        rings(prefix+' trouser leg',[(.14,.145,.17,s*.18,.005),(.43,.12,.15,s*.17,0),
              (.73,.14,.17,s*.17,0)],trousers,model,part=part,pivot=pivot,segments=6)
        box(prefix+' shoe sole',(s*.18,.055,.055),(.31,.43,.10),black,model,part,pivot)
        box(prefix+' shoe upper',(s*.18,.065,.15),(.29,.40,.12),black if mason else lapel,
            model,part,pivot,taper=.8)
        if not mason:
            box('Ronald blue shoe toe',(s*.18,.254,.12),(.25,.018,.075),blue,model,part,pivot)
        part=3 if s<0 else 4
        pivot=(s*.40,0,1.36)
        rings(prefix+' sleeve',[(.94 if mason else 1.09,.12,.145,s*.47,0),
              (1.34,.13,.17,s*.40,0)],cloth,model,part=part,pivot=pivot,segments=6)
        if not mason:
            rings('Ronald forearm',[(.82,.08,.10,s*.50,.015),(1.11,.095,.12,s*.47,0)],
                  skin,model,part=part,pivot=pivot,segments=6)
        rings(prefix+' hand',[(.72,.075,.08,s*.51,.02),(.78,.12,.11,s*.51,.02),
              (.94 if mason else .86,.10,.10,s*.49,.01)],skin,model,part=part,pivot=pivot,segments=6)
    head(model,mason)
    mesh(prefix+' contact shadow',[(.47*math.cos(i*math.pi/4),.32*math.sin(i*math.pi/4),.018)
          for i in range(8)],[tuple(range(8))],shadow,model)


character(1, False)
character(4, True)
manifest['actors'][0]['name']='Ronald'
manifest['actors'][4]['name']='Dr. Mason'
manifest['actors'][4]['heading']=0
manifest['models']={'1':'Ronald','2':'Nicholas / Brandon','3':'Isaac','4':'Dr. Mason'}
manifest['character_pipeline']={'blender':bpy.app.version_string,'fast64':'2.6.0',
    'fast64_commit':'566d57b2ec38a968a04dab4bd7ea32ac4e91d55d',
    'reference':'concept.png','rig':'Rigid parts: body 0, legs 1/2, arms 3/4',
    'texture_format':'RGBA16','face_texture_size':[32,32]}
manifest['character_triangles']={str(model):sum(sum(len(p.vertices)-2 for p in o.data.polygons)
    for o in objs) for model,objs in objects.items()}
scene['lightning_manifest']=json.dumps(manifest)

# Place the two replacements using shared mesh datablocks, matching original rig.
for actor in (manifest['actors'][0],manifest['actors'][4]):
    parent=bpy.data.objects.new(actor['name']+' placement',None)
    scene.collection.objects.link(parent)
    parent.location=(actor['x'],-actor['z'],0)
    parent.rotation_euler.z=math.radians(actor['heading'])
    for source in objects[actor['model']]:
        obj=source.copy()
        obj['export_source']=False
        scene.collection.objects.link(obj)
        obj.parent=parent
        obj.hide_render=False
        obj.hide_set(False)
for objs in objects.values():
    for obj in objs:
        obj.hide_render=True
        obj.hide_set(True)

export_scene()
scene.render.filepath=str(OUT/'blender-preview.png')
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x=960
scene.render.resolution_y=720
scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'lightning_characters.blend'))
bpy.ops.render.render(write_still=True)
print('CHARACTERS',json.dumps(manifest['character_triangles']))
