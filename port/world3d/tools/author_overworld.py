"""Author the imagegen-approved island in Blender and export its actual meshes.

Blender --background --python port/world3d/tools/author_overworld.py

This owns only map 0. The source route/node coordinates and every interior and
actor export remain unchanged. Materials use opaque 32/64-pixel textures and
baked vertex shading, so the editable scene and N64 draw the same geometry.
"""
import bpy
from mathutils import Vector
from mathutils.geometry import delaunay_2d_cdt
import math
import json
import os
import random
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_world import Scene, texture
from world_manifest import write_world_metadata

OUT=ROOT/'generated'
RNG=random.Random(7164)
SEA=-.62
SUN=Vector((-.55,.65,.9)).normalized()
WHITE=(239,233,208,255)
DARK=(32,38,42,255)
BLUE=(59,113,176,255)
RED=(211,35,30,255)
GOLD=(230,181,45,255)

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.name='Overworld — concept model'
for collection in list(bpy.data.collections):
    if collection.name!='Collection':bpy.data.collections.remove(collection)
root_collection=bpy.data.collections.get('Collection')
root_collection.name='Island'
active_collection=root_collection
materials={}
texture_rows={}
all_land=[]
ground_routes=[]
building_footprints=[]
landmarks=[]
ground_casters=[]
# SUN is in Blender axes (Z up, Y north). Ground coordinates use Z south.
SUN_GROUND=(-SUN.x/SUN.z,SUN.y/SUN.z)
GROUND_SPLIT_BUDGET=32


def group(name):
    global active_collection
    collection=bpy.data.collections.new(name)
    scene.collection.children.link(collection)
    active_collection=collection
    return collection


def linear(v):
    x=v/255
    return x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4


def srgb(v):
    return max(0,min(255,round((12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055)*255)))


def lattice(x,y,seed):
    n=(x*374761393+y*668265263+seed*1442695041)&0xffffffff
    n=((n^(n>>13))*1274126177)&0xffffffff
    return ((n^(n>>16))&65535)/65535


def noise(x,y,n,seed):
    ix,iy=math.floor(x),math.floor(y)
    tx,ty=x-ix,y-iy
    tx=tx*tx*(3-2*tx);ty=ty*ty*(3-2*ty)
    a=lattice(ix%n,iy%n,seed);b=lattice((ix+1)%n,iy%n,seed)
    c=lattice(ix%n,(iy+1)%n,seed);d=lattice((ix+1)%n,(iy+1)%n,seed)
    return (a+(b-a)*tx)*(1-ty)+(c+(d-c)*tx)*ty


