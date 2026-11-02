#!/usr/bin/env python3
"""Directly authored Mark concepts; later rounds encode observed capture revisions."""
import argparse, copy, json, math, struct, subprocess, sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
STUDY=HERE.parent
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'))
import builder as b
from geom import lathe, extrude, dump_json, write_l643, cross, ellipsoid, box, Lighting
from paint import Canvas, eye, INK, WARM

IDS=['a_compact','b_portrait','c_slender','d_angular']
BASE=['a_round','b_pokemon','c_zelda','d_angular']

class Figure(b.Figure):
    def head_radius_scale(self,t):
        return math.sqrt(max(0,1-t*t)) * (1-self.jaw*max(0,-t))
    def head(self):
        if self.round>=2:
            s=self.s;ft=s.eye_t+.36;fb=s.eye_t-.60
            self.face_top_z=self.head_z(ft);self.face_bottom_z=self.head_z(fb)
            self.face_half=s.face_faces*180/s.head_segs
            def uv(a,z,ri):
                a=b.wrap(a)
                return (max(0,min(1,(a+self.face_half)/(2*self.face_half))),max(0,min(1,(self.face_top_z-z)/(self.face_top_z-self.face_bottom_z))))
            head=lathe([self.head_ring(t) for t in (-1,fb,s.eye_t,ft,.76,1)],s.head_segs,self.skin,smooth=s.smooth,uv_fn=uv,name='unified head and cap')
            face=b.mat('face',(1,1,1),f'{self.prefix}_face')
            capmat=b.mat('cap',self.d.cap_rgb)
            def material(c,m):
                if c[2]>self.face_top_z:return capmat
                angle=abs(b.wrap(math.degrees(math.atan2(c[0],c[1]-s.head_center[0]))))
                if angle<self.face_half and c[2]>self.face_bottom_z:return face
                return m
            head.remap_material(material);self.add(head)
        else:
            super().head()
        # Nose belongs to the front head surface, never a detached spike.
        head=self.meshes[0]
        cy,cz=self.s.head_center
        ry=self.s.head_r[1]
        for i,(x,y,z) in enumerate(head.verts):
            t=(z-cz)/self.s.head_r[2]
            angle=abs(math.atan2(x,y-cy))
            y+=self.nose*max(0,1-angle/.50)*max(0,1-abs(t+.18)/.32)
            head.verts[i]=(x,y,z)
        hair=b.mat('hair',self.d.hair_rgb)
        head.remap_material(lambda c,m: hair if c[1]<cy-.035 and cz-.12<c[2]<self.face_top_z else m)

    def arms(self):
        if self.round==1:return super().arms()
        s=self.s;px,pz=s.arm_pivot;a=s.arm
        for side in (-1,1):
            x=side*px;cut=pz-a['len']*.42;bottom=pz-a['len']
            sleeve=b.mat('sleeve',self.d.sleeve_rgb)
            arm=lathe([(bottom,a['r_bot'],a['r_bot'],x,0),(cut,a['r_top'],a['r_top'],x,0),(pz+.04,a['r_top'],a['r_top'],x,0)],6,sleeve,smooth=s.smooth,name='coarse arm')
            arm.remap_material(lambda c,m:self.skin if c[2]<cut else m)
            hand=ellipsoid((x,.01,bottom-.045),(.105,.10,.115),6,2,self.skin,s.smooth,'mitten')
            arm.merge(hand);arm.rotate_y(-side*a['tilt'],about=(x,pz))
            self.add(arm,3 if side<0 else 4,(x,0,pz))

    def legs(self):
        if self.round==1:return super().legs()
        s=self.s;px,pz=s.leg_pivot;l=s.leg;L,W,H=l['shoe']
        for side in (-1,1):
            x=side*px
            leg=lathe([(.08,l['r_bot'],l['r_bot']*1.1,x,0),(pz+.09,l['r_top'],l['r_top']*1.1,x,0)],6,b.mat('trousers',self.d.trousers_rgb),smooth=s.smooth,name='coarse leg')
            shoe=box((x,.06,H/2),(W,L,H),b.mat('shoe',self.d.shoe_rgb),name='simple shoe')
            shoe.remap_material(lambda c,m:b.mat('sole',self.d.sole_rgb) if c[2]<.04 else m)
            leg.merge(shoe);self.add(leg,1 if side<0 else 2,(x,0,pz))


