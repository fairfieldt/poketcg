/* Private doorway review: identical source poses with old/new room meshes. */
#define main production_entry
#define scene_open doorway_review_scene_open
#include "../main.c"
#undef main
#undef scene_open
Scene *scene_open(const char *,const char *);
static bool before;
Scene *doorway_review_scene_open(const char *path,const char *textures)
{
 static char replacement[192];
 if(before&&!strncmp(path,"rom:/world/maps/",16)) {
  snprintf(replacement,sizeof(replacement),"rom:/before/maps/%s",path+16);
  return scene_open(replacement,"rom:/before/textures/");
 }
 return scene_open(path,textures);
}
typedef struct {const char *name;unsigned map,x,z,patch;} PropCase;
static const PropCase cases[]={
 {"foyer-west-north",10,6,8,0},{"lobby-east",11,20,12,0},
 {"water-south",12,12,24,0},{"lab-east",1,22,12,0},
 {"lab-south",1,14,22,0},{"deck-west",2,6,12,0},
 {"ishihara-south",3,10,16,0},{"fighting-south",6,10,10,0},
 {"rock-south",9,12,22,0},{"lightning-south",15,12,24,0},
 {"grass-south",18,12,24,0},{"psychic-south",21,12,20,0},
 {"science-south",24,12,24,0},{"fire-south",27,12,24,0},
 {"challenge-south",30,14,24,0},{"dome-entrance-north",31,22,6,0},
 {"dome-north",32,14,4,0},{"dome-south",32,14,24,0},
 {"honor-south",33,10,20,0}
};
static void fixture(const PropCase *c)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 game.ow.map=c->map;game.ow.x=c->x;game.ow.y=c->z;game.ow.direction=NORTH;
 loaded_map=-1;
 ow_set_event(&game.ow,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
 if(c->patch==99)for(unsigned p=2;p<=9;p++)game.ow.map_patch[p]=1;
 else if(c->patch)game.ow.map_patch[c->patch]=1;
 for(const OwNpcDef *n=ow_map_npcs[c->map];n&&n->id;n++) {
  if(n->id==255||n->x>=w3_maps[c->map].width||n->y>=w3_maps[c->map].height)continue;
  unsigned i=game.ow.npc_count++;assertf(i<OW_NPC_LIMIT,"Doorway fixture overflow");
  game.ow.npcs[i]=(OwNpc){n->id,n->x,n->y,n->dir,1,ow_npc_headers[n->id].sprite};
  game.ow.npc_flags[i]=ow_npc_headers[n->id].flags;
  game.ow.npc_animation[i]=n->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[n->id].animation;
 }
 presentation_init(&presentation,&game.ow,false);
 for(unsigned f=0;f<60;f++)presentation_camera_step(&presentation,&game.ow,&w3_maps[c->map]);
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(n64_video_init(true),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 display_set_fps_limit(30);dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();
 depth=n64_video_surface();scene_begin_opaque();
 glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();n64_ui_init();actors=scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)for(unsigned variant=0;variant<2;variant++) {
  before=variant==0;fixture(&cases[i]);OwState canonical=game.ow;
  char label[64];snprintf(label,sizeof(label),"%s-%s",cases[i].name,before?"before":"after");
  uint64_t elapsed=0;
  for(unsigned f=0;f<12;f++) {
   presentation.render_frames=0;presentation.player.idle_phase=0;
   for(unsigned n=0;n<presentation.npc_count;n++)presentation.npcs[n].idle_phase=0;
   uint64_t start=get_ticks();surface_t *frame=game_render_frame();
   n64_video_present(frame);adventure_audio_poll();
   if(f>=4)elapsed+=get_ticks()-start;
   if(f==11) {
    debugf("VIDEO REVIEW %s BEGIN\n",label);capture(frame,label);
    heap_stats_t heap;sys_get_heap_stats(&heap);
    debugf("DOORWAY_CASE {\"name\":\"%s\",\"free_heap\":%u,\"mean_cadence_us\":%u,\"samples\":8}\n",
           label,(unsigned)(heap.total-heap.used),(unsigned)(TICKS_TO_US(elapsed)/8));
   }
  }
  assertf(!memcmp(&game.ow,&canonical,sizeof(canonical)),"Doorway review changed source state");
 }
 debugf("DOORWAYS NATIVE PASS\n");for(;;)adventure_audio_poll();
}
