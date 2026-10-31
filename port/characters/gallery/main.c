#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <string.h>
#include "../../frontends/n64/lightning-characters/scene.h"
#include "assets/models.h"

#if CAST_CAPTURE
static void capture(surface_t *frame,const char *name)
{
    const uint16_t *pixels=UncachedAddr(frame->buffer);
    debugf("FRAMEBUFFER %s 320 240\n",name);
    for(int y=0;y<240;y++) for(int x=0;x<320;x+=32) {
        char line[129];
        for(int i=0;i<32;i++)snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
        debugf("PIXELS %d %d %s\n",x,y,line);
    }
    debugf("FRAMEBUFFER END\n");
}
#endif

int main(void)
{
    debug_init_isviewer();debug_init_usblog();
    display_init(RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    display_set_fps_limit(30);dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();
    surface_t depth=surface_alloc(FMT_RGBA16,320,240);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS_INTERPENETRATING_N64);
    glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(34,4.0/3.0,.4,20);
    glMatrixMode(GL_MODELVIEW);scene_load();
    assertf(glGetError()==GL_NO_ERROR,"Cast setup GL error");
    unsigned selected=0;float heading=0,gait=0;unsigned frame_number=0;
    debugf("CAST READY %u models\n",(unsigned)MODEL_COUNT);
    for(;;) {
        joypad_poll();
        joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_buttons_t held=joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_inputs_t input=joypad_get_inputs(JOYPAD_PORT_1);
        if(pressed.d_right || pressed.c_right)selected=(selected+1)%MODEL_COUNT;
        if(pressed.d_left || pressed.c_left)selected=(selected+MODEL_COUNT-1)%MODEL_COUNT;
        if(pressed.start){heading=0;gait=0;}
        heading+=input.stick_x*.035f;
        gait=held.b ? gait+.22f:0;
#if CAST_CAPTURE
        selected=(frame_number/4)%MODEL_COUNT;
        heading=(frame_number%4)*90;
        gait=frame_number%4==3 ? 1.2f:0;
#endif
        surface_t *frame=display_get();rdpq_attach(frame,&depth);gl_context_begin();
        glClearColor(.15,.17,.21,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();gluLookAt(2.4,2.2,4.5,0,1.15,0,0,1,0);
        scene_draw_actor(selected+1,0,0,heading,gait);
        gl_context_end();
        assertf(glGetError()==GL_NO_ERROR,"Cast frame GL error %u",selected);
        rdpq_detach_wait();
#if CAST_CAPTURE
        if(frame_number%4==0 && (selected%7==0 || strstr(model_names[selected],"lying") || selected+1==MODEL_COUNT))capture(frame,model_names[selected]);
        if(frame_number%4==3)debugf("MODEL_OK %u %s\n",selected,model_names[selected]);
        if(frame_number+1==MODEL_COUNT*4)debugf("CAST DONE %u\n",(unsigned)MODEL_COUNT);
#endif
        display_show(frame);frame_number++;
    }
}
