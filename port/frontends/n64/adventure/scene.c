#include <libdragon.h>
#include <GL/gl.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "scene.h"

typedef struct {
    float position[3], uv[2];
    uint8_t color[4];
} Vertex;
_Static_assert(sizeof(Vertex) == 24, "L643 vertex layout");
typedef struct {
    uint16_t model, part;
    int16_t texture;
    uint16_t reserved;
    uint32_t vertices, indices;
    float pivot[3];
} MeshHeader;
_Static_assert(sizeof(MeshHeader) == 28, "L643 mesh layout");
typedef struct PackedMesh PackedMesh;
typedef struct { MeshHeader h; GLuint list; PackedMesh *packed; } Mesh;

struct Scene {
 Mesh *meshes;
 unsigned mesh_count, texture_count;
 GLuint room_list;
 sprite_t **sprites;
 GLuint *textures;
 rspq_block_t *packed_room;
};
#define meshes (scene->meshes)
#define mesh_count (scene->mesh_count)
#define room_list (scene->room_list)
#define sprites (scene->sprites)
#define textures (scene->textures)
static void (*load_idle)(void);
void scene_set_load_idle(void (*callback)(void)){load_idle=callback;}
static void scene_load_idle(void){if(load_idle)load_idle();}

#if SCENE_T3D
#include "scene_packed.inc"
#endif

void scene_camera(float fov,float aspect,float near,float far,const float eye[3],const float target[3],int x,int y,int width,int height)
{
#if SCENE_T3D
    packed_init();
    assertf(!packed_camera_active,"End the prior packed camera frame before replacing DMA matrices");
    /* Call once per frame after acquiring a safe display surface. Every
       existing presentation ends with detach_wait, so prior DMA is complete. */
    packed_matrix_count=0;
    packed_camera_active=true;
    t3d_viewport_set_area(&packed_viewport,x,y,width,height);
    t3d_viewport_set_perspective(&packed_viewport,fov*.01745329252f,aspect,near,far);
    T3DVec3 e={{eye[0],eye[1],eye[2]}},t={{target[0],target[1],target[2]}},up={{0,1,0}};
    t3d_viewport_look_at(&packed_viewport,&e,&t,&up);t3d_viewport_attach(&packed_viewport);
#else
    (void)fov;(void)aspect;(void)near;(void)far;(void)eye;(void)target;(void)x;(void)y;(void)width;(void)height;
#endif
}

void scene_frame_end(void)
{
#if SCENE_T3D
    if(packed_camera_active)t3d_tri_sync();
    packed_camera_active=false;
    /* Native viewports set the physical RDP scissor. Restore the full UI area
       before a portrait frame composites its keyboard or dialogue canvas. */
    rdpq_set_scissor(0,0,display_get_width(),display_get_height());
#endif
}

void scene_begin_opaque(void)
{
    /* Normal opaque surfaces need the RDP's opaque Z mode. The special
       interpenetrating mode changes coverage at near intersections and can
       mix rear surfaces into a character. GL_LESS also lets libdragon choose
       transparent Z mode while the contact-shadow blend pass is enabled. */
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);glBlendFunc(GL_ONE,GL_ZERO);
    glDisable(GL_ALPHA_TEST);glDisable(GL_FOG);
    glDisable(GL_LIGHTING);glDisable(GL_CULL_FACE);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glColor4ub(255,255,255,255);
}

