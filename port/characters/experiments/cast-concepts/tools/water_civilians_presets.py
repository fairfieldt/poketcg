"""Save canonical Sara/Amanda head presets without cloning body meshes; add Amy pose control."""
import bpy,json,math,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
mode=sys.argv[sys.argv.index('--')+1]
if mode=='swimmer':
 base=ROOT/'models/swimmer/i3b/swimmer.blend';alt=ROOT/'models/swimmer_palette2/i3/swimmer_palette2.blend';bpy.ops.wm.open_mainfile(filepath=str(base))
 for obj in bpy.data.objects:
  if 'head' in obj.name:obj['head_preset']='sara';obj['selected_preset']=True
 with bpy.data.libraries.load(str(alt),link=False) as (src,dst):dst.objects=[x for x in src.objects if 'head' in x]
 presets=bpy.data.collections.new('Head presets');bpy.context.scene.collection.children.link(presets)
 for obj in dst.objects:
  presets.objects.link(obj);obj.name='Amanda uncovered blonde head';obj['head_preset']='amanda';obj['selected_preset']=False;obj.hide_render=True;obj.hide_viewport=True
 bpy.context.scene['head_presets']=json.dumps({'sara':'swim cap/goggles','amanda':'uncovered swept blonde bob','body':'same mesh datablocks and five rigid parts; only owning head changes'})
 bpy.context.scene['active_head_preset']='sara';bpy.ops.wm.save_as_mainfile(filepath=str(base))
elif mode=='amy':
 base=ROOT/'models/amy/i3/amy.blend';bpy.ops.wm.open_mainfile(filepath=str(base));root=bpy.data.objects.new('Amy pose control',None);bpy.context.scene.collection.objects.link(root)
 for obj in list(bpy.context.scene.objects):
  if obj!=root:obj.parent=root
 root['pose']='standing';root['lying_rotation_x']=math.pi/2;root['lying_location']=(0,.9,.35);root['instruction']='For lying: X rotation 90 degrees, location (0, .9, .35). Standing: zero rotation/location.'
 bpy.context.scene['derived_lying']='amy/poses/lying/scene.l64';bpy.ops.wm.save_as_mainfile(filepath=str(base))
else:raise ValueError(mode)
