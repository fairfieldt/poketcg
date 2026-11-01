"""Sara and Amanda: shared rounded pool outfit, portrait-specific heads.

The overworld uses one sprite with blue/gold palettes. Its duel portraits make
an important distinction: Sara wears a brimless swimming cap and goggles;
Amanda has an uncovered side-parted bob. Only the tagged head is swapped for
that appearance. The torso and four rigid limbs retain identical geometry.

Coordinates here are the builder's authoring convention: X right, Y front,
Z up. Shared helpers apply the family scale and native coordinate conversion.
"""
import math


def _loft(ctx,name,profiles,materials,bands,part=0,pivot=(0,0,0),segments=8):
    from water_cast_common import mesh
    points=[(cx+rx*math.sin((i+.5)*math.tau/segments),
             cy+ry*math.cos((i+.5)*math.tau/segments),z)
            for z,rx,ry,cx,cy in profiles for i in range(segments)]
    faces=[tuple(range(segments))];indices=[bands[0]]
    for row in range(len(profiles)-1):
        for i in range(segments):
            a=row*segments+i;b=row*segments+(i+1)%segments
            faces.append((a+segments,b+segments,b,a));indices.append(bands[row])
    faces.append(tuple(reversed(range((len(profiles)-1)*segments,len(points)))))
    indices.append(bands[-1])
    return mesh(ctx,name,points,faces,materials,face_material_indices=indices,
                part=part,pivot=pivot)


def _body(ctx):
    accent,skin,white,ink=(ctx[key] for key in ('accent','skin','white','ink'))
    # A deep rib cage, soft shoulder slope and modest waist retain the source
    # sleeveless pool outfit without the old thin plank-shaped side profile.
    _loft(ctx,'pool outfit owning torso',[
        (.65,.30,.23,0,0),(.85,.31,.24,0,0),(1.02,.265,.22,0,0),
        (1.22,.335,.27,0,0),(1.37,.35,.245,0,0),(1.44,.275,.19,0,0),
        (1.47,.26,.19,0,0),(1.49,.135,.13,0,0),(1.57,.125,.125,0,0)],
        [accent,skin,white],[0,0,0,0,0,2,1,1])
    for side in (-1,1):
        joint=1 if side<0 else 2
        # One continuous surface owns shoe sole, shoe, bare leg and shorts.
        # No complete beige limb or trouser cap sits behind an exterior layer.
        _loft(ctx,'pool outfit left leg' if side<0 else 'pool outfit right leg',[
            (.018,.14,.22,side*.17,.075),(.070,.14,.23,side*.17,.075),
            (.13,.13,.23,side*.17,.075),(.19,.10,.12,side*.17,.015),
            (.38,.105,.12,side*.17,-.015),(.53,.13,.14,side*.17,0),
            (.57,.145,.16,side*.17,0),(.67,.14,.15,side*.17,0),
            (.77,.11,.10,side*.17,0)],
            [ink,white,skin,accent],[0,1,1,2,2,3,3,3],
            part=joint,pivot=(side*.17,0,.72))
        # Shoulder, elbow, wrist and rounded hand form a single flowing limb.
        # The quiet relaxed bend is readable from the side while preserving
        # the original shoulder pivot and walking animation rigid groups.
        _loft(ctx,'pool outfit left arm' if side<0 else 'pool outfit right arm',[
            (.73,.10,.115,side*.50,.05),(.81,.12,.13,side*.50,.05),
            (.89,.075,.09,side*.49,.03),(1.05,.10,.12,side*.46,0),
            (1.22,.11,.13,side*.415,-.015),(1.37,.105,.13,side*.355,-.02)],
            [skin],[0,0,0,0,0],part=3 if side<0 else 4,
            pivot=(side*.40,0,1.36))