def make_texture(kind):
    width,height=(64,32) if kind in ('grass','water') else (32,64) if kind=='cliff' else (32,32)
    palettes={
        'grass':((48,119,19),(143,199,46)),
        'dirt':((153,111,49),(218,190,112)),
        'cliff':((89,67,43),(207,173,99)),
        'rock':((115,86,45),(225,187,109)),
        'sand':((180,152,91),(241,221,165)),
        'water':((20,68,188),(43,126,247)),
        'stone':((159,155,139),(239,231,202)),
        'wood':((72,44,25),(177,132,66)),
        'red_roof':((128,19,23),(236,62,39)),
        'green_roof':((22,78,44),(69,151,74)),
        'gold_roof':((137,91,18),(249,204,51)),
        'blue_roof':((24,42,66),(73,110,146)),
        'pine':((18,75,20),(71,155,32)),
        'glass':((18,36,71),(47,100,163)),
    }
    low,high=palettes[kind]
    seed=list(palettes).index(kind)+71
    rows=[]
    for y in range(height):
        row=[]
        for x in range(width):
            a=noise(x/width*4,y/height*4,4,seed)
            b=noise(x/width*12,y/height*12,12,seed+2)
            grain=lattice(x,y,seed+5)
            value=.12+.55*a+.23*b+.10*grain
            if kind=='grass':
                value=.1+.7*a+.16*b+.14*grain
                if grain>.94:value+=.12
                if ((x+y*3)%11==0) and grain<.35:value-=.17
            elif kind=='water':
                value=.12+.45*a+.28*b+.15*grain
                wave=math.sin((x/width*3+y/height*7+.2*math.sin(x/width*math.tau))*math.tau)
                if wave>.91 and b>.58:value=min(1.3,value+.48)
            elif kind in ('cliff','rock'):
                streak=noise(x/width*13,y/height*3,13,seed+7)
                value=.05+.5*a+.3*b+.2*streak
                if streak<.22:value-=.23
                if kind=='cliff':value*=.77+.23*(1-y/height)
            elif kind=='stone':
                brick_y=y//8;brick_x=(x+(brick_y%2)*8)%16
                value=.42+.35*a+.18*grain
                if y%8==0 or brick_x==0:value-=.28
                if y%8==1:value+=.13
            elif kind=='wood':
                value=.25+.45*noise(x/width*8,y/height*2,8,seed)+.18*grain
                if x%8==0:value-=.35
                if x%8==1:value+=.18
                if int(y+noise(x/width*4,y/height*4,4,seed)*6)%13==0:value-=.12
            elif kind.endswith('roof'):
                value=.3+.4*a+.18*grain
                yy=y%8;xx=(x+(y//8%2)*4)%8
                if yy==0 or xx==0:value-=.28
                if yy==1:value+=.25
            elif kind=='pine':
                value=.15+.52*a+.18*grain
                value+=.15*math.cos((x*.4+y*.9)*math.pi)
            elif kind=='glass':
                value=.4+.48*(1-y/height)+.08*grain
                if abs(x-y*.35-9)<1.5:value+=.25
                if x in (0,31) or y in (0,31):value-=.4
            value=max(0,min(1.15,value))
            row.append(tuple(max(0,min(255,round(lo+(hi-lo)*value))) for lo,hi in zip(low,high))+(255,))
        rows.append(row)
    key=texture(rows)
    texture_rows[key]=rows
    return key


def material(name,kind=None,color=WHITE,tile=False,scale=1,rows=None):
    mat=bpy.data.materials.new(name)
    mat.use_nodes=True
    nodes=mat.node_tree.nodes;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial')
    shader=nodes.new('ShaderNodeEmission')
    mat.node_tree.links.new(shader.outputs[0],out.inputs[0])
    col=nodes.new('ShaderNodeVertexColor');col.layer_name='Color'
    if kind or rows is not None:
        key=make_texture(kind) if rows is None else texture(rows)
        if rows is not None:texture_rows[key]=rows
        image=bpy.data.images.load(str(OUT/'textures'/(key+'.png')),check_existing=True)
        image.pack()
        tex=nodes.new('ShaderNodeTexImage');tex.image=image;tex.interpolation='Linear';tex.extension='REPEAT'
        mul=nodes.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1
        mat.node_tree.links.new(tex.outputs['Color'],mul.inputs[1])
        mat.node_tree.links.new(col.outputs['Color'],mul.inputs[2])
        mat.node_tree.links.new(mul.outputs[0],shader.inputs['Color'])
        mat['native_texture']=key
    else:
        mat.node_tree.links.new(col.outputs['Color'],shader.inputs['Color'])
    mat['native_tile']=tile;mat['native_scale']=scale;mat['native_color']=color
    materials[name]=mat
    return mat


GRASS=material('Grass — painted moss and leaves','grass',tile=True,scale=1.8)
PATH=material('Paths — sand and small stones','dirt',tile=True,scale=1.2)
CLIFF=material('Cliffs — layered warm rock','cliff')
ROCK=material('Rocks — ochre facets','rock')
SAND=material('Beach — pale sand','sand',tile=True,scale=1.2)
WATER=material('Sea — blue wavelets','water',color=(255,255,255,255),tile=True,scale=3)
STONE=material('Buildings — pale masonry','stone')
WOOD=material('Bridge and doors — timber','wood')
REDROOF=material('Roof — red tiles','red_roof')
GREENROOF=material('Roof — green tiles','green_roof')
GOLDROOF=material('Roof — gold tiles','gold_roof')
BLUEROOF=material('Roof — blue slate','blue_roof')
PINE=material('Conifers — painted branches','pine')
GLASS=material('Windows — blue glass','glass')
TRIM=material('Trim — warm white',color=WHITE)
DARKMAT=material('Inset — blue charcoal',color=DARK)
REDMAT=material('Accent — red',color=RED)
GOLDMAT=material('Accent — gold',color=GOLD)
BLUEMAT=material('Accent — blue',color=BLUE)
FOAM=material('Foam — pale cyan',color=(176,225,235,255))
FOAMWHITE=material('Foam — white tips',color=(246,249,230,255))


def world(v):
    return Vector((v[0],-v[2],v[1]))


def height(x,z):
    return .35+.035*math.sin(x*.7)+.026*math.cos(z*.9)


def smoothstep(lo,hi,value):
    t=max(0,min(1,(value-lo)/(hi-lo)))
    return t*t*(3-2*t)


def footprint_distance(x,z,caster,dx=0,dz=0,contact=False):
    px,pz=x-caster['x']-dx,z-caster['z']-dz
    if caster['shape']=='rect':
        return math.hypot(max(abs(px)-caster['w']/2,0),max(abs(pz)-caster['d']/2,0))
    radius=caster['contact_radius'] if contact else caster['radius']
    return max(0,math.hypot(px,pz)-radius)


def rect_caster(name,x,z,w,d,h):
    ground_casters.append(dict(name=name,shape='rect',x=x+w/2,z=z+d/2,
        w=w,d=d,height=h,base=height(x+w/2,z+d/2)))


def round_caster(name,x,z,r,h,contact_radius=None):
    ground_casters.append(dict(name=name,shape='round',x=x,z=z,radius=r,
        contact_radius=r if contact_radius is None else contact_radius,
        height=h,base=height(x,z)))


def grounding_factor(x,z,y,casters=None):
    """Soft contact and short sunward projections, evaluated on real ground.

    Only ground vertex colors receive this field. A shadow cannot extend a
    polygon into the sea, climb a wall, or become a stack of black tree discs.
    Overlapping casters use the strongest term rather than accumulating ink.
    """
    occlusion=0
    for caster in ground_casters if casters is None else casters:
        building=caster['shape']=='rect' or caster['contact_radius']==caster.get('radius')
        contact=(.23 if building else .18)*(1-smoothstep(0,.32 if building else .28,
            footprint_distance(x,z,caster,contact=True)))
        rise=max(0,min(caster['height'],caster['base']+caster['height']*.7-y))
        dx,dz=(c*rise*.5 for c in SUN_GROUND)
        cast=(.23 if building else .18)*(1-smoothstep(0,.34,
            footprint_distance(x,z,caster,dx,dz)))
        occlusion=max(occlusion,1-(1-contact)*(1-cast))
    return max(.60,1-occlusion)


def coast_distance(x,z):
    best=float('inf')
    for outline in all_land:
        if inside(x,z,outline):return 0
        for (ax,az),(bx,bz) in zip(outline,outline[1:]+outline[:1]):
            dx,dz=bx-ax,bz-az
            t=max(0,min(1,((x-ax)*dx+(z-az)*dz)/(dx*dx+dz*dz)))
            best=min(best,math.hypot(x-ax-dx*t,z-az-dz*t))
    return best


def shoreline_distance(x,z):
    """Distance to the nearest shoreline from either side, for small beaches."""
    best=float('inf')
    for outline in all_land:
        for (ax,az),(bx,bz) in zip(outline,outline[1:]+outline[:1]):
            dx,dz=bx-ax,bz-az
            t=max(0,min(1,((x-ax)*dx+(z-az)*dz)/(dx*dx+dz*dz)))
            best=min(best,math.hypot(x-ax-dx*t,z-az-dz*t))
    return best


def prepare_ground_samples():
    """Add a few samples around casters, without making shadow overlay meshes."""
    import bmesh
    budget=GROUND_SPLIT_BUDGET
    for obj in scene.objects:
        if not obj.get('ground_receiver') or obj.get('ground_prepared'):continue
        data=obj.data
        if obj.get('grass_shelf') and budget:
            bm=bmesh.new();bm.from_mesh(data)
            candidates=[]
            for edge in bm.edges:
                if edge.calc_length()<.55:continue
                v=(edge.verts[0].co+edge.verts[1].co)*.5
                midpoint=grounding_factor(v.x,-v.y,v.z)
                endpoints=sum(grounding_factor(p.co.x,-p.co.y,p.co.z) for p in edge.verts)*.5
                error=abs(midpoint-endpoints)
                # Spend the tiny refinement budget where a small contact or
                # cast shadow would otherwise disappear between two samples.
                if error>.025:candidates.append((error,edge))
            edges=[edge for error,edge in sorted(candidates,key=lambda item:-item[0])[:budget]]
            if edges:
                bmesh.ops.subdivide_edges(bm,edges=edges,cuts=1,use_grid_fill=True)
                budget-=len(edges)
            bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
            bm.to_mesh(data);bm.free();data.update()
        if obj.get('grass_shelf'):
            if SAND.name not in data.materials:data.materials.append(SAND)
            sand_index=list(data.materials).index(SAND)
            uv=data.uv_layers.active.data
            for poly in data.polygons:
                coords=[data.vertices[i].co for i in poly.vertices]
                x=sum(v.x for v in coords)/len(coords);z=-sum(v.y for v in coords)/len(coords)
                beach=(.8<x<6.6 and 15.65<z<17.3) or (13.4<x<15.8 and 13.85<z<14.7)
                if data.materials[poly.material_index]!=GRASS:continue
                if not beach or shoreline_distance(x,z)>.38:continue
                if max(shoreline_distance(v.x,-v.y) for v in coords)>.75:continue
                poly.material_index=sand_index
                for li in poly.loop_indices:
                    v=data.vertices[data.loops[li].vertex_index].co
                    uv[li].uv=(v.x/SAND['native_scale'],-v.y/SAND['native_scale'])
        obj['ground_prepared']=True


def bake_ground_lighting():
    prepare_ground_samples()
    shaded_vertices=0
    for obj in scene.objects:
        ground=obj.get('ground_receiver');sea=obj.get('sea_receiver')
        if not ground and not sea:continue
        data=obj.data;colors=data.color_attributes['Color']
        original=data.color_attributes.get('UnbakedColor')
        if original is None:
            original=data.color_attributes.new(name='UnbakedColor',type='FLOAT_COLOR',domain='CORNER')
            for i,color in enumerate(colors.data):original.data[i].color=color.color
        factors={}
        for li,loop in enumerate(data.loops):
            v=obj.matrix_world@data.vertices[loop.vertex_index].co
            key=(round(v.x,6),round(-v.y,6),round(v.z,6))
            if key not in factors:
                if sea:
                    factors[key]=.9+.1*smoothstep(0,4.5,coast_distance(v.x,-v.y))
                elif abs(v.z-height(v.x,-v.y))<.1 and any(inside(v.x,-v.y,p) for p in all_land):
                    factors[key]=grounding_factor(v.x,-v.y,v.z)
                else:factors[key]=1
            factor=factors[key]
            base=original.data[li].color
            colors.data[li].color=tuple(linear(round(srgb(c)*factor)) for c in base[:3])+(base[3],)
            shaded_vertices+=factor<.999
    scene['grounding_bake']=json.dumps(dict(casters=len(ground_casters),shaded_corners=shaded_vertices,
        split_edge_budget=GROUND_SPLIT_BUDGET,sun_ground=list(SUN_GROUND),opaque_overlays=False))


def mesh(name,verts,faces,mat,up=False,uvs=None,shade=1,face_materials=None):
    data=bpy.data.meshes.new(name)
    verts=[world(v) for v in verts]
    if up:
        faces=[tuple(reversed(f)) if (verts[f[1]]-verts[f[0]]).cross(verts[f[2]]-verts[f[0]]).z<0 else f for f in faces]
    data.from_pydata(verts,[],faces)
    face_materials=face_materials or [mat]*len(faces)
    assert len(face_materials)==len(faces)
    palette=[]
    for surface in face_materials:
        if surface not in palette:palette.append(surface);data.materials.append(surface)
    data.update()
    uv=data.uv_layers.new(name='UVMap')
    col=data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for poly in data.polygons:
        surface=face_materials[poly.index];poly.material_index=palette.index(surface)
        base=surface['native_color']
        normal=poly.normal
        light=(.72+.28*max(0,normal.dot(SUN)))*shade
        if surface in (FOAM,FOAMWHITE):light=1
        coords=[data.vertices[v].co for v in poly.vertices]
        dominant=max(range(3),key=lambda i:abs(normal[i]))
        axes=(0,1) if dominant==2 else (0,2) if dominant==1 else (1,2)
        mins=[min(v[k] for v in coords) for k in axes]
        spans=[max(v[k] for v in coords)-mins[i] for i,k in enumerate(axes)]
        for j,li in enumerate(poly.loop_indices):
            v=data.vertices[data.loops[li].vertex_index].co
            if uvs:coord=uvs[poly.index][j]
            elif surface['native_tile']:coord=(v.x/surface['native_scale'],-v.y/surface['native_scale'])
            else:coord=tuple((v[k]-mins[i])/spans[i] if spans[i]>.00001 else 0 for i,k in enumerate(axes))
            uv.data[li].uv=coord
            # A small position-dependent tint keeps wide top faces painted,
            # rather than uniformly lit, while remaining baked native colors.
            local=light
            if surface==GRASS:
                normal=Vector((-.0245*math.cos(v.x*.7),-.0234*math.sin(-v.y*.9),1)).normalized()
                local=(.83+.17*max(0,normal.dot(SUN)))*(.96+.04*math.sin(v.x*.6+v.y*.4)**2)
            elif surface==WATER:local=1
            col.data[li].color=tuple(linear(min(255,round(c*local))) for c in base[:3])+(1,)
    obj=bpy.data.objects.new(name,data);active_collection.objects.link(obj)
    obj['native_export']=True
    return obj


def box(name,x,z,w,d,h,mat,y=None,top=None):
    if y is None:y=height(x+w/2,z+d/2)
    v=[(x,y,z),(x+w,y,z),(x+w,y,z+d),(x,y,z+d),
       (x,y+h,z),(x+w,y+h,z),(x+w,y+h,z+d),(x,y+h,z+d)]
    f=[(0,1,2,3),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0),(4,7,6,5)]
    return mesh(name,v,f,mat,face_materials=[mat]*5+[top or mat])


def cylinder(name,x,z,y,r,h,mat,n=8,top_r=None):
    if top_r is None:top_r=r
    verts=[(x+r*math.cos(i*math.tau/n),y,z+r*math.sin(i*math.tau/n)) for i in range(n)]
    verts += [(x+top_r*math.cos(i*math.tau/n),y+h,z+top_r*math.sin(i*math.tau/n)) for i in range(n)]
    faces=[tuple(range(n)),tuple(reversed(range(n,n*2)))]
    faces += [(i,n+i,n+(i+1)%n,(i+1)%n) for i in range(n)]
    return mesh(name,verts,faces,mat)


def cone(name,x,z,y,r,h,mat,n=6,shade=1):
    verts=[(x+r*math.cos(i*math.tau/n),y,z+r*math.sin(i*math.tau/n)) for i in range(n)]+[(x,y+h,z)]
    return mesh(name,verts,[(i,n,(i+1)%n) for i in range(n)],mat,shade=shade)


def roof_tag(obj,site,tier=0,role='shell'):
    obj['roof_site']=site;obj['roof_tier']=tier;obj['roof_role']=role
    return obj


def cap_uv(obj):
    # The owned final cap uses native image rows north -> south, matching the
    # original verified roof badge rather than Blender's reversed Y axis.
    data=obj.data;poly=data.polygons[-1]
    coords=[data.vertices[i].co for i in poly.vertices]
    left,right=min(v.x for v in coords),max(v.x for v in coords)
    north,south=min(-v.y for v in coords),max(-v.y for v in coords)
    for li in poly.loop_indices:
        v=data.vertices[data.loops[li].vertex_index].co
        data.uv_layers.active.data[li].uv=((v.x-left)/(right-left),(-v.y-north)/(south-north))
    return obj


def roof(name,x,z,w,d,y,rise,mat,site=None,tier=0,cap_raise=0,cap_mat=None):
    # One outer shell: trim is the exposed fascia, never a second full roof.
    fascia=.12
    assert rise>fascia
    cut=min(w,d)*.13
    outline=[(x+cut,z),(x+w-cut,z),(x+w,z+cut),(x+w,z+d-cut),
             (x+w-cut,z+d),(x+cut,z+d),(x,z+d-cut),(x,z+cut)]
    verts=[(a,y,b) for a,b in outline]
    verts += [(a,y+fascia,b) for a,b in outline]
    cap=[(x+w/2+(a-x-w/2)*.66,y+rise,z+d/2+(b-z-d/2)*.48) for a,b in outline]
    verts+=cap
    faces=[tuple(range(8))];mats=[TRIM]
    for i in range(8):
        j=(i+1)%8
        faces.append((i,8+i,8+j,j));mats.append(TRIM)
        faces.append((8+i,16+i,16+j,8+j));mats.append(mat)
    cap_start=16
    if cap_raise:
        verts += [(x+w/2+(a-x-w/2)*.82,yy+cap_raise,z+d/2+(b-z-d/2)*.82) for a,yy,b in cap]
        for i in range(8):
            j=(i+1)%8;faces.append((16+i,24+i,24+j,16+j));mats.append(mat)
        cap_start=24
    faces.append(tuple(reversed(range(cap_start,cap_start+8))));mats.append(cap_mat or mat)
    obj=cap_uv(mesh(name,verts,faces,mat,face_materials=mats))
    return roof_tag(obj,site or name,tier)


def gable(name,x,z,w,d,y,rise,mat,site=None,tier=0):
    fascia=.12
    verts=[(x,y,z),(x+w,y,z),(x+w,y,z+d),(x,y,z+d),
           (x,y+fascia,z),(x+w,y+fascia,z),(x+w,y+fascia,z+d),(x,y+fascia,z+d),
           (x+w*.5,y+rise,z),(x+w*.5,y+rise,z+d)]
    faces=[(0,1,2,3),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0),
           (4,8,5),(7,6,9),(4,7,9,8),(8,9,6,5)]
    obj=mesh(name,verts,faces,mat,face_materials=[TRIM]*5+[mat]*4)
    return roof_tag(obj,site or name,tier)