def paint(fig,concept,round):
    s=fig.s;c=Canvas()
    # A forehead fringe painted on the same skin shell, joined to temple hair.
    c.poly([(0,0),(32,0),(32,6),(27,4),(23,7),(18,4),(14,6),(8,3),(4,6),(0,5)] if round==1 else [(0,0),(32,0),(32,3),(26,2),(22,4),(17,2),(12,3),(6,1),(0,3)],(28,24,29))
    span=s.face_faces*360/s.head_segs
    center=16;off=22/span*32
    sizes=[(7,10,4,8),(8,11,5,9),(6,8,3,6),(7,8,4,6)]
    w,h,pw,ph=sizes[concept]
    cy=12 if round==1 else 15
    for side in (-1,1):
        cx=center+side*off
        eye(c,cx,cy,w,h,pw,ph,(33,43,66),True,shape='oval' if concept!=3 else 'slant')
        y=(6 if concept!=2 else 7) if round==1 else 8
        c.line([(cx-w/2,y),(cx+w/2-1,y)],INK,1)
    c.line([(14,24),(17,24)] if round==1 else [(13,23),(17,23),(18,22)],WARM if round==1 else (90,49,33),1)
    if round>=2:
        c.px(18,22,(90,49,33))
    fig.textures[f'{fig.prefix}_face']=c
    fig.textures[f'{fig.prefix}_torso']=b.paint_torso(s,fig.d,s.torso[-1][0]-s.torso[0][0])


def cap(fig,concept,round):
    s=fig.s;cy,cz=s.head_center;rx,ry,rz=s.head_r
    material=b.mat('cap',fig.d.cap_rgb)
    base=cz+rz*.36
    # Close-fitting crown tracks the head; no detached fringe wedges or ears.
    lift=[.015,.01,.0,.005][concept]
    if round>=2:lift-=.012
    rings=[(base,rx*.955,ry*.955,0,cy),(cz+rz*.69,rx*.76,ry*.76,0,cy),
           (cz+rz*.90,rx*.46,ry*.46,0,cy),(cz+rz+lift,0,0,0,cy)]
    if round==1:
        fig.add(lathe(rings,s.head_segs,material,smooth=s.smooth,name='close cap',cap_bottom=True))
    length=[.20,.17,.16,.18][concept]
    if round>=2:length-=.025
    width=rx*.58
    edge=ry*.72
    outline=[(-width,-edge),(-width*.90,-ry-length*.65),(-width*.45,-ry-length),
             (width*.45,-ry-length),(width*.90,-ry-length*.65),(width,-edge)]
    brim=extrude(outline,base-(.018 if round==1 else .04),base+(.018 if round==1 else .04),material,name='rear bill')
    fig.add(brim)


def config(concept,round):
    s=copy.deepcopy(b.STYLES[BASE[concept]])
    s.id=f'm{concept}{round}'
    s.nose='none';s.ear=None;s.head_shape='egg';s.head_segs=12 if concept<2 else 10 if concept==2 else 8
    s.limb_segs=6;s.torso_segs=8
    s.face_faces=3
    s.head_r=[(.40,.33,.40),(.365,.305,.415),(.305,.275,.355),(.37,.32,.385)][concept]
    s.head_center=[(0,1.72),(0,1.80),(0,1.87),(0,1.77)][concept]
    if concept==0:
        s.arm_pivot=(.355,1.24);s.arm['len']=.54
        s.torso=[(.66,.28,.20),(.91,.28,.20),(1.20,.32,.21),(1.34,.25,.18),(1.39,.11,.09)]
        s.leg['shoe']=(.37,.265,.15);s.leg['r_top']=.13;s.leg['r_bot']=.11
    elif concept==1:
        s.arm_pivot=(.34,1.33);s.arm['len']=.61
        s.torso=[(.72,.25,.18),(1.0,.26,.18),(1.31,.31,.20),(1.43,.23,.16),(1.47,.105,.09)]
        s.leg['r_top']=.125;s.leg['r_bot']=.105;s.leg['shoe']=(.36,.245,.14)
    elif concept==2:
        s.arm_pivot=(.305,1.40);s.arm['len']=.67
        s.torso=[(.79,.225,.16),(1.07,.22,.16),(1.40,.28,.18),(1.49,.21,.14),(1.53,.09,.08)]
        s.leg['r_top']=.11;s.leg['r_bot']=.09;s.leg['shoe']=(.32,.21,.12)
    else:
        s.arm_pivot=(.37,1.29)
        s.torso=[(.70,.27,.19),(.98,.28,.20),(1.29,.32,.21),(1.40,.23,.16),(1.43,.11,.09)]
        s.leg['r_top']=.135;s.leg['r_bot']=.115;s.leg['shoe']=(.37,.26,.15)
    if round>=2:
        # Native i1: sunken eyes and bulging rear head; brighten and balance depth.
        s.light=Lighting(.72,.28,b.LIGHT_DIR)
        s.head_r=(s.head_r[0],s.head_r[1]*.92,s.head_r[2])
    if round>=3:
        pass # Round 3 changes are authored only after reviewing round 2 captures.
    return s