def _goggles(ctx):
    from water_cast_common import mesh
    ink=ctx['ink']
    lens=ctx.get('_water_swimmer_lens')
    if lens is None:
        lens=ctx['material']('pool goggle lens',(.47,.73,.82))
        ctx['_water_swimmer_lens']=lens
    objects=[]
    # Small true extrusions sit above the swimming cap. The rim partitions
    # its front with the glass; there is no dark disk hidden behind that glass.
    for side in (-1,1):
        # These small rings need at least one actual packed step of rim.
        # Author their calm hexagonal outline on the native 1/32 grid, with
        # an inward-bin guard so float32 scale/multiply/trunc cannot collapse
        # the glass into the frame. This preserves the original silhouette.
        sx,sy,sz=ctx.get('scale',(.84,.85,.94))
        def position(x,y,z):
            def native(value):
                return (value+math.copysign(.125,value))/32 if value else 0
            return (native(x)/sx,native(z)/sy,(1.5*sz+native(y))/sz)
        cx=side*4;n=6
        outline=[(3,0),(1,2),(-1,2),(-3,0),(-1,-2),(1,-2)]
        glass=[(2,0),(1,1),(-1,1),(-2,0),(-1,-1),(1,-1)]
        outer=[position(cx+x,20+y,10) for x,y in outline]
        inner=[position(cx+x,20+y,10) for x,y in glass]
        back=[position(cx+x,20+y,8) for x,y in outline]
        verts=outer+inner+back
        faces=[];indices=[]
        for i in range(n):
            j=(i+1)%n
            faces.extend(((i,j,n+j,n+i),(i,2*n+i,2*n+j,j)))
            indices.extend((0,0))
        faces.extend((tuple(range(n,2*n)),tuple(reversed(range(2*n,3*n)))))
        indices.extend((1,0))
        faces=[tuple(reversed(face)) for face in faces]
        obj=mesh(ctx,'Sara raised goggle '+str(side),verts,faces,[ink,lens],
                 face_material_indices=indices,head_component=True)
        obj['water_swimmer_head']=True;objects.append(obj)
    # A short bridge reads as one pair of goggles at native resolution.
    from parts import box
    verts,faces=box((.09,.075,.032))
    verts=[(x,y+.32,z+2.16) for x,y,z in verts]
    obj=mesh(ctx,'Sara goggle bridge',verts,faces,[ink],head_component=True)
    obj['water_swimmer_head']=True;objects.append(obj)
    return objects


def apply_appearance(ctx,appearance):
    """Swap only head components, preserving every body mesh and rigid pivot."""
    from water_cast_common import remove_sources,head,face_material
    remove_sources(ctx,lambda obj:bool(obj.get('water_swimmer_head')))
    sara=appearance!='swimmer_palette2'
    # A fresh material is intentional: export pruning can renumber texture
    # slots between the canonical head and its appearance-specific replacement.
    face=face_material(ctx,style='sara' if sara else 'amanda',
                       eye_color=(.13,.26,.34) if sara else (.23,.29,.17),
                       eyebrows='gentle' if sara else 'sly',
                       smile=True if sara else 'smirk')
    obj=head(ctx,style='friendly',palette={'skin':ctx['skin'],'face':face,
                                          'hair':ctx['white'] if sara else ctx['accent']},
             width=.90,depth=.76,bottom=1.45,top=2.28,
             face_low=1.53,face_high=2.05,nose=.11,
             hair='cap' if sara else 'bob',sweep=0 if sara else .10)
    # Material regions own these forehead/nape surfaces. No hair wedge or
    # second scalp is layered over the closed head. Amanda's side locks
    # frame the cheeks; Sara's dark short nape stays below the white cap.
    segments=int(obj['water_head_segments'])
    side_offset=int(obj['water_head_side_offset'])
    side_rows=int(obj['water_head_side_rows'])
    if sara:
        obj.data.materials.append(ctx['ink'])
        obj.material_slots[-1].link='OBJECT';obj.material_slots[-1].material=ctx['ink']
    for polygon in obj.data.polygons:
        if not side_offset<=polygon.index<side_offset+side_rows*segments:continue
        row,angular=divmod(polygon.index-side_offset,segments)
        theta=-math.pi+(angular+.5)*math.tau/segments
        if sara and row<=1 and abs(theta)>math.pi*.5:
            polygon.material_index=3
        elif not sara and 1<=row<=3 and math.pi*.375<abs(theta)<=math.pi*.5:
            polygon.material_index=2
    obj['water_swimmer_head']=True
    obj['water_swimmer_appearance']='Sara swim cap' if sara else 'Amanda side-part bob'
    if sara:_goggles(ctx)
    return obj


def apply(ctx):
    """Replace the generic prototype once, then select Sara's canonical head."""
    from water_cast_common import remove_sources
    remove_sources(ctx,lambda obj:True)
    _body(ctx)
    apply_appearance(ctx,'swimmer')