def flat_roof(name,x,z,w,d,y,mat,site,cap_mat=None):
    rim=.08;h=.15
    corners=[(x,z),(x+w,z),(x+w,z+d),(x,z+d)]
    verts=[(a,y,b) for a,b in corners]+[(a,y+h,b) for a,b in corners]
    verts += [(x+rim,y+h,z+rim),(x+w-rim,y+h,z+rim),
              (x+w-rim,y+h,z+d-rim),(x+rim,y+h,z+d-rim)]
    faces=[(0,1,2,3)];mats=[TRIM]
    for i in range(4):
        j=(i+1)%4
        faces.extend([(i,4+i,4+j,j),(4+i,8+i,8+j,4+j)]);mats.extend([TRIM,TRIM])
    faces.append((8,11,10,9));mats.append(cap_mat or mat)
    return roof_tag(cap_uv(mesh(name,verts,faces,mat,face_materials=mats)),site)


def round_roof(name,x,z,profile,side_mats,inner_radius,recess,center_mat,site,n=8):
    # A continuous annular roof with a recessed center. Intermediate profile
    # rings have no caps, so neighboring roof stages cannot fight for depth.
    assert len(side_mats)==len(profile)-1 and 0<inner_radius<profile[-1][0]
    verts=[]
    for radius,y in profile:
        verts += [(x+radius*math.cos(i*math.tau/n),y,z+radius*math.sin(i*math.tau/n)) for i in range(n)]
    top_y=profile[-1][1];inner=len(verts)
    verts += [(x+inner_radius*math.cos(i*math.tau/n),top_y,z+inner_radius*math.sin(i*math.tau/n)) for i in range(n)]
    floor=len(verts)
    verts += [(x+inner_radius*math.cos(i*math.tau/n),top_y-recess,z+inner_radius*math.sin(i*math.tau/n)) for i in range(n)]
    faces=[tuple(range(n))];mats=[side_mats[0]]
    for ring,material in enumerate(side_mats):
        for i in range(n):
            j=(i+1)%n;lo=ring*n;hi=lo+n
            faces.append((lo+i,hi+i,hi+j,lo+j));mats.append(material)
    outer=(len(profile)-1)*n
    for i in range(n):
        j=(i+1)%n
        faces.extend([(outer+i,inner+i,inner+j,outer+j),(inner+i,floor+i,floor+j,inner+j)])
        mats.extend([side_mats[-1]]*2)
    faces.append(tuple(reversed(range(floor,floor+n))));mats.append(center_mat)
    return roof_tag(cap_uv(mesh(name,verts,faces,side_mats[0],face_materials=mats)),site)