Scene *scene_open(const char *path, const char *texture_root)
{
    Scene *scene=calloc(1,sizeof(*scene));
    assert(scene);
    FILE *f = fopen(path, "rb");
    assertf(f, "scene.l64 missing; run make assets");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    assertf(size >= 8 && size < 2*1024*1024, "Invalid scene size");
    uint16_t header[4];
    assertf(fread(header,1,sizeof(header),f)==sizeof(header),"Scene header read failed");
    assertf(!memcmp(header,"L643",4),"Unsupported scene format");
    mesh_count = header[2];
    unsigned texture_count = header[3];
    assert(mesh_count <= 1024 && texture_count <= 512);
    scene->texture_count=texture_count;
    textures = calloc(texture_count, sizeof(*textures));
    sprites = calloc(texture_count, sizeof(*sprites));
    meshes = calloc(mesh_count, sizeof(*meshes));
    assert((!texture_count || (textures && sprites)) && meshes);
    char (*names)[32]=malloc(texture_count*32);
    bool repeat[512]={0};
    assert(!texture_count||names);
    assertf(fread(names,32,texture_count,f)==texture_count,"Scene texture table read failed");
    long batches_start=ftell(f);
    /* libdragon fixes wrap modes when a sprite image is attached. Determine
       per-texture wrapping before creating the GL texture objects. */
    for(unsigned i=0;i<mesh_count;i++) {
        MeshHeader h;
        assertf(fread(&h,1,sizeof(h),f)==sizeof(h),"Scene scan header read failed");
        assert(h.vertices&&h.vertices<=65535&&h.indices<=1024*1024);
        assert(h.texture>=-1&&h.texture<(int)texture_count);
        size_t vertex_bytes=h.vertices*sizeof(Vertex);
        assertf(vertex_bytes<=(size_t)(size-ftell(f)),"Scene scan exceeds file");
        Vertex *vertices=malloc(vertex_bytes);assert(vertices);
        assertf(fread(vertices,1,vertex_bytes,f)==vertex_bytes,"Scene scan read failed");
        if(h.texture>=0)for(unsigned v=0;v<h.vertices;v++)for(unsigned c=0;c<2;c++)
            if(vertices[v].uv[c]<-.0001f||vertices[v].uv[c]>1.0001f)repeat[h.texture]=true;
        free(vertices);
        fseek(f,(h.indices*sizeof(uint16_t)+3)&~(size_t)3,SEEK_CUR);
        assertf(ftell(f)<=size,"Scene scan indices exceed file");
        scene_load_idle();
    }
    fseek(f,batches_start,SEEK_SET);
    glGenTextures(texture_count, textures);
    for (unsigned i = 0; i < texture_count; i++) {
        char texture_path[128];
        assertf(memchr(names[i],0,32),"Invalid scene texture name");
        snprintf(texture_path,sizeof(texture_path),"%s%s",texture_root,names[i]);
        sprites[i] = sprite_load(texture_path);
        assert(sprites[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat[i]?GL_REPEAT:GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat[i]?GL_REPEAT:GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], NULL);
        scene_load_idle();
    }
    free(names);
    for (unsigned i = 0; i < mesh_count; i++) {
        Mesh *mesh = &meshes[i];
        assertf(fread(&mesh->h,1,sizeof(MeshHeader),f)==sizeof(MeshHeader),"Scene batch header read failed");
        MeshHeader *h = &mesh->h;
        assert(h->vertices > 0 && h->vertices <= 65535 && h->indices % 3 == 0);
        assert(h->texture >= -1 && h->texture < (int)texture_count);
        assert(h->indices<=1024*1024);
        size_t bytes=h->vertices*sizeof(Vertex)+h->indices*sizeof(uint16_t);
        bytes=(bytes+3)&~(size_t)3;
        assertf(bytes<=(size_t)(size-ftell(f)),"Scene batch exceeds file");
        uint8_t *batch=malloc(bytes);assertf(batch,"Scene batch allocation failed");
        assertf(fread(batch,1,bytes,f)==bytes,"Scene batch read failed");
        const Vertex *vertices=(const Vertex *)batch;
        const uint16_t *indices=(const uint16_t *)(batch+h->vertices*sizeof(Vertex));
        for(unsigned index=0;index<h->indices;index++)
            assertf(indices[index]<h->vertices,"Scene vertex index exceeds its batch");
#if SCENE_T3D
        mesh->packed=packed_create(vertices,indices,h->indices,h->texture>=0?sprites[h->texture]:NULL,h->texture>=0&&repeat[h->texture]);
#else
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), vertices->position);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), vertices->color);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), vertices->uv);
        mesh->list = glGenLists(1);
        glNewList(mesh->list, GL_COMPILE);
        if (h->texture >= 0) {
            glEnable(GL_TEXTURE_2D);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glBindTexture(GL_TEXTURE_2D, textures[h->texture]);
        } else {
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glDrawElements(GL_TRIANGLES, h->indices, GL_UNSIGNED_SHORT, indices);
        glEndList();
#endif
        /* libdragon's list compiler copies each vertex attribute into its RSP
           command stream. The source arrays need not stay resident. Streaming
           one batch at a time also avoids a full-file loading peak. */
        free(batch);
        scene_load_idle();
    }
    assertf(ftell(f)==size,"Scene trailing data");fclose(f);
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(Vertex),NULL);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),NULL);
    glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),NULL);
    /* Batches are material sorted offline. Nested lists cache the room's state
       changes and RSP transforms rather than resubmitting arrays every frame. */
#if SCENE_T3D
    rspq_block_begin();
    for(unsigned i=0;i<mesh_count;i++)if(!meshes[i].h.model)rspq_block_run(meshes[i].packed->list);
    scene->packed_room=rspq_block_end();
#else
    room_list = glGenLists(1);
    glNewList(room_list, GL_COMPILE);
    for (unsigned i = 0; i < mesh_count; i++)
        if (!meshes[i].h.model) glCallList(meshes[i].list);
    glEndList();
