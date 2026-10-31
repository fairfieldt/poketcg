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
typedef struct { MeshHeader h; GLuint list; } Mesh;

static Mesh *meshes;
static unsigned mesh_count;
static GLuint room_list;
/* Texture storage and client arrays must outlive GL display lists / RSP DMA. */
static sprite_t **sprites;
static GLuint *textures;
static void *mesh_storage;

void scene_load(void)
{
    FILE *f = fopen("rom:/scene.l64", "rb");
    assertf(f, "scene.l64 missing; run make assets");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    assertf(size >= 8 && size < 2*1024*1024, "Invalid scene size");
    mesh_storage = malloc((size_t)size);
    assert(mesh_storage);
    assertf(fread(mesh_storage, 1, size, f) == (size_t)size, "Scene read failed");
    fclose(f);
    uint8_t *p = mesh_storage, *end = p+size;
    assertf(!memcmp(p, "L643", 4), "Unsupported scene format");
    mesh_count = ((uint16_t *)p)[2];
    unsigned texture_count = ((uint16_t *)p)[3];
    assert(mesh_count <= 256 && texture_count <= 128);
    p += 8;
    textures = calloc(texture_count, sizeof(*textures));
    sprites = calloc(texture_count, sizeof(*sprites));
    meshes = calloc(mesh_count, sizeof(*meshes));
    assert(textures && sprites && meshes);
    glGenTextures(texture_count, textures);
    for (unsigned i = 0; i < texture_count; i++, p += 32) {
        assert(p+32 <= end && memchr(p, 0, 32));
        char path[64];
        snprintf(path, sizeof(path), "rom:/textures/%s", (char *)p);
        sprites[i] = sprite_load(path);
        assert(sprites[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], NULL);
    }
    data_cache_hit_writeback(mesh_storage, size);
    for (unsigned i = 0; i < mesh_count; i++) {
        Mesh *mesh = &meshes[i];
        assert(p+sizeof(MeshHeader) <= end);
        memcpy(&mesh->h, p, sizeof(MeshHeader));
        p += sizeof(MeshHeader);
        MeshHeader *h = &mesh->h;
        assert(h->vertices > 0 && h->vertices <= 65535 && h->indices % 3 == 0);
        assert(h->texture >= -1 && h->texture < (int)texture_count);
        assert(p+h->vertices*sizeof(Vertex)+h->indices*2 <= end);
        const Vertex *vertices = (const Vertex *)p;
        p += h->vertices*sizeof(Vertex);
        const uint16_t *indices = (const uint16_t *)p;
        p += h->indices*2;
        p += (-(uintptr_t)p) & 3;
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
    }
    assert(p == end);
    /* Batches are material sorted offline. Nested lists cache the room's state
       changes and RSP transforms rather than resubmitting arrays every frame. */
    room_list = glGenLists(1);
    glNewList(room_list, GL_COMPILE);
    for (unsigned i = 0; i < mesh_count; i++)
        if (!meshes[i].h.model) glCallList(meshes[i].list);
    glEndList();
    debugf("Lightning: %u batches, %u textures, %ld mesh bytes\n", mesh_count, texture_count, size);
}

void scene_draw_room(void) { glCallList(room_list); }

void scene_draw_actor(unsigned model, float x, float z, float heading, float gait)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(heading, 0, 1, 0);
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
}