def rock(name,x,z,scale,mat=ROCK):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=1,location=(0,0,0))
    old=bpy.context.object
    verts=[(x+v.co.x*scale[0],height(x,z)+v.co.z*scale[1],z-v.co.y*scale[2]) for v in old.data.vertices]
    faces=[tuple(p.vertices) for p in old.data.polygons]
    bpy.data.objects.remove(old,do_unlink=True)
    return mesh(name,verts,faces,mat)


def inside(x,z,poly):
    result=False
    for (a,b),(c,d) in zip(poly,poly[1:]+poly[:1]):
        if (b>z)!=(d>z) and x<(c-a)*(z-b)/(d-b)+a:result=not result
    return result


def smooth(poly,steps=2):
    result=[]
    for i,p1 in enumerate(poly):
        p0,p2,p3=poly[i-1],poly[(i+1)%len(poly)],poly[(i+2)%len(poly)]
        for j in range(steps):
            t=j/steps;t2=t*t;t3=t2*t
            result.append(tuple(.5*((2*p1[k])+(-p0[k]+p2[k])*t+(2*p0[k]-5*p1[k]+4*p2[k]-p3[k])*t2+(-p0[k]+3*p1[k]-3*p2[k]+p3[k])*t3) for k in range(2)))
    return result


def island(name,control):
    outline=smooth(control)
    all_land.append(outline)
    points=[Vector((x,-z)) for x,z in outline]
    for z in range(math.floor(min(v[1] for v in outline)),math.ceil(max(v[1] for v in outline))+1):
        for x in range(math.floor(min(v[0] for v in outline)),math.ceil(max(v[0] for v in outline))+1):
            px,pz=x+.09*math.sin(z*3.7),z+.08*math.cos(x*4.2)
            if inside(px,pz,outline):points.append(Vector((px,-pz)))
    edges=[(i,(i+1)%len(outline)) for i in range(len(outline))]
    vv,ee,ff,*_=delaunay_2d_cdt(points,edges,[list(range(len(outline)))],1,.00001)
    verts=[(v.x,height(v.x,-v.y),-v.y) for v in vv]
    ground=mesh(name+' grass shelf',verts,ff,GRASS,up=True)
    ground['ground_receiver']=True;ground['grass_shelf']=True
    ground['land_outline_index']=len(all_land)-1
    # All cliff and foam rings share the same displaced point at each coast
    # vertex; independently offset edge normals would leave blue cracks.
    coast_rings=[[],[],[],[]];normals=[]
    for i,(x,z) in enumerate(outline):
        px,pz=outline[i-1];nx,nz=outline[(i+1)%len(outline)]
        dx,dz=nx-px,nz-pz;length=math.hypot(dx,dz)
        outward=(dz/length,-dx/length);normals.append(outward)
        for level in range(4):
            distance=(0,.015,.1,.19)[level]+(.025*math.sin(i*2.9) if level else 0)
            y=(height(x,z),-.02,-.34,SEA+.025)[level]
            coast_rings[level].append((x+outward[0]*distance,y,z+outward[1]*distance))
    for i,(x,z) in enumerate(outline):
        j=(i+1)%len(outline);nx,nz=outline[j]
        for level in range(3):
            a,b=coast_rings[level][i],coast_rings[level][j]
            c,d=coast_rings[level+1][i],coast_rings[level+1][j]
            mesh(name+' cliff',[a,b,d,c],[(0,1,2,3)],CLIFF,
                 uvs=[[(0,level/3),(1,level/3),(1,(level+1)/3),(0,(level+1)/3)]],shade=1-level*.08)
        oa,ob=normals[i],normals[j]
        def point(x,z,o,d,y):return (x+o[0]*d,y,z+o[1]*d)
        inner=[point(x,z,oa,.205,SEA+.018),point(nx,nz,ob,.205,SEA+.018)]
        outer=[point(x,z,oa,.31,SEA+.018),point(nx,nz,ob,.31,SEA+.018)]
        wash=mesh(name+' foam wash',[inner[0],inner[1],outer[1],outer[0]],[(0,1,2,3)],FOAM,up=True)
        # Color interpolation fades a narrow cyan wash into cobalt water;
        # white breaker tips stay sparse rather than outlining every cliff.
        for li,loop in enumerate(wash.data.loops):
            if loop.vertex_index>=2:
                wash.data.color_attributes['Color'].data[li].color=tuple(linear(c) for c in (45,112,213))+(1,)
        if i%4==0:
            mesh(name+' white breaker',[inner[0],inner[1],
                 point(nx,nz,ob,.24,SEA+.028),point(x,z,oa,.24,SEA+.028)],[(0,1,2,3)],FOAMWHITE,up=True)
    return outline


group('Ocean and organic coast')
sea_axis=(-12,-4,4,12,20,32)
sea_vertices=[(x,SEA,z) for z in sea_axis for x in sea_axis]
sea_faces=[]
for z in range(5):
    for x in range(5):
        a=z*6+x;sea_faces.append((a,a+1,a+7,a+6))
ocean=mesh('Sea',sea_vertices,sea_faces,WATER,up=True);ocean['sea_receiver']=True
main=[(3.25,4.7),(4.4,4.6),(5.3,5.2),(6.5,5.45),(7.2,4.35),(7.45,3.8),(9.0,3.75),(9.7,4.6),(9.7,5.65),
      (11.1,5.2),(11.15,3.7),(12.1,3.2),(12.9,3.15),(13.2,2.05),(14.45,1.75),(15.4,.85),(16.6,1.05),
      (17.65,2.2),(18.05,3.65),(17.85,4.9),(18.65,6.0),(19.1,7.4),(19.0,9.4),(18.65,10.7),(18.9,12.2),
      (18.65,14.6),(17.6,16.4),(15.55,17.05),(13.0,17.0),(11.6,16.3),(10.8,15.1),(11.0,14.6),
      (12.4,15.55),(14.1,16.0),(16.0,15.5),(17.25,14.2),(17.5,12.9),(16.9,12.0),(15.8,11.7),
      (14.15,11.85),(12.75,12.4),(12.15,13.6),(11.9,14.15),(10.5,13.65),(9.0,14.15),(8.6,15.5),
      (7.1,16.65),(4.6,17.0),(2.7,16.65),(1.25,16.1),(.85,14.75),(1.7,13.5),(3.35,13.2),(4.2,12.55),
      (4.05,11.7),(2.8,11.05),(1.6,10.5),(.9,9.1),(1.1,7.8),(2.2,6.8),(3.4,6.85),(4.6,7.1),(4.55,6.3)]
island('Main island',main)
island('Ishihara islet',[(.45,3.9),(.7,3.25),(1.7,3.0),(2.45,3.4),(2.7,4.35),(2.0,4.8),(.85,4.65)])
island('Water Club islet',[(13.4,13.1),(13.75,12.45),(14.7,12.35),(15.4,12.9),(15.55,13.75),(14.9,14.45),(13.8,14.3)])


def path(name,points,width=.24,mat=PATH):
    # Route contours become constraints in the grass shelf triangulation.
    # Shared strip edges avoid both floating decals and overlapping corner quads.
    samples=[]
    for a,b in zip(points,points[1:]):
        steps=max(1,math.ceil(math.hypot(b[0]-a[0],b[1]-a[1])/.8))
        samples.extend((a[0]+(b[0]-a[0])*j/steps,a[1]+(b[1]-a[1])*j/steps) for j in range(steps))
    samples.append(points[-1])
    left=[];right=[]
    for i,(x,z) in enumerate(samples):
        a=samples[max(0,i-1)];b=samples[min(len(samples)-1,i+1)]
        dx,dz=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dz)
        half=width*.5*(1+.08*math.sin(i*1.7))
        ox,oz=-dz/length*half,dx/length*half
        left.append((x-ox,z-oz));right.append((x+ox,z+oz))
    ground_routes.append((name,left+right[::-1],mat))