def clean_batches(batches):
    removed=0
    for key,(vs,indices,cache) in batches.items():
        clean=[]
        for j in range(0,len(indices),3):
            tri=indices[j:j+3];a,c,d=[vs[i][:3] for i in tri]
            normal=cross(tuple(c[k]-a[k] for k in range(3)),tuple(d[k]-a[k] for k in range(3)))
            if sum(v*v for v in normal)<1e-16:removed+=1
            else:clean.extend(tri)
        indices[:]=clean
    return removed


def validate(path):
    data=path.read_bytes();magic,nb,nt=struct.unpack_from('>4sHH',data);assert magic==b'L643'
    pos=8+32*nt;parts=set();triangles=0;minimum=1
    for _ in range(nb):
        model,part,tex,_,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',data,pos);pos+=28
        assert model==1 and 0<=part<=4 and -1<=tex<nt
        parts.add(part);vs=[struct.unpack_from('>5f4B',data,pos+24*i) for i in range(nv)];pos+=24*nv
        assert all(math.isfinite(v) for vertex in vs for v in vertex[:5])
        idx=struct.unpack_from('>'+str(ni)+'H',data,pos);pos+=2*ni;pos+=(-pos)%4
        assert ni%3==0 and max(idx)<nv
        for k in range(0,ni,3):
            a,c,d=[vs[i][:3] for i in idx[k:k+3]]
            n=cross(tuple(c[i]-a[i] for i in range(3)),tuple(d[i]-a[i] for i in range(3)))
            area=sum(v*v for v in n)
            assert area>1e-16,(path,k,'degenerate packed triangle')
            minimum=min(minimum,area);triangles+=1
    assert pos==len(data) and parts==set(range(5)) and triangles<=900
    return dict(triangles=triangles,parts=sorted(parts),packed_degenerates=0,minimum_cross_squared=minimum)


def main():
    p=argparse.ArgumentParser();p.add_argument('round',type=int,choices=(1,2,3));a=p.parse_args()
    for concept,cid in enumerate(IDS):
        s=config(concept,a.round);d=copy.deepcopy(b.DESIGNS['mark'])
        fig=Figure(s,d);fig.round=a.round;fig.jaw=([.48,.62,.54,.65] if a.round==1 else [.22,.30,.27,.34])[concept];fig.nose=[.025,.035,.045,.03][concept]
        fig.head();fig.torso(s.torso,f'{fig.prefix}_torso');fig.arms();fig.legs();cap(fig,concept,a.round);b.backpack(fig);fig.shadow();paint(fig,concept,a.round)
        batches,textures=fig.finish();removed=clean_batches(batches)
        out=STUDY/'work/exports'/f'{cid}_i{a.round}';(out/'textures').mkdir(parents=True,exist_ok=False)
        stats=write_l643(out/'scene.l64',batches,textures)
        for name in textures:fig.textures[name].save(out/'textures'/f'{name}.png')
        dump_json(out/'authoring.json',fig.meshes,dict(names=textures))
        validation=validate(out/'scene.l64');validation['removed_export_degenerates']=removed
        (out/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
        (out/'scene.json').write_text(json.dumps(dict(format='L643',model='mark',parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
        (out/'design.json').write_text(json.dumps(dict(concept=cid,round=a.round,head_r=s.head_r,jaw=fig.jaw,nose=fig.nose,method='direct 3D authoring; no generated raster art'),indent=2)+'\n')
        changes={1:'Authored distinct head/body proportions, tapered jaw, integrated nose and painted fringe; close backward cap with centered rear bill.',2:'Round 1 native review/audit: unified cap/head shell removes crossing seam; coarser head/limb topology, thicker rear bill and simple shoes avoid packed collapse; lower eye row, shorter fringe, clearer mouth and brighter face balance pointed jaw.',3:'Pending round 2 capture review.'}[a.round]
        subprocess.run([sys.executable,str(HERE/'retain_iteration.py'),cid,str(a.round),'--mark',str(out),'--direct-3d','--changes',changes],check=True)
        print(cid,a.round,validation,flush=True)

if __name__=='__main__':main()
