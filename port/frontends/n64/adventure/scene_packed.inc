/* Optional Tiny3D adapter for the existing L643 meshes. Included by scene.c so
 * one loader owns the source layout. No model reauthoring/decimation occurs. */
#include <t3d/t3d.h>

#define PACKED_POSITION_SCALE 32.0f
#define PACKED_MATRIX_LIMIT 160
typedef struct PackedChunk {
    T3DVertPacked *vertices;
    struct PackedChunk *next;
} PackedChunk;
struct PackedMesh { rspq_block_t *list; PackedChunk *chunks; };
static bool packed_ready;
static bool packed_camera_active;
static T3DViewport packed_viewport;
static T3DMat4FP *packed_matrices;
static unsigned packed_matrix_count;

static void packed_init(void)
{
    if(packed_ready)return;
    t3d_init((T3DInitParams){.matrixStackSize=8});
    packed_viewport=t3d_viewport_create_buffered(1);
    packed_matrices=malloc_uncached(PACKED_MATRIX_LIMIT*sizeof(*packed_matrices));
    assert(packed_matrices);packed_ready=true;
}

static int16_t packed_position(float value)
{
    /* Match libGL's 5-bit source position quantization. */
    float scaled=truncf(value*PACKED_POSITION_SCALE);
    assertf(scaled>=-32768&&scaled<=32767,"Packed position overflow");
    return (int16_t)scaled;
}

static void packed_vertex(T3DVertPacked *pair,unsigned odd,const Vertex *source,unsigned width,unsigned height)
{
    int16_t *pos=odd?pair->posB:pair->posA,*st=odd?pair->stB:pair->stA;
    for(unsigned c=0;c<3;c++)pos[c]=packed_position(source->position[c]);
    uint32_t color=(uint32_t)source->color[0]<<24|(uint32_t)source->color[1]<<16|
        (uint32_t)source->color[2]<<8|source->color[3];
    if(odd){pair->rgbaB=color;pair->normB=0;}else{pair->rgbaA=color;pair->normA=0;}
    st[0]=(int16_t)lrintf(source->uv[0]*width*32.0f);
    st[1]=(int16_t)lrintf(source->uv[1]*height*32.0f);
}

static void packed_chunk(PackedMesh *mesh,const Vertex *vertices,const uint16_t *sources,unsigned count,const uint8_t *indices,unsigned length,unsigned width,unsigned height)
{
    if(!length)return;
    PackedChunk *chunk=calloc(1,sizeof(*chunk));assert(chunk);
    unsigned pairs=(count+1)/2;
    chunk->vertices=malloc_uncached(pairs*sizeof(*chunk->vertices));assert(chunk->vertices);
    memset(chunk->vertices,0,pairs*sizeof(*chunk->vertices));
    chunk->next=mesh->chunks;mesh->chunks=chunk;
    for(unsigned i=0;i<count;i++)packed_vertex(&chunk->vertices[i/2],i&1,&vertices[sources[i]],width,height);
    t3d_vert_load(chunk->vertices,0,count);
    for(unsigned i=0;i<length;i+=3)t3d_tri_draw(indices[i],indices[i+1],indices[i+2]);
    t3d_tri_sync();
}