def conform_routes():
    # Constrained triangulation splits crossing strip borders and joins every
    # route/grass edge. The original island outline remains the clipping face.
    # Heights are evaluated once per shared vertex, preventing terrain cracks.
    for obj in list(scene.objects):
        if not obj.get('grass_shelf'):continue
        old=obj.data
        outline=all_land[obj['land_outline_index']]
        points=[Vector((x,-z)) for x,z in outline]
        points.extend(Vector((v.co.x,v.co.y)) for v in old.vertices)
        edges=[(i,(i+1)%len(outline)) for i in range(len(outline))]
        for name,poly,mat in ground_routes:
            start=len(points);points.extend(Vector((x,-z)) for x,z in poly)
            edges.extend((start+i,start+(i+1)%len(poly)) for i in range(len(poly)))
        vv,ee,ff,*_=delaunay_2d_cdt(points,edges,[list(range(len(outline)))],1,.00001)
        # CDT also returns closed route cycles outside a shelf. They are
        # constraints, not extra islands: clip by the original coastline.
        ff=[face for face in ff if inside(sum(vv[i].x for i in face)/len(face),
                -sum(vv[i].y for i in face)/len(face),outline) and
            abs(sum(vv[a].x*vv[b].y-vv[a].y*vv[b].x for a,b in zip(face,face[1:]+face[:1])))>2e-10]
        verts=[(v.x,height(v.x,-v.y),-v.y) for v in vv]
        mats=[]
        for face in ff:
            x=sum(verts[i][0] for i in face)/len(face)
            z=sum(verts[i][2] for i in face)/len(face)
            surface=GRASS
            for name,poly,mat in ground_routes:
                if inside(x,z,poly):surface=mat
            mats.append(surface)
        replacement=mesh(obj.name+' joined routes',verts,ff,GRASS,up=True,face_materials=mats)
        replacement['ground_receiver']=True;replacement['grass_shelf']=True
        replacement['terrain_surface_owner']=True
        replacement['terrain_outline']=json.dumps(outline)
        replacement['land_outline_index']=obj['land_outline_index']
        bpy.data.objects.remove(obj,do_unlink=True)
        if old.users==0:bpy.data.meshes.remove(old)


group('Sandy routes and beaches')
paths=[[(2.5,14),(4.5,14.8),(6.5,16),(7.5,14),(8.5,14),(8.5,10),(8.5,9),(6.5,6),(4.0,5.0)],
       [(3.5,8),(5.5,9.0),(8.5,10),(9.5,9.5),(12.5,10),(16.5,9)],
       [(5.5,11),(6.5,12.0),(8.5,14),(12.5,14),(12.5,16.5),(15.5,16.5)],
       [(8.5,5),(8.5,7),(8.5,9)],[(12.5,6.5),(12.5,10)],
       [(14.5,3),(12.5,7),(12.5,10)],[(16.5,5),(15.0,6.3),(12.5,7)]]
for i,points in enumerate(paths):path('Path '+str(i),points)
# The broad south-west beach is a sloping crescent, not a white island cap.
path('South-west sand',[(1.35,15.6),(2.1,16.2),(3.2,16.45),(4.5,16.7),(5.9,16.6)],.52,SAND)
conform_routes()


def beam(name,a,b,width,mat=WOOD):
    mid=(Vector(a)+Vector(b))*.5
    direction=world(b)-world(a)
    obj=box(name,-width/2,-width/2,width,width,direction.length,mat,y=0)
    # The authored box's long axis is Blender Z (world up).
    obj.location=world(a)
    obj.rotation_euler=direction.to_track_quat('Z','Y').to_euler()
    return obj


def bridge(name,a,b,width=.43):
    dx,dz=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dz)
    side=Vector((-dz/length,dx/length))
    steps=math.ceil(length/.13)
    for i in range(steps):
        t=(i+.5)/steps;x,z=a[0]+dx*t,a[1]+dz*t
        p=(x-side.x*width/2,.27,z-side.y*width/2)
        q=(x+side.x*width/2,.27,z+side.y*width/2)
        beam(name+' plank',p,q,.10)
    for sign in (-1,1):
        aa=(a[0]+sign*side.x*width*.62,.68,a[1]+sign*side.y*width*.62)
        bb=(b[0]+sign*side.x*width*.62,.68,b[1]+sign*side.y*width*.62)
        beam(name+' railing',aa,bb,.055)
        for j in range(5):
            t=j/4;x,z=aa[0]+(bb[0]-aa[0])*t,aa[2]+(bb[2]-aa[2])*t
            cylinder(name+' post',x,z,SEA,.045,.62-SEA,WOOD,n=4)


group('Timber bridges')
bridge('Ishihara footbridge',(2.25,4.25),(4.0,5.0))
bridge('Water Club approach',(14.5,13.75),(15.5,16.5),.36)


def front(name,x,z,w,base,roof_y):
    box(name+' doorway',x-.18,z+.025,.36,.06,.5,DARKMAT,y=base+.08)
    for dx in (-.22,.18):box(name+' door jamb',x+dx,z-.015,.055,.1,.57,TRIM,y=base+.05)
    box(name+' lintel',x-.22,z+.005,.455,.1,.065,GOLDMAT,y=base+.57)
    for dx in (-w*.34,w*.34):
        box(name+' blue window',x+dx-.115,z+.025,.23,.04,.27,GLASS,y=base+.29)
        box(name+' window sill',x+dx-.14,z+.005,.28,.07,.045,TRIM,y=base+.25)
    box(name+' door step',x-.28,z+.02,.56,.24,.08,STONE,y=base)


def hall(name,node,w=1.25,d=.95,roofmat=GREENROOF,roof_type='flat',cap_mat=None,cap_raise=0):
    x,z=node;center_z=z-d*.46
    y=height(x,z)
    rect_caster(name,x-w*.55,center_z-d*.55,w*1.1,d*1.1,1.1 if roof_type!='flat' else .9)
    landmarks.append(dict(name=name,x=x,y=z,width=w,depth=d))
    building_footprints.append((x-w*.64,center_z-d*.64,w*1.28,d*1.28))
    box(name+' foundation',x-w*.55,center_z-d*.55,w*1.1,d*1.1,.13,STONE,y=y)
    box(name+' walls',x-w*.5,center_z-d*.5,w,d,.62,STONE,y=y+.12)
    for dx in (-w*.5,w*.5-.09):box(name+' corner pillar',x+dx,center_z-d*.5,.09,d,.64,TRIM,y=y+.12)
    front(name,x,z+d*.05+.025,w,y+.12,y+.74)
    if roof_type=='flat':
        flat_roof(name+' roof',x-w*.56,center_z-d*.56,w*1.12,d*1.12,y+.74,roofmat,name,cap_mat)
    elif roof_type=='hip':
        roof(name+' roof',x-w*.6,center_z-d*.61,w*1.2,d*1.22,y+.74,.33,roofmat,
             site=name,cap_mat=cap_mat,cap_raise=cap_raise)
    else:assert roof_type=='none'
    return y,center_z


group('01 Mason Laboratory')
y,cz=hall('Mason Laboratory',(2.5,14),1.25,1.05,BLUEROOF,'none')
box('Mason side annex',1.52,13.33,.45,.55,.5,STONE,y=y+.08)
gable('Mason silver roof',1.7,12.85,1.6,1.23,y+.74,.46,TRIM,site='Mason Laboratory')
cylinder('Mason chimney',2.87,13.12,y+1.0,.07,.5,STONE,n=6)
cylinder('Mason gold chimney cap',2.87,13.12,y+1.46,.1,.07,GOLDMAT,n=6)
cylinder('Dish support',1.95,13.2,y+1.0,.04,.35,WOOD,n=4)
mesh('Laboratory satellite dish',[(1.95,y+1.27,13.2)]+[(1.95+.25*math.cos(i*math.tau/8),y+1.49,13.2+.25*math.sin(i*math.tau/8)) for i in range(8)],
     [(0,i+1,(i+1)%8+1) for i in range(8)],TRIM,up=True)
beam('Dish aerial',(1.95,y+1.30,13.2),(1.95,y+1.63,13.2),.025,BLUEMAT)

group('02 Ishihara House')
hall('Ishihara House',(1.5,4),.95,.8,GOLDROOF,'hip')

