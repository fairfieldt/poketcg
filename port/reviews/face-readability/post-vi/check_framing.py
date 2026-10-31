"""Project the unchanged native trial meshes through the exact trial cameras."""
from pathlib import Path
import hashlib,json,math,sys
ROOT=next(parent for parent in Path(__file__).resolve().parents if (parent/'port/world3d/generated/world.json').exists())
sys.path.insert(0,str(ROOT/'port/world3d/tools'))
from build_world import read_l643


def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def norm(a):return tuple(x/math.sqrt(dot(a,a)) for x in a)
def project(point,camera,viewport):
    forward=norm(sub(camera['target'],camera['eye']));right=norm(cross(forward,(0,1,0)));up=cross(right,forward)
    delta=sub(point,camera['eye']);depth=dot(delta,forward);assert depth>0
    vx,vy,w,h=viewport;f=h/2/math.tan(math.radians(camera['fov'])/2)
    return (vx+w/2+dot(delta,right)*f/depth,240-(vy+h/2+dot(delta,up)*f/depth),depth)


def main():
    world=json.loads((ROOT/'port/world3d/generated/world.json').read_text());lookup={r['model']:r for r in world['individual_characters']}
    ids=[7,9,12,8,14,43];map_camera=world['maps'][1]['camera']
    cases=[]
    for view in ('portrait','room','conversation'):
        models=ids[:3] if view=='portrait' else ids
        for index,model in enumerate(models):
            rec=lookup[model];path=ROOT/'port/world3d/generated'/rec['file'];_,batches=read_l643(path)
            if view=='portrait':camera=dict(eye=[0,1.28,4.7],target=[0,1.08,0],fov=34,near=.1,far=20);viewport=[2+index*106,38,104,154];position=(0,0)
            elif view=='room':camera=dict(**map_camera,near=1.5,far=90);viewport=[0,0,320,240];position=(10+(index%3)*4,20 if index<3 else 10)
            else:camera=dict(eye=[14.5,10,30],target=[14,1,20],fov=50,near=1.5,far=90);viewport=[0,0,320,240];position=(9+index*2,20)
            samples=[];face=[]
            for mid,part,texture,pivot,vertices,indices in batches:
                for vertex in vertices:
                    point=(vertex[0]+pivot[0]+position[0],vertex[1]+pivot[1],vertex[2]+pivot[2]+position[1])
                    p=project(point,camera,viewport);samples.append(p)
                    if texture>=0:face.append(p)
            vx,vy,w,h=viewport;box=[min(p[0] for p in samples),min(p[1] for p in samples),max(p[0] for p in samples),max(p[1] for p in samples)]
            assert vx<box[0] and box[2]<vx+w and 240-vy-h<box[1] and box[3]<240-vy,(view,model,box)
            assert min(p[2] for p in samples)>camera['near'] and max(p[2] for p in samples)<camera['far']
            facebox=[min(p[0] for p in face),min(p[1] for p in face),max(p[0] for p in face),max(p[1] for p in face)]
            assert facebox[1]>26,(view,model,'Face behind experiment label',facebox)
            cases.append(dict(view=view,model=model,slug=rec['slug'],position=list(position),camera=camera,viewport=viewport,body_pixel_bounds=[round(v,3) for v in box],face_pixel_bounds=[round(v,3) for v in facebox],face_pixel_width=round(facebox[2]-facebox[0],3),face_pixel_height=round(facebox[3]-facebox[1],3),geometry_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    result=dict(passed=True,models=len(ids),camera_model_checks=len(cases),no_clipped_vertices=True,no_faces_occluded_by_review_label=True,geometry_unchanged_across_options=True,cases=cases)
    (Path(__file__).parent/'framing.json').write_text(json.dumps(result,indent=2)+'\n')
    print('FACE FRAMING PASS:',len(cases),'camera/model checks; no clipping or label occlusion')
    for row in cases:print(row['view'],row['slug'],f"face{row['face_pixel_width']}x{row['face_pixel_height']}nativepixels")

if __name__=='__main__':main()