#endif
    debugf("Scene %s: %u batches, %u textures, %ld bytes\n",path,mesh_count,texture_count,size);
    return scene;
}

void scene_draw_room(const Scene *scene)
{
    if(!scene)return;
#if SCENE_T3D
    T3DMat4 identity;t3d_mat4_identity(&identity);
    rdpq_mode_push();t3d_frame_start();rdpq_mode_antialias(AA_NONE);
    packed_matrix(&identity);rspq_block_run(scene->packed_room);t3d_matrix_pop(1);t3d_tri_sync();rdpq_mode_pop();
#else
    glCallList(room_list);
#endif
}

void scene_draw_contact_shadows(const float (*positions)[2],unsigned count)
{
    if(!count)return;
    static const float ring[13][2]={
        {1,0},{.8660254f,.5f},{.5f,.8660254f},{0,1},
        {-.5f,.8660254f},{-.8660254f,.5f},{-1,0},
        {-.8660254f,-.5f},{-.5f,-.8660254f},{0,-1},
        {.5f,-.8660254f},{.8660254f,-.5f},{1,0}
    };
    /* A transparent rim keeps this small at native resolution. The fan sits
       on the floor, independent of actor bob, and furniture still occludes it.
       libdragon has no polygon offset or attribute stack. Restore explicitly. */
    glDisable(GL_TEXTURE_2D);glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(unsigned i=0;i<count;i++) {
        float x=positions[i][0],z=positions[i][1];
        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(16,22,32,64);glVertex3f(x,.045f,z);
        glColor4ub(16,22,32,0);
        for(unsigned j=0;j<13;j++)
            glVertex3f(x+ring[j][0]*.56f,.045f,z+ring[j][1]*.40f);
        glEnd();
    }
    scene_begin_opaque();
}

void scene_close(Scene *scene)
{
 if(!scene) return;
 rspq_wait();
#if SCENE_T3D
 rspq_block_free(scene->packed_room);
 for(unsigned i=0;i<mesh_count;i++)packed_close(meshes[i].packed);
#else
 glDeleteLists(room_list,1);
 for(unsigned i=0;i<mesh_count;i++)glDeleteLists(meshes[i].list,1);
#endif
 glDeleteTextures(scene->texture_count,textures);
 for(unsigned i=0;i<scene->texture_count;i++) sprite_free(sprites[i]);
 free(meshes); free(sprites); free(textures); free(scene);
}

void scene_draw_actor_tilt(const Scene *scene,unsigned model, float x, float z, float heading, float gait,float tilt)
{
#if SCENE_T3D
    T3DMat4 identity;t3d_mat4_identity(&identity);packed_draw_actor(scene,model,&identity,x,z,heading,gait,tilt);
#else
    glPushMatrix();
    glTranslatef(x, tilt?1.0f:0, z);
    glRotatef(heading, 0, 1, 0);
    if(tilt)glRotatef(tilt,1,0,0);
    for (unsigned i = 0; i < mesh_count; i++) {
        const Mesh *m = &meshes[i];
        if (m->h.model != model) continue;
        glPushMatrix();
        glTranslatef(m->h.pivot[0], m->h.pivot[1], m->h.pivot[2]);
        unsigned part = m->h.part;
        if (part) {
            float swing = sinf(gait)*24;
            if (part == 2 || part == 3) swing = -swing;
            glRotatef(swing, 1, 0, 0);
        }
        glCallList(m->list);
        glPopMatrix();
    }
    glPopMatrix();
#endif
}

void scene_draw_actor(const Scene *scene,unsigned model,float x,float z,float heading,float gait)
{scene_draw_actor_tilt(scene,model,x,z,heading,gait,0);}

void scene_draw_actor_presented(const Scene *scene,unsigned model,float x,float y,float z,float heading,float scale,float lean,float gait,float tilt)
{
#if SCENE_T3D
    T3DMat4 outer;t3d_mat4_identity(&outer);packed_translate(&outer,x,y,z);packed_rotate(&outer,heading,1);
    t3d_mat4_scale(&outer,scale,scale,scale);if(lean)packed_rotate(&outer,lean,0);
    packed_draw_actor(scene,model,&outer,0,0,0,gait,tilt);
#else
    glPushMatrix();glTranslatef(x,y,z);glRotatef(heading,0,1,0);glScalef(scale,scale,scale);
    if(lean)glRotatef(lean,1,0,0);
    scene_draw_actor_tilt(scene,model,0,0,0,gait,tilt);glPopMatrix();
#endif
}