group('03 Fighting Club')
y,cz=hall('Fighting Club',(7.5,14),1.25,.9,REDROOF,'hip')
box('Fighting Club upper storey',7.19,cz-.27,.62,.54,.18,STONE,y=y+1.07)
gable('Fighting Club upper roof',7.05,cz-.38,.9,.76,y+1.25,.36,REDROOF,site='Fighting Club',tier=1)
for dx in (-.62,.62):
    roof_tag(cone('Fighting roof finial',7.5+dx,cz,y+.95,.05,.24,TRIM,n=4),'Fighting Club',role='ornament')

group('04 Rock Club and western hills')
for x,z,sx,sy,sz in [(2.2,7.5,.75,.95,.75),(3.1,6.9,.8,1.25,.65),(4.3,7.0,1,1.05,.6),
                     (5.2,7.7,.7,.75,.9),(4.9,9.2,.65,.9,.8),(2.1,9.1,.9,1.2,.8),(3.6,9.8,1.2,.62,.65)]:
    rock('Rock Club ridge',x,z,(sx,sy,sz))
y=height(3.5,8)
rect_caster('Rock Club gate',2.9,7.61,1.19,.5,.95)
box('Rock Club gate recess',2.98,7.66,1.04,.35,.68,DARKMAT,y=y)
for x in (2.91,3.93):box('Rock Club stone jamb',x,7.61,.15,.5,.85,STONE,y=y)
roof_tag(box('Rock Club gate lintel',2.9,7.61,1.19,.5,.17,STONE,y=y+.78),'Rock Club')
box('Rock Club timber door',3.08,7.97,.85,.04,.62,WOOD,y=y+.04)
for x in (3.23,3.65):box('Rock gate gold hinge',x,8.0,.11,.035,.06,GOLDMAT,y=y+.3)
box('Rock Club approach step',3.0,8.0,1,.32,.07,STONE,y=y)
landmarks.append(dict(name='Rock Club',x=3.5,y=8,width=1.2,depth=.7))
building_footprints.append((2.9,7.5,1.2,.7))
rock('Cave frame',4.75,9.25,(.55,.55,.55))
mesh('Small cave mouth',[(4.45,height(4.6,9.6)+.02,9.6),(4.95,height(4.6,9.6)+.02,9.6),(4.72,height(4.6,9.6)+.46,9.48)],[(0,1,2)],DARKMAT)

group('05 Water Club stadium')
x,z=14.5,12.98;y=height(x,z)
round_caster('Water Club',x,z,.77,1.2)
landmarks.append(dict(name='Water Club',x=14.5,y=13.5,width=1.5,depth=1.4))
building_footprints.append((13.7,12.15,1.6,1.6))
cylinder('Water Club octagonal walls',x,z,y,.7,.69,STONE,n=8)
cylinder('Water Club glass band',x,z,y+.28,.715,.32,GLASS,n=8)
for i in range(8):
    a=(i+.5)*math.tau/8
    cylinder('Water Club column',x+.68*math.cos(a),z+.68*math.sin(a),y+.08,.055,.77,TRIM,n=4)
round_roof('Water Club roof',x,z,[(.81,y+.75),(.7,y+.87),(.43,y+1.15)],
           [TRIM,STONE],.33,.12,GREENROOF,'Water Club')
front('Water Club',x,13.70,1.1,y+.03,y+.8)
for i in range(8):box('Water Club stair',x-.21,13.56+i*.085,.42,.09,.07,STONE,y=y+.035-i*.025)

group('06 Lightning Club')
# Paint the bolt into the top face. Separate tiny overlay triangles lose
# fragments against the roof in the N64's compressed depth buffer.
bolt=[(17,3),(25,3),(18,13),(25,13),(8,29),(13,18),(7,18)]
rows=[[DARK if inside(x+.5,z+.5,bolt) else GOLD for x in range(32)] for z in range(32)]
bolt_material=material('Roof — Lightning bolt',rows=rows,color=(255,255,255,255))
y,cz=hall('Lightning Club',(5.5,11),1.2,.87,GOLDMAT,cap_mat=bolt_material)

group('07 Grass Club')
leaf=[(4,23),(8,9),(27,4),(24,22),(14,28)]
green_rows=texture_rows[GREENROOF['native_texture']]
rows=[[(157,201,76,255) if inside(x+.5,z+.5,leaf) else green_rows[z][x] for x in range(32)] for z in range(32)]
leaf_material=material('Roof — Grass leaf',rows=rows)
y,cz=hall('Grass Club',(16.5,9),1.45,.9,GREENROOF,'hip',cap_mat=leaf_material)

group('08 Psychic Club obelisk')
x,z=12.5,6.15;y=height(x,z)
rect_caster('Psychic Club',x-.37,z-.28,.74,.62,1.5)
box('Psychic Club plinth',x-.37,z-.28,.74,.62,.18,STONE,y=y)
box('Psychic Club obelisk',x-.21,z-.19,.42,.4,1.02,STONE,y=y+.18)
roof('Psychic Club pointed crown',x-.22,z-.2,.44,.42,y+1.19,.31,TRIM,site='Psychic Club')
# One blocky rune replaces three subpixel glyphs. Its bars project at least
# two native pixels after the landmark's footprint enlargement below.
for dx in (-.19,.06):
    box('Psychic blue glyph upright',x+dx,z+.225,.13,.025,.64,BLUEMAT,y=y+.36)
box('Psychic blue glyph cross',x-.19,z+.225,.38,.025,.22,BLUEMAT,y=y+.57)
for dx in (-.52,.52):
    box('Psychic sentinel',x+dx-.1,z+.34,.2,.2,.3,STONE,y=y)
    cone('Psychic sentinel cap',x+dx,z+.44,y+.3,.15,.17,TRIM,n=4)
landmarks.append(dict(name='Psychic Club',x=12.5,y=6.5,width=1.4,depth=1))
building_footprints.append((11.8,5.8,1.4,1.1))

group('09 Science Club')
y,cz=hall('Science Club',(16.5,5),1.35,.92,BLUEROOF)
box('Science Club vent',16.7,cz-.24,.34,.38,.29,TRIM,y=y+.89,top=DARKMAT)

group('10 Fire Club volcano')
cx,cz=16.2,2.08;n=12
verts=[]
for ring,(radius,yy) in enumerate(((1.8,.34),(1.35,1.05),(.75,2.2),(.49,2.62))):
    for i in range(n):
        a=i*math.tau/n;jitter=1+.12*math.sin(i*4.1+ring)
        verts.append((cx+radius*jitter*math.cos(a),yy+.12*math.sin(i*2.1+ring),cz+radius*jitter*math.sin(a)))
faces=[]
for ring in range(3):
    for i in range(n):faces.append((ring*n+i,(ring+1)*n+i,(ring+1)*n+(i+1)%n,ring*n+(i+1)%n))
mesh('Fire Club faceted volcano',verts,faces,ROCK)
cylinder('Volcano dark crater',cx,cz,2.49,.43,.012,DARKMAT,n=12)
for x,z,sx,sy,sz in ((14.7,1.6,.65,.8,.7),(17.65,3.2,.55,.75,.7),(17.5,.95,.5,.5,.55)):
    rock('Volcano foothill',x,z,(sx,sy,sz))
hall('Fire Club',(14.5,3),1.13,.85,REDROOF,'hip')
cylinder('Volcano flagpole',cx,cz,2.51,.025,.95,WOOD,n=4)
mesh('Volcano red flag',[(cx,3.43,cz),(cx+.48,3.4,cz+.03),(cx+.3,3.15,cz+.12),(cx,3.12,cz)],[(0,1,2,3)],REDMAT)


def round_hall(name,x,z):
    y=height(x,z)
    round_caster(name,x,z,.72,1.32)
    cylinder(name+' lower tower',x,z,y,.62,.58,STONE,n=8)
    cylinder(name+' window row',x,z,y+.58,.63,.35,GLASS,n=8)
    for i in range(8):
        a=i*math.tau/8
        cylinder(name+' pale column',x+.59*math.cos(a),z+.59*math.sin(a),y+.4,.045,.62,TRIM,n=4)
    round_roof(name+' roof',x,z,[(.75,y+.96),(.62,y+1.13),(.47,y+1.31)],
               [STONE,TRIM],.33,.10,BLUEROOF,name)
    for i in range(7):box(name+' entrance stair',x-.18,z+.56+i*.08,.36,.085,.065,STONE,y=y+.15-i*.025)
    front(name,x,z+.65,.85,y+.14,y+.95)


