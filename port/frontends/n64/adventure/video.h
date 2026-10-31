#ifndef ADVENTURE_VIDEO_H
#define ADVENTURE_VIDEO_H
#include <libdragon.h>
#include <stdbool.h>

/* Gameplay and interface composition retain 320x240 logical coordinates.
 * Expansion Pak builds render into a physical 480x360 surface. */
extern unsigned n64_video_width, n64_video_height;
extern float n64_video_scale;
resolution_t n64_video_init(bool high_resolution);
int n64_video_x(int logical);
int n64_video_y(int logical);
surface_t n64_video_surface(void);
/* Present a completed, still-owned framebuffer with the internal FPS badge. */
void n64_video_present(surface_t *frame);
void n64_video_box(surface_t *canvas,int x,int y,int width,int height,uint32_t color);
/* RDP must already be attached. Source pixels use logical or physical units. */
void n64_video_blit(const surface_t *source,int x,int y);
void n64_video_blit_physical(const surface_t *source,int x,int y);
#endif
