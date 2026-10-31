"""Create the linked shared-parts library once. Run in Blender background mode."""
import bpy
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from parts import definitions

ROOT=Path(__file__).resolve().parents[1]
if 'fast64' not in bpy.context.preferences.addons:
    bpy.ops.preferences.addon_enable(module='fast64')
from fast64.fast64_internal.f3d.f3d_material import createF3DMat
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.gameEditorMode='Homebrew'
bpy.context.scene.f3d_type='F3DEX2/LX2'
placeholder=bpy.data.materials.new('Shared slot · object overrides material')
for name,(verts,faces) in definitions().items():
    mesh=bpy.data.meshes.new('part_'+name)
    mesh.from_pydata([(x,-y,z) for x,y,z in verts],[],[tuple(reversed(f)) for f in faces])
    mesh.materials.append(placeholder)
    uv=mesh.uv_layers.new()
    for poly in mesh.polygons:
        for j,li in enumerate(poly.loop_indices):
            if name=='face':
                vi=mesh.loops[li].vertex_index
                uv.data[li].uv=((verts[vi][0]+.31)/.62,vi//4)
            else:
                uv.data[li].uv=((0,0),(1,0),(1,1),(0,1))[j%4]
    obj=bpy.data.objects.new(name,mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.hide_render=True
solid=createF3DMat(None,preset='Shaded Solid');solid.name='Character Solid Template';solid.use_fake_user=True
tex=createF3DMat(None,preset='Shaded Texture');tex.name='Character Texture Template';tex.use_fake_user=True
# Embed Fast64 shader graphs so models only depend on this checked-in parts file.
for group in list(bpy.data.node_groups):
    if group.library:group=group.make_local()
    group.use_fake_user=True
for mat in list(bpy.data.materials):
    if mat.library:mat.make_local()
bpy.context.scene['parts_version']=1
(ROOT/'assets').mkdir(exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/parts.blend'))
print('SHARED_PARTS',len(definitions()))
