#ifndef ADVENTURE_VISIBILITY_H
#define ADVENTURE_VISIBILITY_H
#include <math.h>
#include "presentation.h"

/* Transient draw decisions only. The bounds include the full rig at 24-degree
   limb swing, six-degree run lean, authored hair and ground contact shadow. */
#define ADVENTURE_ACTOR_CENTER 1.3f
#define ADVENTURE_ACTOR_RADIUS 2.2f
#define ADVENTURE_TILTED_ACTOR_RADIUS 3.6f
typedef struct {
 float eye[3],right[3],up[3],forward[3];
 float tan_x,tan_y,normal_x,normal_y,near,far;
 bool valid;
} AdventureFrustum;

static inline void adventure_frustum_init(AdventureFrustum *f,const PresentationCamera *camera,
 float aspect,float near,float far)
{
 *f=(AdventureFrustum){0};
 if(!camera||camera->fov<=1||camera->fov>=179||aspect<=0||near<0||far<=near)return;
 float length=0;
 for(unsigned i=0;i<3;i++) {
  f->eye[i]=camera->eye[i];f->forward[i]=camera->target[i]-camera->eye[i];
  length+=f->forward[i]*f->forward[i];
 }
 if(length<.000001f)return;
 length=sqrtf(length);
 for(unsigned i=0;i<3;i++)f->forward[i]/=length;
 float horizontal=sqrtf(f->forward[0]*f->forward[0]+f->forward[2]*f->forward[2]);
 if(horizontal<.000001f)return; /* Degenerate lookAt: conservatively draw. */
 f->right[0]=-f->forward[2]/horizontal;f->right[2]=f->forward[0]/horizontal;
 f->up[0]=-f->right[2]*f->forward[1];
 f->up[1]=f->right[2]*f->forward[0]-f->right[0]*f->forward[2];
 f->up[2]=f->right[0]*f->forward[1];
 f->tan_y=tanf(camera->fov*.00872664625997f);f->tan_x=f->tan_y*aspect;
 f->normal_x=sqrtf(1+f->tan_x*f->tan_x);f->normal_y=sqrtf(1+f->tan_y*f->tan_y);
 f->near=near;f->far=far;f->valid=true;
}

static inline bool adventure_frustum_sphere(const AdventureFrustum *f,
 float x,float y,float z,float radius)
{
 if(!f->valid)return true;
 float position[3]={x-f->eye[0],y-f->eye[1],z-f->eye[2]},depth=0,side=0,up=0;
 for(unsigned i=0;i<3;i++) {
  depth+=position[i]*f->forward[i];side+=position[i]*f->right[i];up+=position[i]*f->up[i];
 }
 if(depth+radius<f->near||depth-radius>f->far)return false;
 if(depth*f->tan_x-fabsf(side)<-radius*f->normal_x)return false;
 if(depth*f->tan_y-fabsf(up)<-radius*f->normal_y)return false;
 return true;
}

static inline bool adventure_frustum_actor(const AdventureFrustum *f,
 const PresentationPose *pose,float scale,float floor,float tilt)
{
 if(!pose)return false;
 float radius=(tilt?ADVENTURE_TILTED_ACTOR_RADIUS:ADVENTURE_ACTOR_RADIUS)*scale;
 return adventure_frustum_sphere(f,pose->x,
  floor+pose->floor+(pose->bob+ADVENTURE_ACTOR_CENTER)*scale,pose->z,radius);
}
#endif