group('11 Challenge Hall')
round_hall('Challenge Hall',8.5,4.48)
landmarks.append(dict(name='Challenge Hall',x=8.5,y=5,width=1.5,depth=1.5))
building_footprints.append((7.65,3.6,1.7,1.8))

group('12 Pokemon Dome')
# The badge belongs to the cap itself, without intersecting decal geometry.
red_rows=texture_rows[REDROOF['native_texture']]
rows=[]
for z in range(32):
    row=[]
    for x in range(32):
        radius=math.hypot(x-15.5,z-15.5)
        color=red_rows[z][x]
        if radius<14:
            color=DARK if radius>12 or abs(z-15.5)<2 else RED if z<16 else WHITE
            if math.hypot(x-15.5,z-15.5)<3:color=WHITE
        row.append(color)
    rows.append(row)
dome_material=material('Roof — Dome badge',rows=rows)
y,cz=hall('Pokemon Dome',(9.5,9.5),1.53,1.03,REDROOF,'hip',cap_mat=dome_material,cap_raise=.10)


def enlarge_landmarks():
    """Grow readable silhouettes about unchanged source entrance anchors.

    Hills, volcano, routes and coastline stay authored at their existing size.
    The islets and Lightning's north ridge limit depth growth. Foundations
    retain their height above local terrain, and lighting/tree masks follow
    exactly the same horizontal transform as their building.
    """
    specs=[('01 Mason Laboratory','Mason Laboratory',1.4,1.0),
           ('02 Ishihara House','Ishihara House',1.25,1.0),
           ('03 Fighting Club','Fighting Club',1.4,1.4),
           ('04 Rock Club and western hills','Rock Club',1.4,1.4),
           ('05 Water Club stadium','Water Club',1.18,1.18),
           ('06 Lightning Club','Lightning Club',1.4,1.1),
           ('07 Grass Club','Grass Club',1.4,1.4),
           ('08 Psychic Club obelisk','Psychic Club',1.5,1.5),
           ('09 Science Club','Science Club',1.4,1.4),
           ('10 Fire Club volcano','Fire Club',1.4,1.4),
           ('11 Challenge Hall','Challenge Hall',1.4,1.4),
           ('12 Pokemon Dome','Pokemon Dome',1.4,1.4)]
    assert len(landmarks)==len(building_footprints)==len(specs)
    for index,(collection_name,name,sx,sz) in enumerate(specs):
        landmark=landmarks[index]
        assert landmark['name']==name
        ax,az=landmark['x'],landmark['y']
        def point(x,z):return ax+(x-ax)*sx,az+(z-az)*sz
        for obj in bpy.data.collections[collection_name].objects:
            if obj.type!='MESH':continue
            if name=='Rock Club' and not obj.name.startswith(('Rock Club gate','Rock Club stone',
                    'Rock Club timber','Rock gate gold','Rock Club approach')):continue
            if name=='Fire Club' and (not obj.name.startswith('Fire Club') or 'volcano' in obj.name.lower()):continue
            inverse=obj.matrix_world.inverted()
            for vertex in obj.data.vertices:
                old=obj.matrix_world@vertex.co
                x,z=old.x,-old.y
                nx,nz=point(x,z)
                vertex.co=inverse@Vector((nx,-nz,old.z+height(nx,nz)-height(x,z)))
            obj.data.update()
        x,z,w,d=building_footprints[index]
        nx,nz=point(x,z)
        building_footprints[index]=(nx,nz,w*sx,d*sz)
        for caster in ground_casters:
            if not caster['name'].startswith(name):continue
            caster['x'],caster['z']=point(caster['x'],caster['z'])
            if caster['shape']=='rect':caster['w']*=sx;caster['d']*=sz
            else:
                assert sx==sz
                caster['radius']*=sx;caster['contact_radius']*=sx
            caster['base']=height(caster['x'],caster['z'])
        landmark['width']*=sx;landmark['depth']*=sz
    scene['landmark_footprint_scales']=json.dumps({name:[sx,sz] for _,name,sx,sz in specs})


enlarge_landmarks()


group('Conifer groves and ground shadows')
tree_templates=[]
def tree(x,z,scale=1):
    y=height(x,z)
    round_caster('Conifer',x,z,.35*scale,1.07*scale,contact_radius=.095*scale)
    # One linked template is reused by every conifer; instance transforms are
    # baked only when exporting the static native room.
    if not tree_templates:
        objs=[cylinder('Pine trunk template',0,0,0,.055,.31,WOOD,n=3),
              cone('Pine lower branches template',0,0,.14,.4,.45,PINE,n=6,shade=.86),
              cone('Pine middle branches template',0,0,.4,.32,.43,PINE,n=6,shade=1),
              cone('Pine crown template',0,0,.65,.23,.42,PINE,n=6,shade=1.1)]
        for o in objs:
            tree_templates.append(o);o.hide_render=True;o['native_export']=False
    for template in tree_templates:
        obj=bpy.data.objects.new('Conifer',template.data);active_collection.objects.link(obj)
        obj.location=(x,-z,y);obj.scale=(scale,scale,scale);obj['native_export']=True


def available(x,z):
    if not any(inside(x,z,p) for p in all_land):return False
    if any(a-.42<x<a+w+.42 and b-.42<z<b+d+.42 for a,b,w,d in building_footprints):return False
    return True

for iz in range(10):
    for ix in range(8):
        x=13.75+ix*.64+RNG.uniform(-.13,.13)
        z=5.15+iz*.65+RNG.uniform(-.13,.13)
        if not available(x,z) or math.hypot(x-12.5,z-6.5)<.75:continue
        if abs(x-16.5)<.75 and abs(z-9)<.85:continue
        if RNG.random()<.12:continue
        tree(x,z,RNG.uniform(.66,1.05))
for x,z,s in [(3.7,4.9,.7),(4.7,5.6,.88),(5.0,6.4,.72),(6.1,6.1,.85),(7.0,6.8,.85),
              (9.2,4.7,.76),(10.7,6.6,.8),(11.7,8.1,.86),(12.9,5.0,.72),(13.4,3.0,.72),
              (6.0,10.1,.76),(7.3,11.8,.9),(10.1,11.9,.76),(11.6,11.25,.86),(12.7,10.9,.9),
              (4.1,13.3,.8),(4.8,15.25,.8),(6.9,13.05,.75),(8.5,15.0,.78),(10.15,13.35,.83),
              (13.0,16.2,.72),(16.6,15.4,.75),(17.9,13.9,.84),(14.9,14.15,.6),(1.6,15.25,.72)]:
    if available(x,z):tree(x,z,s)


group('Flowers, shrubs, rocks and small boats')
FLOWERWHITE=material('Flowers — cream',color=(247,236,180,255))
FLOWERYELLOW=material('Flowers — yellow',color=(240,204,41,255))
FLOWERPINK=material('Flowers — pink',color=(226,133,139,255))
for i in range(65):
    x,z=RNG.uniform(2,17),RNG.uniform(5.4,16.0)
    if not available(x,z) or (x>13.5 and z<11.7) or (x<5.6 and 7<z<10):continue
    for j in range(3):
        px,pz=x+RNG.uniform(-.16,.16),z+RNG.uniform(-.15,.15)
        yy=height(px,pz)+.04;r=.045
        verts=[(px-r,yy,pz-r),(px+r,yy,pz-r),(px+r,yy,pz+r),(px-r,yy,pz+r)]
        mesh('Wildflowers',verts,[(0,1,2,3)],(FLOWERWHITE,FLOWERYELLOW,FLOWERPINK)[(i+j)%3],up=True)
for x,z,sx,sy,sz in [(0.0,7.0,.3,.5,.3),(.3,12.2,.25,.4,.24),(19.9,15.7,.38,.68,.4),
                     (18.6,17.5,.46,.85,.4),(6.2,3.4,.25,.32,.24),(10.5,3.0,.3,.5,.3),(18.9,9.7,.3,.4,.3)]:
    obj=rock('Sea rock',x,z,(sx,sy,sz));obj.location.z-=1.0
for x,z in [(4.0,15.5),(6.0,15.2),(10.8,12.1),(11.4,10.8)]:
    rock('Grass hummock',x,z,(.22,.16,.22),GRASS)

