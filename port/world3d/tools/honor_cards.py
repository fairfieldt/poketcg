"""Map-only derivatives of the four original Hall card/aura sprite quadrants.

Keep canonical actor exports, runtime IDs, logical positions and visibility.
Local mesh offsets assemble the image-plane quadrants into one floating card.
"""
import math


def build(w,doc):
    source=w.Image(w.REPO/'src/gfx/overworld_sprites/legendary_card.png')
    # AnimFrameTable18's inner-corner tile4, mirrored into four quadrants.
    # OAM palette5: transparent, cream, purple, black (original RGB555 values).
    palette=((49,115,90,0),(247,222,198,255),(123,66,214,255),(0,0,0,255))
    pixels=[]
    for y in range(16):
        row=[]
        for x in range(16):
            ix=x if x<8 else 15-x;iy=y if y<8 else 15-y
            ci=(255-source.at(ix,16+iy)[0])//85
            row.append(palette[ci])
        pixels.append(row)
    nonzero=[(x,y) for y,row in enumerate(pixels) for x,p in enumerate(row) if p[3]]
    left=min(x for x,y in nonzero);right=max(x for x,y in nonzero)+1
    top=min(y for x,y in nonzero);bottom=max(y for x,y in nonzero)+1
    assert (right-left,bottom-top)==(10,14)
    cropped=[row[left:right] for row in pixels[top:bottom]]
    cropped=[[p if p[3] else palette[1] for p in row] for row in cropped]
    tex=w.texture([[cropped[y*14//16][x*10//16] for x in range(16)] for y in range(16)])
    hall=doc['maps'][33];entries=[]
    parts=((104,'top_left',True,True),(105,'top_right',False,True),
           (107,'bottom_left',True,False),(108,'bottom_right',False,False))
    for npc_id,name,is_left,is_top in parts:
        npc=next(n for n in hall['npcs'] if n['npc']==npc_id)
        model=doc['npc_models'][npc_id];s=w.Scene('hall_card_'+name)
        # Absolute visual coordinates, then convert to the unchanged source
        # actor's local origin (including the shared2.2-unit support height).
        x0,x1=(10.45,11) if is_left else (11,11.55)
        y0,y1=(3.25,4.02) if is_top else (2.48,3.25)
        omit=[3,2 if is_left else 4,0 if is_top else 5]
        s.box(x0,8.82,x1-x0,.08,y1-y0,(247,222,198,255),y=y0,omit=tuple(omit))
        points=[(x0,y0,8.90),(x1,y0,8.90),(x1,y1,8.90),(x0,y1,8.90)]
        uv=[((x-10.45)/1.10,1-(y-2.48)/1.54) for x,y,z in points]
        s.polygon(points,(255,255,255,255),tex,uv)
        # The restrained faceted pink halo is behind the card; each logical
        # quadrant owns its corresponding90 degrees, with exact shared edges.
        start=(1 if is_top and is_left else 0 if is_top else 2 if is_left else 3)*math.pi/2
        for i in range(4):
            a=start+i*math.pi/8;b=a+math.pi/8
            ring=[(11+r*math.cos(t),3.25+r*math.sin(t),8.77) for r,t in
                  ((.94,a),(.94,b),(.86,b),(.86,a))]
            s.polygon(ring,(238,137,173,255))
        # Change only the derivative's baked local vertices. Native and web
        # still apply the actual source pose, including visibility and heading.
        remapped={}
        for (_,part,t,pivot),(vertices,indices,_) in s.batches.items():
            local=[(v[0]-npc['x'],v[1]-2.2,v[2]-npc['y'],*v[3:]) for v in vertices]
            remapped[(model,part,t,pivot)]=(local,indices,{})
        s.batches=remapped
        record=s.save(w.MAPS/(s.name+'.l64'));doc['meshes'][s.name]=record
        entries.append(dict(map=33,npc=npc_id,model=model,scene='rom:/world/'+record['file']))
    doc['map_actor_scenes']=entries
    # Character sync keeps canonical actor records last. Match that stable
    # ordering so adding room-only derivatives does not cause a no-op rewrite.
    doc['meshes']={**{k:v for k,v in doc['meshes'].items() if not v.get('character')},
                   **{k:v for k,v in doc['meshes'].items() if v.get('character')}}