static PackedMesh *packed_create(const Vertex *vertices,const uint16_t *indices,unsigned length,sprite_t *sprite,bool repeat)
{
    packed_init();PackedMesh *mesh=calloc(1,sizeof(*mesh));assert(mesh);
    uint8_t *mapped=malloc(length);assert(mapped);
    uint16_t sources[T3D_VERTEX_CACHE_SIZE];unsigned count=0,used=0;
    unsigned width=sprite?sprite->width:1,height=sprite?sprite->height:1;
    rspq_block_begin();
    t3d_state_set_drawflags(T3D_FLAG_DEPTH|T3D_FLAG_SHADED|T3D_FLAG_NO_LIGHT|(sprite?T3D_FLAG_TEXTURED:0));
    rdpq_mode_combiner(sprite?RDPQ_COMBINER_TEX_SHADE:RDPQ_COMBINER_SHADE);
    if(sprite)rdpq_sprite_upload(TILE0,sprite,&(rdpq_texparms_t){
        .s.repeats=repeat?REPEAT_INFINITE:1,.t.repeats=repeat?REPEAT_INFINITE:1});
    for(unsigned at=0;at<length;at+=3) {
        unsigned missing=0;
        for(unsigned c=0;c<3;c++) {
            bool found=false;
            for(unsigned v=0;v<count;v++)if(sources[v]==indices[at+c]){found=true;break;}
            if(!found)missing++;
        }
        if(count+missing>T3D_VERTEX_CACHE_SIZE) {
            packed_chunk(mesh,vertices,sources,count,mapped,used,width,height);count=used=0;
        }
        for(unsigned c=0;c<3;c++) {
            unsigned v;
            for(v=0;v<count;v++)if(sources[v]==indices[at+c])break;
            if(v==count)sources[count++]=indices[at+c];
            mapped[used++]=(uint8_t)v;
        }
    }
    packed_chunk(mesh,vertices,sources,count,mapped,used,width,height);
    mesh->list=rspq_block_end();free(mapped);return mesh;
}

static void packed_close(PackedMesh *mesh)
{
    if(!mesh)return;
    rspq_block_free(mesh->list);
    for(PackedChunk *c=mesh->chunks,*next;c;c=next){next=c->next;free_uncached(c->vertices);free(c);}
    free(mesh);
}

static void packed_mul(T3DMat4 *left,const T3DMat4 *right)
{T3DMat4 result;t3d_mat4_mul(&result,left,right);*left=result;}
static void packed_translate(T3DMat4 *matrix,float x,float y,float z)
{
    T3DMat4 t;t3d_mat4_identity(&t);t.m[3][0]=x;t.m[3][1]=y;t.m[3][2]=z;packed_mul(matrix,&t);
}
static void packed_rotate(T3DMat4 *matrix,float degrees,unsigned axis)
{
    float angle=degrees*.01745329252f,c=cosf(angle),s=sinf(angle);
    T3DMat4 r;t3d_mat4_identity(&r);
    unsigned a=(axis+1)%3,b=(axis+2)%3;
    r.m[a][a]=c;r.m[b][b]=c;r.m[a][b]=s;r.m[b][a]=-s;packed_mul(matrix,&r);
}
static void packed_matrix(const T3DMat4 *model)
{
    assertf(packed_camera_active,"Packed scene needs scene_camera for this frame");
    assertf(packed_matrix_count<PACKED_MATRIX_LIMIT,"Packed frame matrix limit");
    T3DMat4 scaled=*model;t3d_mat4_scale(&scaled,1/PACKED_POSITION_SCALE,1/PACKED_POSITION_SCALE,1/PACKED_POSITION_SCALE);
    T3DMat4FP *out=&packed_matrices[packed_matrix_count++];t3d_mat4_to_fixed_3x4(out,&scaled);
    t3d_matrix_push(out);
}

static void packed_draw_actor(const Scene *scene,unsigned model,const T3DMat4 *outer,float x,float z,float heading,float gait,float tilt)
{
    T3DMat4 actor=*outer;
    packed_translate(&actor,x,tilt?1:0,z);packed_rotate(&actor,heading,1);
    if(tilt)packed_rotate(&actor,tilt,0);
    rdpq_mode_push();t3d_frame_start();rdpq_mode_antialias(AA_NONE);
    for(unsigned i=0;i<mesh_count;i++) {
        const Mesh *m=&meshes[i];if(m->h.model!=model)continue;
        T3DMat4 part=actor;packed_translate(&part,m->h.pivot[0],m->h.pivot[1],m->h.pivot[2]);
        if(m->h.part)packed_rotate(&part,sinf(gait)*24*((m->h.part==2||m->h.part==3)?-1:1),0);
        packed_matrix(&part);rspq_block_run(m->packed->list);t3d_matrix_pop(1);
    }
    t3d_tri_sync();rdpq_mode_pop();
}
