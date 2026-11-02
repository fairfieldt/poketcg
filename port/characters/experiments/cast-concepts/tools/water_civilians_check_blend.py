import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];bpy.ops.wm.open_mainfile(filepath=str(ROOT/'models/swimmer/i3b/swimmer.blend'))
meshes=[x for x in bpy.context.scene.objects if x.type=='MESH'];heads=[x for x in meshes if x.get('head_preset')];body=[x for x in meshes if x not in heads];assert len(heads)==2 and len(body)==5
presets={x['head_preset']:x for x in heads};assert set(presets)=={'sara','amanda'};assert presets['amanda'].hide_render and presets['amanda'].hide_viewport;assert not presets['sara'].hide_render
assert len({x.data.name for x in body})==5;assert set(int(x['rigid_part']) for x in body)==set(range(5))
report=dict(swimmer=dict(head_presets=list(presets),shared_body_objects=len(body),unique_body_mesh_datablocks=len({x.data.name for x in body}),amanda_hidden_by_default=True,default='sara'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'models/amy/i3/amy.blend'));root=bpy.data.objects['Amy pose control'];meshes=[x for x in bpy.context.scene.objects if x.type=='MESH'];assert len(meshes)==6 and all(x.parent==root for x in meshes);assert root['pose']=='standing';assert tuple(root['lying_location'])==(0,.9,.35)
report['amy']=dict(source_mesh_objects=6,shared_pose_control=True,default='standing',lying_rotation_x=root['lying_rotation_x'],lying_location=list(root['lying_location']))
(ROOT/'review/water-civilians-blender-presets.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
