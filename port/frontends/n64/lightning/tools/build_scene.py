"""Run with Blender --background --python tools/build_scene.py.

Editable Blender source + deterministic big-endian indexed meshes for libdragon.
Blender coordinates: X east, Y north, Z up. Export: X east, Y up, Z south.
Every texture fits in the RDP's 4 KiB TMEM as RGBA16. No Blender add-ons needed.
"""
import bpy
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[3]
sys.path.insert(0, str(REPO / 'port/frontends/web/tools'))
from png import read_png, write_png

OUT = ROOT / 'assets'
TEX = OUT / 'textures'
TEX.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
for material in list(bpy.data.materials):
    bpy.data.materials.remove(material)

_, _, pixels, palette = read_png(REPO / 'port/maps/lightning_club.png')
def px(x, y):
    p = pixels[y][x]
    return tuple(palette[p]) + (255,) if palette else tuple(p)

materials = []
texture_names = []
texture_hashes = {}
def material(name, color=(1, 1, 1, 1), rows=None):
    if rows is not None:
        h = hashlib.sha256(b''.join(bytes(c for p in row for c in p) for row in rows)).hexdigest()
        if h in texture_hashes:
            return texture_hashes[h]
        texture_id = len(texture_names)
        texture_names.append(name)
        path = TEX / (name + '.png')
        write_png(path, len(rows[0]), len(rows), [bytes(c for p in row for c in p) for row in rows])
    else:
        texture_id = -1
    m = bpy.data.materials.new(name)
    m.diffuse_color = color
    m['texture_id'] = texture_id
    m.use_nodes = True
    nodes = m.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    shader = nodes.new('ShaderNodeEmission')
    shader.inputs['Color'].default_value = color
    m.node_tree.links.new(shader.outputs[0], output.inputs[0])
    if rows is not None:
        image = bpy.data.images.load(str(path))
        image.pack()
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = image
        tex.interpolation = 'Linear'
        m.node_tree.links.new(tex.outputs['Color'], shader.inputs['Color'])
        texture_hashes[h] = m
    materials.append(m)
    return m