def boat(x,z,large=True):
    y=SEA+.08;w=.8 if large else .4;d=.3 if large else .2
    verts=[(x-w*.5,y,z),(x-w*.34,y,z-d/2),(x+w*.35,y,z-d/2),(x+w*.55,y,z),
           (x+w*.35,y,z+d/2),(x-w*.34,y,z+d/2)]
    mesh('Boat deck',verts,[tuple(range(6))],WOOD,up=True)
    cylinder('Boat mast',x,z,y,.015,.75 if large else .25,WOOD,n=4)
    if large:
        mesh('Boat cream sail',[(x,y+.7,z),(x-.02,y+.2,z),(x-.4,y+.2,z)],[(0,1,2)],TRIM)
        mesh('Boat red pennant',[(x,y+.7,z),(x+.27,y+.52,z+.025),(x,y+.48,z)],[(0,1,2)],REDMAT)
boat(19.5,17.1)
boat(.55,11.85,False)


# A preview cursor makes the authored concept composition reviewable. Native
# gameplay draws its own travelling character/marker, so this is not exported.
group('Preview cursor — runtime draws its own')
before=set(bpy.data.objects)
cx,cz=9.1,11.35;yy=height(cx,cz)+.46
for half,mat in ((0,REDMAT),(1,TRIM)):
    # Low-poly hemispheres around a horizontal black belt.
    n=10;verts=[(cx,yy+(.24 if half==0 else -.24),cz)]
    verts += [(cx+.23*math.cos(i*math.tau/n),yy,cz+.23*math.sin(i*math.tau/n)) for i in range(n)]
    mesh('Preview Pokeball',verts,[(0,i+1,(i+1)%n+1) for i in range(n)],mat)
cylinder('Preview Pokeball band',cx,cz,yy-.022,.235,.044,DARKMAT,n=10)
for o in set(bpy.data.objects)-before:o['native_export']=False


def interpolate(a,b,t):
    return tuple(x+(y-x)*t for x,y in zip(a,b))


def clip(vertices,axis,bound,keep_more):
    result=[]
    for a,b in zip(vertices,vertices[1:]+vertices[:1]):
        aa=a[3+axis]-bound;bb=b[3+axis]-bound
        ina=aa>=-.000001 if keep_more else aa<=.000001
        inb=bb>=-.000001 if keep_more else bb<=.000001
        if ina:result.append(a)
        if ina!=inb:result.append(interpolate(a,b,aa/(aa-bb)))
    return result


def native_export():
    native=Scene('overworld');count=0;surfaces=set()
    for obj in sorted(scene.objects,key=lambda o:o.name):
        if obj.type!='MESH' or not obj.get('native_export',False):continue
        data=obj.data;data.calc_loop_triangles()
        uvs=data.uv_layers.active.data;colors=data.color_attributes['Color'].data
        for tri in data.loop_triangles:
            mat=data.materials[tri.material_index]
            tex=mat.get('native_texture');tile=mat.get('native_tile',False)
            verts=[]
            for li in tri.loops:
                v=obj.matrix_world@data.vertices[data.loops[li].vertex_index].co
                uv=uvs[li].uv;c=colors[li].color
                verts.append((v.x,v.z,-v.y,uv.x,uv.y,*[srgb(x) for x in c[:3]],255))
            a,b,c=[Vector(p[:3]) for p in verts]
            center=(a+b+c)/3
            two_sided=any(word in obj.name.lower() for word in ('flag','sail','pennant','dish'))
            if not two_sided and (b-a).cross(c-a).dot(Vector((10,22,24.6))-center)<=0:continue
            # Native scene.c selects GL_REPEAT for batches with unwrapped
            # UVs. Keeping the original triangles avoids geometry subdivision
            # merely to emulate tiled textures through GL_CLAMP.
            polygons=[verts]
            for polygon in polygons:
                key=(0,0,tex,(0,0,0));vs,indices,cache=native.batches[key]
                for i in range(1,len(polygon)-1):
                    tri_vertices=[polygon[0],polygon[i],polygon[i+1]]
                    a,b,c=[Vector(p[:3]) for p in tri_vertices]
                    if (b-a).cross(c-a).length_squared<1e-13:continue
                    surface=tuple(sorted(tuple(round(v,5) for v in p[:3]) for p in tri_vertices))
                    assert surface not in surfaces,('Duplicate native island surface',obj.name)
                    surfaces.add(surface)
                    for p in tri_vertices:
                        vertex=tuple(round(x,7) for x in p[:5])+tuple(max(0,min(255,round(c))) for c in p[5:])
                        if vertex not in cache:cache[vertex]=len(vs);vs.append(vertex)
                        indices.append(cache[vertex])
                    count+=1
    native.props=landmarks
    temp=OUT/'maps/overworld-authoring.l64'
    record=native.save(temp)
    assert record['triangles']<7500,record
    doc=json.loads((OUT/'world.json').read_text())
    before_nodes=doc['overworld_nodes']
    doc['meshes']['overworld']=record
    doc['maps'][0]['camera']=dict(eye=[10,22,24.6],target=[10,.25,8.8],fov=38)
    doc['overworld_authoring']=dict(blend='overworld.blend',script='tools/author_overworld.py',
        concept='port/maps/prototypes/overworld_n64_concept_v1.png',sea_height=SEA,ground_height=.35)
    doc['texture_count']=len({t for r in doc['meshes'].values() for t in r['textures']})
    assert doc['overworld_nodes']==before_nodes
    from check_overworld_terrain import validate as validate_terrain
    terrain_check=validate_terrain()
    terrain_report=ROOT/'build/terrain-check.json';terrain_report.parent.mkdir(parents=True,exist_ok=True)
    terrain_report.write_text(json.dumps(terrain_check,indent=2)+'\n')
    assert terrain_check['passed'],terrain_check['failures'][:8]
    scene['terrain_surface_validation']=json.dumps(terrain_check['totals'])
    # Reject layered/crossing roof surfaces before publishing runtime assets.
    from check_overworld_roofs import validate as validate_roofs
    roof_check=validate_roofs(doc,.12)
    report=ROOT/'build/roof-check.json';report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps(roof_check,indent=2)+'\n')
    assert roof_check['passed'],roof_check['failures'][:8]
    scene['roof_shell_validation']=json.dumps(roof_check['totals'])
    os.replace(temp,OUT/'maps/overworld.l64');record['file']='maps/overworld.l64'
    write_world_metadata(OUT,doc)
    (OUT/'overworld_authoring.json').write_text(json.dumps(dict(record=record,camera=doc['maps'][0]['camera'],
        blend='overworld.blend',script='tools/author_overworld.py',native_uv_wrap='repeat when outside [0,1]',
        material_textures=dict(water=WATER['native_texture'],grass=GRASS['native_texture'],
            sand=SAND['native_texture'],cliff=CLIFF['native_texture']),
        fixed_camera_backface_optimization=True),indent=2)+'\n')
    return record


# Keep the authored Blender object hierarchy and repeated conifer meshes.
camera=bpy.data.cameras.new('Native island camera')
camera.lens_unit='FOV';camera.angle=2*math.atan(math.tan(math.radians(38)/2)*4/3)
cam=bpy.data.objects.new('Camera',camera);scene.collection.objects.link(cam)
cam.location=(10,-24.6,22)
target=Vector((10,-8.8,.25));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
scene.camera=cam
scene.render.engine='BLENDER_EEVEE'
scene.render.image_settings.file_format='PNG'
scene.render.resolution_x=960;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard'
scene.world.color=(.02,.04,.12)
scene['native_scene']='map 0; all original logical destinations and routes unchanged'
scene['visual_reference']='port/maps/prototypes/overworld_n64_concept_v1.png'
scene['material_rules']='Opaque RGBA16 <=4 KiB TMEM; actual UVs and baked vertex shade exported'
bpy.context.view_layer.update()
bake_ground_lighting()
record=native_export()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'overworld.blend'))
(OUT/'previews').mkdir(exist_ok=True)
scene.render.filepath=str(OUT/'previews/overworld-concept.png')
bpy.ops.render.render(write_still=True)
scene.render.resolution_x=320;scene.render.resolution_y=240
scene.render.filepath=str(OUT/'previews/overworld.png')
bpy.ops.render.render(write_still=True)
print('BLENDER OVERWORLD:',json.dumps(record))
