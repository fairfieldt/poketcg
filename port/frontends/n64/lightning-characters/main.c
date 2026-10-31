#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include "scene.h"
#include "world.h"

#if LIGHTNING_CAPTURE
/* Diagnostic ROM only: read the completed RDP framebuffer through the uncached
   alias and send it to the emulator's homebrew console, without UI automation. */
static void capture_frame(surface_t *frame, const char *name)
{
    const uint16_t *pixels=UncachedAddr(frame->buffer);
    debugf("FRAMEBUFFER %s 320 240\n", name);
    for (int y=0;y<240;y++) {
        for (int x=0;x<320;x+=32) {
            char line[129];
            for (int i=0;i<32;i++)
                snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
            debugf("PIXELS %d %d %s\n",x,y,line);
        }
    }
    debugf("FRAMEBUFFER END\n");
}
#endif

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE,
                 FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    display_set_fps_limit(30);
    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();
    rdpq_init();
    gl_init();
    surface_t depth=surface_alloc(FMT_RGBA16,320,240);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS_INTERPENETRATING_N64);
    glEnable(GL_MULTISAMPLE_ARB);
    glDisable(GL_CULL_FACE); /* Interior panels and skirt have two visible sides. */
    glDisable(GL_LIGHTING); /* Flat vertex lighting is exported with the meshes. */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(58,4.0/3.0,1.5,65);
    glMatrixMode(GL_MODELVIEW);
    scene_load();
    assertf(glGetError()==GL_NO_ERROR,"Scene setup GL error");
    Player player;
    world_reset(&player);
    float pan=0;
    uint32_t last=timer_ticks();
#if LIGHTNING_CAPTURE
    unsigned frame_number=0;
#endif
    for (;;) {
        uint32_t now=timer_ticks();
        float dt=TICKS_TO_US((uint32_t)(now-last))/1000000.0f;
        last=now;
        joypad_poll();
        joypad_inputs_t input=joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t held=joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1);
        float x=input.stick_x/80.0f, z=-input.stick_y/80.0f;
        if (held.d_left) x=-1;
        if (held.d_right) x=1;
        if (held.d_up) z=-1;
        if (held.d_down) z=1;
        if (pressed.start) { world_reset(&player); pan=0; }
#if LIGHTNING_CAPTURE
        x=0; z=frame_number>=3 ? -1:0; dt=1.0f/30;
#endif
        world_move(&player,x,z,held.b,dt);
        pan += ((held.c_right ? 1:0)-(held.c_left ? 1:0))*dt*4;
        pan=fmaxf(-5,fminf(5,pan));
        surface_t *frame=display_get();
        rdpq_attach(frame,&depth);
        gl_context_begin();
        glClearColor(.03,.015,.065,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        /* Fixed entrance composition, with a small optional horizontal pan. */
#if LIGHTNING_CAPTURE
        if (frame_number >= 153) {
            /* Actual libdragon/RDP model closeups, separate from room captures. */
            gluLookAt(2.6,2.1,5.3,0,1.15,0,0,1,0);
            if (frame_number == 153) scene_draw_actor(4,0,0,0,0);
            else if (frame_number == 154) scene_draw_actor(1,0,0,0,0);
            else {
                scene_draw_actor(4,-.9,0,180,frame_number == 156 ? 1.2f:0);
                scene_draw_actor(1,.9,0,180,frame_number == 156 ? 1.2f:0);
            }
        } else {
#endif
        gluLookAt(14+pan,9,34,14+pan*.2,0,21.5,0,1,0);
        scene_draw_room();
        for (unsigned i=0;i<4;i++) {
            const Actor *a=&lightning_npcs[i];
            scene_draw_actor(a->model,a->x,a->z,a->heading,0);
        }
        scene_draw_actor(1,player.x,player.z,player.heading,player.gait);
#if LIGHTNING_CAPTURE
        }
#endif
        gl_context_end();
#if LIGHTNING_CAPTURE
        assertf(glGetError()==GL_NO_ERROR,"Frame GL error");
        rdpq_detach_wait();
        frame_number++;
        if (frame_number == 3) capture_frame(frame,"entrance");
        if (frame_number == 33) capture_frame(frame,"walking");
        if (frame_number == 153) {
            assertf(player.z>18.71f && player.z<18.9f,"NPC collision failed");
            capture_frame(frame,"npc-collision");
        }
        if (frame_number == 154) capture_frame(frame,"mason-front");
        if (frame_number == 155) capture_frame(frame,"ronald-front");
        if (frame_number == 156) capture_frame(frame,"characters-back");
        if (frame_number == 157) {
            capture_frame(frame,"walking-pose");
            debugf("CAPTURE DONE\n");
        }
        display_show(frame);
#else
        rdpq_detach_show();
#endif
    }
}