def crop(name, x, y, w, h, width=32, height=32):
    return material(name, rows=[[px(x + i*w//width, y + j*h//height)
                               for i in range(width)] for j in range(height)])

def mesh(name, verts, faces, mat, model=0, part=0, pivot=(0, 0, 0), uv=None):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.materials.append(mat)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj['export_source'] = True
    obj['model'] = model
    obj['part'] = part
    obj['pivot'] = pivot
    layer = data.uv_layers.new()
    for poly in data.polygons:
        for i, li in enumerate(poly.loop_indices):
            layer.data[li].uv = uv[i] if uv else ((0, 0), (1, 0), (1, 1), (0, 1))[i % 4]
    return obj

def quad(name, verts, mat, uv=None):
    return mesh(name, verts, [(0, 1, 2, 3)], mat, uv=uv)

def box(name, center, size, mat, model=0, part=0, pivot=(0, 0, 0), omit=()):
    x, y, z = center
    a, b, c = (s/2 for s in size)
    verts = [(x+sx*a, y+sy*b, z+sz*c) for sx, sy, sz in
             [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
              (-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    faces=[(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)]
    return mesh(name, verts, [f for i,f in enumerate(faces) if i not in omit], mat, model, part, pivot)

def ball(name, center, scale, mat, model, part=0, rings=4, segments=8):
    # Latitude rings with a single vertex at each pole; no degenerate triangles.
    verts = [(center[0], center[1], center[2]-scale[2])]
    for j in range(1, rings):
        phi = -math.pi/2 + math.pi*j/rings
        for i in range(segments):
            theta = 2*math.pi*i/segments
            verts.append((center[0]+scale[0]*math.cos(phi)*math.cos(theta),
                          center[1]+scale[1]*math.cos(phi)*math.sin(theta),
                          center[2]+scale[2]*math.sin(phi)))
    top = len(verts)
    verts.append((center[0], center[1], center[2]+scale[2]))
    faces = []
    for i in range(segments):
        n = (i+1) % segments
        faces.append((0, 1+n, 1+i))
        for j in range(rings-2):
            a, b = 1+j*segments, 1+(j+1)*segments
            faces.append((a+i, a+n, b+n, b+i))
        a = 1+(rings-2)*segments
        faces.append((a+i, a+n, top))
    return mesh(name, verts, faces, mat, model, part)

# Reconstruct the walkable floor from the source, removing elevations and
# continuing the carpet beneath pillars. Keep the source's cream north apron.
def floor_pixel(x, y):
    tx, ty = x//8, y//8
    if ((8 <= tx < 10 or 17 <= tx < 20) and 14 <= ty < 26):
        return px(16 + x%16, 128 + y%16)
    if ((4 <= tx < 6 or 22 <= tx < 24) and 3 <= ty < 7) or (8 <= tx < 20 and 2 <= ty < 5):
        return (255, 255, 173, 255)
    return px(x, y)

for y in range(2, 30, 4):
    for x in range(2, 26, 4):
        rows = [[floor_pixel(min(207,x*8+i), min(223,y*8+j)) for i in range(32)] for j in range(32)]
        m = material(f'floor_{x:02}_{y:02}', rows=rows)
        # Texture V is flipped at export because PNG/sprite origin is top-left.
        quad(f'Floor {x},{y}', [(x,y,0),(x+4,y,0),(x+4,y+4,0),(x,y+4,0)], m,
             [(0,1),(1,1),(1,0),(0,0)])

brown = material('Wall brown', (0.32,0.094,0.012,1))
gold = material('Gold', (1,0.74,0.13,1))
cream = material('Cream', (1,1,0.68,1))
maroon = material('Maroon', (0.45,0,0.26,1))
orange = material('Orange', (0.90,0.55,0,1))
frieze = crop('hazard', 16, 8, 32, 8, 32, 8)
pillar_mat = crop('pillar', 64, 112, 16, 64, 16, 64)
badge_mats = [crop(f'badges_{i}', 64+i*32,16,32,24,32,32) for i in range(3)]

box('North wall', (14,1,2.25),(28,2,4.5), brown,omit=(3,))
box('West wall', (1,16,2.25),(2,28,4.5), brown,omit=(2,))
box('East wall', (27,16,2.25),(2,28,4.5), brown,omit=(4,))
# The decorated bands replace wall faces instead of overlaying them. The N64's
# compressed Z buffer loses precision for nearly coplanar surfaces at distance.
for low,high in ((0,3.35),(4.24,4.5)):
    quad('North wall face',[(2,2,low),(26,2,low),(26,2,high),(2,2,high)],brown)
    for x in (2,26):
        quad('Side wall face',[(x,2,low),(x,30,low),(x,30,high),(x,2,high)],brown)
# Low near-wall sections keep the entrance and the player readable.
for x in (6,22):
    box('South wall', (x,30,0.6),(12,1,1.2), brown)
    box('South cap', (x,30,1.24),(12.1,1.1,0.12),gold)
for z in (0.22,3.28,4.35):
    for x in (1.99,26.01):
        box('Side gold trim',(x,16,z),(0.035,28,0.09), gold)
    box('North gold trim',(14,2.01,z),(24,0.035,0.09),gold)
box('North skirting',(14,2.025,0.5),(24,0.05,0.65),cream)
for x in (2.025,25.975):
    box('Side skirting',(x,16,0.5),(0.05,28,0.65),cream)
for x in range(2,26,4):
    quad('North hazard',[(x,2.2,3.35),(x+4,2.2,3.35),(x+4,2.2,4.24),(x,2.2,4.24)],frieze)
for y in range(2,30,4):
    for x in (2.2,25.8):
        quad('Side hazard',[(x,y,3.35),(x,y+4,3.35),(x,y+4,4.24),(x,y,4.24)],frieze)

colliders = []
for x,y,h in [(8,19,3.8),(17,19,3.8),(4,3,1.6),(22,3,1.6)]:
    # Tall pillars' 12-tile source silhouettes encode elevation, not 12 tiles
    # of floor footprint. Place their feet at the source silhouette's bottom.
    box('Badge pillar', (x+1,y+1,h/2+0.25),(2,2,h),pillar_mat)
    box('Maroon pillar foot',(x+1,y+1,0.20),(2.08,2.08,0.4),maroon)
    box('Gold pillar toe',(x+1,y+1,0.055),(2.16,2.16,0.11),gold)
    box('Pillar cap',(x+1,y+1,h+0.30),(2.08,2.08,0.14),orange)
    colliders.append([x,y,x+2,y+2])
box('Badge shrine',(14,2.75,0.85),(12,1.5,1.7),orange,omit=(3,))
for i, m in enumerate(badge_mats):
    x = 8+i*4
    quad('Shrine badges',[(x,3.7,0.15),(x+4,3.7,0.15),(x+4,3.7,1.7),(x,3.7,1.7)],m)
box('Shrine gold toe',(14,3.54,0.11),(12,0.08,0.13),gold)
colliders.append([8,2,20,3.5])

skin = material('Skin', (0.94,0.71,0.45,1))
white = material('Shirt white',(0.90,0.88,0.76,1))
black = material('Eyes and shoes',(0.055,0.045,0.045,1))
hair_black = material('Dark hair',(0.085,0.065,0.045,1))
hair_brown = material('Brown hair',(0.32,0.17,0.055,1))
red = material('Red jacket',(0.73,0.035,0.035,1))
blue = material('Blue trousers',(0.025,0.12,0.38,1))
dark = material('Dark trousers',(0.11,0.13,0.16,1))
pink = material('Pink dress',(0.92,0.40,0.61,1))
pack = material('Backpack',(0.46,0.29,0.10,1))
shadow = material('Contact shadow',(0.055,0.025,0.11,1))

def character(model, name, shirt, trousers, hair, girl=False, coat=False):
    start = set(bpy.data.objects)
    # Opaque octagonal contact shadows suit the original hardware; no blending.
    mesh(name+' shadow',[(.43*math.cos(i*math.pi/4),.32*math.sin(i*math.pi/4),.018)
                         for i in range(8)], [tuple(range(8))], shadow, model)
    # Local forward is +Y (south); heading 180 faces north in the renderer.
    box(name+' torso',(0,0,1.02),(0.64,0.38,0.65),shirt,model)
    if coat:
        box(name+' coat',(0,-0.025,0.72),(0.72,0.42,0.50),shirt,model)
    if girl:
        mesh(name+' skirt',[(-.32,-.2,.91),(.32,-.2,.91),(.32,.2,.91),(-.32,.2,.91),
                            (-.45,-.29,.56),(.45,-.29,.56),(.45,.29,.56),(-.45,.29,.56)],
             [(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)],shirt,model)
    box(name+' neck',(0,0,1.42),(.22,.22,.18),skin,model)
    ball(name+' head',(0,0,1.72),(.38,.31,.39),skin,model)
    ball(name+' hair',(0,-.045,1.91),(.41,.32,.29),hair,model)
    if girl:
        box(name+' back hair',(0,-.24,1.66),(.68,.18,.53),hair,model)
    for side in (-1,1):
        box(name+' fringe',(side*.22,.24,1.94),(.23,.11,.20),hair,model)
        box(name+' eye',(side*.135,.289,1.73),(.075,.025,.11),black,model)
        # Pivots are local skeleton joint positions, exported per limb batch.
        part = 1 if side < 0 else 2
        pivot = (side*.18,0,.64)
        box(name+' leg',(side*.18,0,.36),(.25,.28,.56),skin if girl else trousers,model,part,pivot)
        box(name+' shoe',(side*.18,.075,.08),(.30,.44,.16),black,model,part,pivot)
        part = 3 if side < 0 else 4
        pivot = (side*.46,0,1.28)
        box(name+' sleeve',(side*.46,0,1.09),(.25,.32,.44),shirt,model,part,pivot)
        box(name+' hand',(side*.47,.015,.79),(.23,.25,.24),skin,model,part,pivot)
    if model == 1:
        box('Backpack',(0,-.29,1.02),(.52,.25,.56),pack,model)
        box('Backpack clasp',(0,-.425,1.05),(.12,.035,.16),gold,model)
    for obj in set(bpy.data.objects)-start:
        obj['character_name'] = name

character(1,'Mark',red,blue,hair_brown)
character(2,'Nicholas and Brandon',white,dark,hair_black)
character(3,'Isaac',white,dark,hair_black,coat=True)
character(4,'Jennifer',pink,white,hair_brown,girl=True)

actors = [dict(name='Mark',model=1,x=14,z=28,heading=180),
          dict(name='Isaac',model=3,x=12,z=4,heading=180,map_x=12,map_y=4),
          dict(name='Nicholas',model=2,x=6,z=10,heading=0,map_x=6,map_y=10),
          dict(name='Brandon',model=2,x=22,z=12,heading=180,map_x=22,map_y=12),
          dict(name='Jennifer',model=4,x=14,z=18,heading=0,map_x=14,map_y=18)]

manifest = dict(format='L643',textures=texture_names,actors=actors,colliders=colliders,
                camera=dict(eye=[14,9,34],target=[14,0,21.5],fov=58),
                note='Tall source pillar silhouettes represent height; solid feet use 2x2 footprints.')
bpy.context.scene['lightning_manifest']=json.dumps(manifest)

# Convert to Blender's right-handed Z-up space for the preview. The exported
# X-east/Y-up/Z-south space above is already right handed.
for obj in bpy.context.scene.objects:
    if obj.type == 'MESH':
        for vertex in obj.data.vertices:
            vertex.co.y = -vertex.co.y
        obj.data.flip_normals()

# Preview instances in Blender share the exported model mesh data.
templates = [o for o in list(bpy.context.scene.objects) if o.type == 'MESH' and o['model']]
collection = bpy.data.collections.new('Character models (local origin)')
bpy.context.scene.collection.children.link(collection)
for obj in templates:
    for coll in list(obj.users_collection):
        coll.objects.unlink(obj)
    collection.objects.link(obj)
    obj.hide_render = True
    obj.hide_set(True)
for actor in actors:
    parent = bpy.data.objects.new(actor['name']+' placement',None)
    bpy.context.collection.objects.link(parent)
    parent.location=(actor['x'],-actor['z'],0)
    parent.rotation_euler.z=math.radians(actor['heading'])
    for template in templates:
        if template['model'] != actor['model']:
            continue
        obj = template.copy()
        obj['export_source'] = False
        obj.hide_render=False
        bpy.context.collection.objects.link(obj)
        obj.hide_set(False)
        obj.parent=parent

bpy.ops.object.camera_add(location=(14,-34,9))
camera=bpy.context.object
camera.name='Entrance camera'
camera.rotation_euler=(Vector((14,-21.5,0))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='PERSP'
camera.data.lens_unit='FOV'
camera.data.angle=2*math.atan(math.tan(math.radians(58)/2)*4/3)
bpy.context.scene.camera=camera
scene_settings=bpy.context.scene
scene_settings.render.engine='BLENDER_EEVEE'
scene_settings.render.resolution_x=960
scene_settings.render.resolution_y=720
scene_settings.render.resolution_percentage=100
scene_settings.world.color=(.02,.02,.02)
scene_settings.view_settings.view_transform='Standard'
scene_settings.render.filepath=str(OUT/'blender-preview.png')
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_scene import export_scene
manifest=export_scene()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'lightning_club.blend'))
bpy.ops.render.render(write_still=True)
