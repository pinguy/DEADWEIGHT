/* DEADWEIGHT / SHIFT 01
 * A small magnetic salvage FPS built with Antoni Norman / Pingu's C Optimizer.
 * Font, collision approach and dynamic SDL loading adapted from ECHOHULL;
 * first-person salvage and procedural sound informed by nervk / VOIDRUNNER.
 * Copyright 2026 Antoni Norman (adapted example material).
 * Modifications: new game, software renderer, soundtrack, and input/capture harness.
 * Licensed under Apache-2.0. See LICENSE and NOTICE.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <errno.h>

typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
typedef struct { u32 type,time,id; u8 state,repeat,pad1,pad2; int scan,sym; uint16_t mod; u32 unused; } KeyEvent;
typedef union { u32 type; KeyEvent key; struct {u32 type,time,id,which,state; int x,y,dx,dy;} motion; struct {u32 type,time,id,which;u8 button,state,clicks,pad;int x,y;} button; struct {u32 type,time,id;u8 event,pad[3];int a,b;} window; u8 pad[56]; } Event;
typedef struct {const char *name;u32 flags,nformats,formats[16];int maxw,maxh;} RendererInfo;
typedef struct { int freq; uint16_t format; u8 channels,silence; uint16_t samples,pad; u32 size; void (*callback)(void*,u8*,int); void *userdata; } AudioSpec;
#define FN(ret,n,args) static ret (*n) args
FN(int,SDL_Init,(u32)); FN(void,SDL_Quit,(void)); FN(const char*,SDL_GetError,(void));
FN(void*,SDL_CreateWindow,(const char*,int,int,int,int,u32)); FN(void,SDL_DestroyWindow,(void*));
FN(void*,SDL_CreateRenderer,(void*,int,u32)); FN(void,SDL_DestroyRenderer,(void*));
FN(int,SDL_RenderSetLogicalSize,(void*,int,int)); FN(void*,SDL_CreateTexture,(void*,u32,int,int,int));
FN(void,SDL_DestroyTexture,(void*)); FN(int,SDL_UpdateTexture,(void*,const void*,const void*,int));
FN(int,SDL_RenderCopy,(void*,void*,const void*,const void*)); FN(void,SDL_RenderPresent,(void*));
FN(int,SDL_RenderReadPixels,(void*,const void*,u32,void*,int)); FN(int,SDL_PollEvent,(Event*));
FN(int,SDL_PushEvent,(Event*)); FN(int,SDL_SetRelativeMouseMode,(int)); FN(int,SDL_SetWindowFullscreen,(void*,u32));
FN(u32,SDL_GetTicks,(void)); FN(void,SDL_Delay,(u32));
FN(u64,SDL_GetPerformanceCounter,(void)); FN(u64,SDL_GetPerformanceFrequency,(void));
FN(int,SDL_GetRendererInfo,(void*,RendererInfo*));
FN(const char*,SDL_GetCurrentVideoDriver,(void));
FN(u32,SDL_OpenAudioDevice,(const char*,int,const AudioSpec*,AudioSpec*,int));
FN(void,SDL_PauseAudioDevice,(u32,int)); FN(int,SDL_QueueAudio,(u32,const void*,u32));
FN(u32,SDL_GetQueuedAudioSize,(u32)); FN(void,SDL_ClearQueuedAudio,(u32)); FN(void,SDL_CloseAudioDevice,(u32));
#define LOAD(n) do { *(void **)(&n)=dlsym(lib,#n); if(!n){fprintf(stderr,"Missing SDL function: %s\n",#n);return 2;} } while(0)

#define W 640
#define H 360
#define MW 18
#define MH 14
#define PI 3.14159265358979323846f
#define DT (1.0f/60.0f)
#define RATE 48000
#define NS 800
#define FMT 0x16362004u
enum { TITLE, PLAY, PAUSE, DEAD, WIN };
enum { FORWARD=1,BACK=2,LEFT=4,RIGHT=8,TURNL=16,TURNR=32,USE=64,ENTER=128,ESC=256,MUTE=512,SPRINT=1024,FULL=2048 };
static const int scans[]={26,22,4,7,20,8,9,40,41,16,225,68}; /* W S A D Q E F Enter Esc M Shift F11 */
static const char *map[MH]={
 "111111111111111111", "100000000000000001", "100000000000000001",
 "100000110011000001", "100000110011000001", "100000000000000001",
 "100000000000000001", "100000110011000001", "100000110011000001",
 "100000000000000001", "100000000000000001", "100000000000000001",
 "100000000000000001", "111111111111111111" };
typedef struct { float x,y,vx,vy; int kind,active; float flash; } Cargo;
typedef struct { float x,y,hp,hit,attack; } Drone;
typedef struct {float x,y,vx,vy,life;u32 color;} Spark;
static Cargo cargo[9]; static Drone drones[3]; static Spark sparks[96];
static struct {float x,y,yaw,hp,time,bob,hit,kick,notice;int state,held,cores,kills,pulls,throws,bumps,deposits;u32 seed;} G;
static u32 pixels[W*H],capture[W*H],keys,oldkeys,mouse,oldmouse,random_state=123;
static int wall_top[W],wall_bottom[W];
static u8 vignette[W*H];
static u32 wall_cache[16][2][96][64];
static float core_sin,core_cos;
static float depth[W],game_clock,nearest,viewbob; static int running=1,muted,fullscreen,testmode,frame,capture_every=3,io_error;
static int force_software,slow_frame_ms,audio_queue_empty_events,rendered_frames;
static double benchmark_seconds;
static RendererInfo renderer_info;
static void *win,*renderer,*texture; static u32 audio_dev; static FILE *video_file,*sound_file;
static const char *shot_dir; static const char *message=""; static float music_gain=.7f;
static u32 rnd(void){u32 x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x;}
static float clampf(float v,float a,float b){return v<a?a:v>b?b:v;}
static float dist(float x,float y){return sqrtf(x*x+y*y);}
static float wrap(float a){while(a>PI)a-=2*PI;while(a<-PI)a+=2*PI;return a;}
static int solid(float x,float y){int ix=(int)floorf(x),iy=(int)floorf(y);return ix<0||iy<0||ix>=MW||iy>=MH||map[iy][ix]!='0';}
static int can_stand(float x,float y,float r){return !solid(x-r,y-r)&&!solid(x+r,y-r)&&!solid(x-r,y+r)&&!solid(x+r,y+r);}
static int move(float *x,float *y,float dx,float dy,float r){int b=0;if(can_stand(*x+dx,*y,r))*x+=dx;else b=1;if(can_stand(*x,*y+dy,r))*y+=dy;else b=1;return b;}
static int sight(float ax,float ay,float bx,float by){float d=dist(bx-ax,by-ay);int n=(int)(d*12)+1;for(int i=1;i<n;i++)if(solid(ax+(bx-ax)*i/n,ay+(by-ay)*i/n))return 0;return 1;}
static u32 rgb(int r,int g,int b){return 0xff000000u|((u32)(r<0?0:r>255?255:r)<<16)|((u32)(g<0?0:g>255?255:g)<<8)|(u32)(b<0?0:b>255?255:b);}
/* Integer blending avoids float conversions and clamping for each channel. */
static inline __attribute__((always_inline)) u32 blend(u32 a,u32 b,unsigned w){
 unsigned iw=256-w;
 return 0xff000000u|((((a&0xff00ff)*iw+(b&0xff00ff)*w)>>8)&0xff00ff)|((((a&0xff00)*iw+(b&0xff00)*w)>>8)&0xff00);
}
static u32 mix(u32 a,u32 b,float k){return blend(a,b,(unsigned)(clampf(k,0,1)*256));}

static void put(int x,int y,u32 c){if((unsigned)x<W&&(unsigned)y<H)pixels[y*W+x]=c;}
static void rect(int x,int y,int w,int h,u32 c){int a=x<0?0:x,b=y<0?0:y;w+=x;h+=y;if(w>W)w=W;if(h>H)h=H;for(int yy=b;yy<h;yy++)for(int xx=a;xx<w;xx++)pixels[yy*W+xx]=c;}
static void line(int x,int y,int x2,int y2,u32 c){int dx=abs(x2-x),sx=x<x2?1:-1,dy=-abs(y2-y),sy=y<y2?1:-1,err=dx+dy;for(;;){put(x,y,c);if(x==x2&&y==y2)break;int e=err*2;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}}
static const u8 font[39][7]={
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,10,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{0,0,0,31,0,0,0},{0,12,12,0,12,12,0},{0,0,0,0,0,12,12}};

static int glyph(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='Z')return 10+c-'A';if(c=='-')return 36;if(c==':')return 37;if(c=='.')return 38;return -1;}
static void text(int x,int y,int s,const char *str,u32 c){for(;*str;str++,x+=6*s){int g=glyph(*str);if(g<0)continue;for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(font[g][yy]&(1<<(4-xx)))rect(x+xx*s,y+yy*s,s,s,c);}}
static void center(int y,int s,const char *str,u32 c){text((W-((int)strlen(str)*6-1)*s)/2,y,s,str,c);}
static void announce(const char *str){message=str;G.notice=2.5f;}

/* Sample-clock music and effects, queued to SDL; recording uses the same PCM. */
typedef struct {int kind;float age,pan;} Voice;
static Voice voices[16]; static int voice_n; static u64 sample_clock;
static float notes[96],delay_l[18000],delay_r[18000],hat_filter,pad_level;static int delay_pos;
static u32 audio_random=0x19771225;
static void sfx(int kind,float pan){voices[voice_n++%16]=(Voice){kind,0,clampf(pan,-1,1)};}
static float noise(void){audio_random^=audio_random<<13;audio_random^=audio_random>>17;audio_random^=audio_random<<5;return (int32_t)audio_random/2147483648.f;}
static float tri(float p){p-=floorf(p);return 1-4*fabsf(p-.5f);}
static void audio_frame(void){
 int16_t pcm[NS*2];static const int roots[]={38,34,41,43};static const int arp[]={0,7,12,3,7,15,12,7,0,3,10,7,12,7,3,7};
 for(int i=0;i<NS;i++,sample_clock++){
  double t=(double)sample_clock/RATE;float tick=(float)(t*7.2);int step=(int)tick;float a=(tick-step)/7.2f;int st=step&15,root=roots[(step/32)&3];
  float n=noise(),kick=0,snare=0,hat=0,at=1.f/RATE;
  if(st==0||st==6||st==8||st==11||(nearest<4&&st==14))kick=sinf(2*PI*(48*a+2.5f*(1-expf(-a*28))))*expf(-a*25)*.47f;
  if(st==4||st==12)snare=(n*.6f+sinf(a*2*PI*175)*.4f)*expf(-a*27)*.23f;
  float high=n-hat_filter;hat_filter+=.35f*(n-hat_filter);hat=high*expf(-a*((st&1)?80:130))*.055f;
  float bass=tri((float)(t*notes[root]))*.10f*(.5f+.5f*expf(-a*12));
  float pulse=sinf(2*PI*(float)(t*notes[root+24+arp[st]]));float pluck=pulse*expf(-a*22)*.047f;
  float pan=.65f*sinf(step*1.41f),chord=0;
  for(int v=0;v<3;v++){int off=v==0?12:v==1?15:19;chord+=sinf(2*PI*(float)fmod(t*notes[root+off],1)+v*.03f)*.018f;}
  pad_level+=(G.state==PLAY?.000006f:-.000003f);pad_level=clampf(pad_level,.35f,1);
  float l=(kick+snare+hat+bass+chord*pad_level+pluck*(1-pan)) *music_gain;
  float r=(kick+snare+hat+bass+chord*pad_level+pluck*(1+pan)) *music_gain;
  for(int v=0;v<16;v++)if(voices[v].kind){Voice *q=&voices[v];float z=q->age,out=0;q->age+=at;
   if(q->kind==1)out=(sinf(2*PI*(120*z+500*z*z))*.14f+n*.025f)*expf(-z*8);
   if(q->kind==2)out=(n*.24f+sinf(2*PI*(90*z-35*z*z))*.27f)*expf(-z*15);
   if(q->kind==3)out=(n*.32f+sinf(z*2*PI*61)*.3f)*expf(-z*11);
   if(q->kind==4)out=(sinf(z*2*PI*587.33f)+sinf(z*2*PI*880))*.12f*expf(-z*4);
   if(q->kind==5)out=(n*.25f+sinf(z*2*PI*105)*.18f)*expf(-z*8);
   if(q->kind==6){int note=(int)(z*7);if(note<7)out=sinf(z*2*PI*notes[62+(int[]){0,3,7,12,7,15,19}[note]])*.2f*expf(-fmodf(z,1.f/7)*12)*expf(-z);}
   l+=out*(1-q->pan)*.7f;r+=out*(1+q->pan)*.7f;if(z>2)q->kind=0;
  }
  float dl=delay_l[delay_pos],dr=delay_r[delay_pos];delay_l[delay_pos]=r*.17f+dr*.31f;delay_r[delay_pos]=l*.17f+dl*.31f;delay_pos=(delay_pos+1)%18000;
  l=tanhf((l+dl)*1.4f)*.88f;r=tanhf((r+dr)*1.4f)*.88f;if(muted)l=r=0;
  pcm[i*2]=(int16_t)(l*32767);pcm[i*2+1]=(int16_t)(r*32767);
 }
 if(sound_file&&fwrite(pcm,sizeof(pcm),1,sound_file)!=1)io_error=1;
 if(audio_dev){if(SDL_GetQueuedAudioSize(audio_dev)>RATE*4)SDL_ClearQueuedAudio(audio_dev);if(SDL_QueueAudio(audio_dev,pcm,sizeof(pcm))<0)io_error=1;}
}
/* Normal playback follows the audio device's consumption, independently of
 * rendering and simulation. The deterministic harness keeps one PCM block
 * per fixed step, so its captures still reproduce the original composition. */
static void pump_audio(void){
 if(!audio_dev)return;
 u32 queued=SDL_GetQueuedAudioSize(audio_dev);
 if(!queued&&sample_clock)audio_queue_empty_events++;
 while(queued<NS*4*5){audio_frame();queued=SDL_GetQueuedAudioSize(audio_dev);if(io_error)break;}
}
static void particles(float x,float y,u32 c){for(int i=0;i<18;i++){Spark *p=&sparks[rnd()%96];float a=(rnd()%628)/100.f,v=.4f+(rnd()%250)/100.f;*p=(Spark){x,y,cosf(a)*v,sinf(a)*v,.3f+(rnd()%65)/100.f,c};}}
static void restart(void){
 u32 seed=G.seed?G.seed:123;memset(&G,0,sizeof(G));G.seed=seed;random_state=seed;G.x=3.5f;G.y=10.5f;G.yaw=-PI/2;G.hp=100;G.time=180;G.held=-1;G.state=PLAY;
 const float positions[9][2]={{3.5,3.5},{14.5,3.5},{14.5,10.5},{3.5,7.5},{8.5,10.5},{13.5,8.5},{8.5,2.5},{3.5,11.8},{14.5,5.5}};
 for(int i=0;i<9;i++)cargo[i]=(Cargo){positions[i][0],positions[i][1],0,0,i<3,1,0};
 drones[0]=(Drone){3.5,4.9,2,0,0};drones[1]=(Drone){14.5,6.5,2,0,0};drones[2]=(Drone){10.5,11.5,2,0,0};
 memset(sparks,0,sizeof(sparks));announce("THREE CORES. BACK TO THE LIFT.");if(!testmode)SDL_SetRelativeMouseMode(1);
}
static int target(void){float best=5.3f;int index=-1;for(int i=0;i<9;i++)if(cargo[i].active){Cargo *c=&cargo[i];float dx=c->x-G.x,dy=c->y-G.y,d=dist(dx,dy);float angle=fabsf(wrap(atan2f(dy,dx)-G.yaw));if(d<best&&angle<.22f+.16f/fmaxf(d,.4f)&&sight(G.x,G.y,c->x,c->y)){best=d;index=i;}}return index;}
static void release(int throwing){if(G.held<0)return;Cargo *c=&cargo[G.held];c->vx=cosf(G.yaw)*(throwing?16:0);c->vy=sinf(G.yaw)*(throwing?16:0);c->flash=.3f;G.held=-1;if(throwing){G.throws++;G.kick=.22f;sfx(2,0);}else sfx(1,0);}
static void use(int throwing){if(G.held>=0){release(throwing);return;}int i=target();if(i>=0){G.held=i;G.pulls++;cargo[i].vx=cargo[i].vy=0;sfx(1,0);announce(cargo[i].kind?"CORE LOCKED. RETURN TO LIFT.":"PLATE LOCKED. LEFT CLICK TO THROW.");}else announce("AIM AT METAL WITHIN FIVE METRES");}
static void step(void){
 u32 edge=keys&~oldkeys,click=mouse&~oldmouse;game_clock+=DT;
 if(edge&MUTE)muted=!muted;
 if(edge&FULL){fullscreen=!fullscreen;if(!testmode)SDL_SetWindowFullscreen(win,fullscreen?0x1001:0);}
 if(edge&ESC){if(G.state==TITLE||G.state==DEAD||G.state==WIN)running=0;else {G.state=G.state==PAUSE?PLAY:PAUSE;if(!testmode)SDL_SetRelativeMouseMode(G.state==PLAY);}}
 if((edge&ENTER)&&(G.state==TITLE||G.state==DEAD||G.state==WIN))restart();
 if(G.state==PLAY){
  G.yaw=wrap(G.yaw+((!!(keys&TURNR))-(!!(keys&TURNL)))*1.8f*DT);
  float f=(!!(keys&FORWARD))-(!!(keys&BACK)),s=(!!(keys&RIGHT))-(!!(keys&LEFT));float mag=dist(f,s);
  if(mag>0){float speed=(keys&SPRINT)?4.25f:3.2f;if(G.held>=0)speed*=.79f;float dx=(cosf(G.yaw)*f-sinf(G.yaw)*s)*speed*DT/mag,dy=(sinf(G.yaw)*f+cosf(G.yaw)*s)*speed*DT/mag;G.bumps+=move(&G.x,&G.y,dx,dy,.22f);G.bob+=DT*speed*2.6f;}
  if((click&1)||(edge&USE))use(!!(click&1));
  if(click&4)release(0);
  if(G.held>=0){Cargo *c=&cargo[G.held];float tx=G.x+cosf(G.yaw)*1.15f,ty=G.y+sinf(G.yaw)*1.15f;move(&c->x,&c->y,(tx-c->x)*fminf(1,DT*13),(ty-c->y)*fminf(1,DT*13),.15f);}
  for(int i=0;i<9;i++)if(cargo[i].active&&i!=G.held){Cargo *c=&cargo[i];float speed=dist(c->vx,c->vy);if(speed>.02f){int n=1+(int)(speed*DT/.08f);for(int j=0;j<n;j++){if(move(&c->x,&c->y,c->vx*DT/n,c->vy*DT/n,.15f)){c->vx*=-.35f;c->vy*=-.35f;sfx(3,0);}for(int d=0;d<3;d++)if(drones[d].hp>0&&speed>3&&dist(c->x-drones[d].x,c->y-drones[d].y)<.63f){drones[d].hp-=speed*.3f;drones[d].hit=.5f;c->vx*=-.12f;c->vy*=-.12f;sfx(3,clampf((drones[d].x-G.x)*.1f,-1,1));particles(drones[d].x,drones[d].y,0xffffbc61);if(drones[d].hp<=0){G.kills++;announce("SECURITY UNIT SCRAPPED");}speed=0;}}c->vx*=.98f;c->vy*=.98f;} }
  for(int i=0;i<9;i++)if(cargo[i].active&&cargo[i].kind&&dist(cargo[i].x-3.5f,cargo[i].y-10.5f)<1.3f){cargo[i].active=0;if(G.held==i)G.held=-1;G.cores++;G.deposits++;sfx(4,0);particles(3.5f,10.5f,0xffa4e8ff);G.hp=fminf(100,G.hp+18);announce("CORE BANKED. HULL PATCHED.");if(G.cores==3){G.state=WIN;sfx(6,0);if(!testmode)SDL_SetRelativeMouseMode(0);}}
  nearest=30;
  for(int d=0;d<3;d++){Drone *e=&drones[d];e->hit=fmaxf(0,e->hit-DT);e->attack-=DT;if(e->hp<=0)continue;float dx=G.x-e->x,dy=G.y-e->y,l=dist(dx,dy);nearest=fminf(nearest,l);if(l<7.0f||G.held>=0){float speed=e->hit>0?.2f:.92f;if(l>.58f&&sight(e->x,e->y,G.x,G.y))move(&e->x,&e->y,dx/fmaxf(l,.1f)*speed*DT,dy/fmaxf(l,.1f)*speed*DT,.27f);if(l<.78f&&e->attack<=0&&G.state==PLAY){G.hp-=18;e->attack=.8f;G.hit=.42f;sfx(5,0);}}}
  G.time-=DT;if((G.hp<=0||G.time<=0)&&G.state==PLAY){G.state=DEAD;G.hp=fmaxf(0,G.hp);sfx(5,0);if(!testmode)SDL_SetRelativeMouseMode(0);}
 }
 for(int i=0;i<96;i++)if(sparks[i].life>0){sparks[i].life-=DT;sparks[i].x+=sparks[i].vx*DT;sparks[i].y+=sparks[i].vy*DT;}
 G.hit=fmaxf(0,G.hit-DT);G.kick=fmaxf(0,G.kick-DT);G.notice=fmaxf(0,G.notice-DT);
 oldkeys=keys;oldmouse=mouse;if(testmode)audio_frame();frame++;
}
static void events(void){Event e;while(SDL_PollEvent(&e)){if(e.type==0x100)running=0;
 if(e.type==0x300||e.type==0x301){for(int i=0;i<12;i++)if(e.key.scan==scans[i]){if(e.type==0x300)keys|=1u<<i;else keys&=~(1u<<i);}}
 if(e.type==0x400&&G.state==PLAY)G.yaw=wrap(G.yaw+e.motion.dx*.003f);
 if(e.type==0x401||e.type==0x402){u32 b=1u<<(e.button.button-1);if(e.type==0x401)mouse|=b;else mouse&=~b;}
 if(e.type==0x200&&e.window.event==13){keys=mouse=0;if(G.state==PLAY){G.state=PAUSE;if(!testmode)SDL_SetRelativeMouseMode(0);}}
}}

static u32 surface(float x,float y,int ceiling){
 int tx=(int)floorf(x*64),ty=(int)floorf(y*64);int a=tx&63,b=ty&63;u32 hash=(u32)(tx*374761393u+ty*668265263u);int n=(hash>>27)&7;
 if(ceiling){int band=(a<3||b<3);if((a>25&&a<38)&&(b>5&&b<57)&&(((int)x+(int)y)%3==0))return rgb(109,157,166);return rgb(24+n-band*7,32+n-band*9,40+n-band*10);}
 int seam=a<2||b<2;u32 c=rgb(37+n-seam*16,49+n-seam*20,57+n-seam*21);
 if((a<9&&b<9)&&(a==5||b==5))c=rgb(71,81,84);
 if(fabsf(x-3.5f)<.045f&&y>3&&y<10.5f)c=rgb(156,110,40);
 float lx=x-3.5f,ly=y-10.5f,lift=lx*lx+ly*ly;if(lift<1.44f){c=rgb(44,73,79);if(lift>1.21f||fabsf(x-3.5f)<.022f||fabsf(y-10.5f)<.022f)c=rgb(131,222,229);}
 for(int i=0;i<3;i++)if(cargo[i].active){float dx=x-cargo[i].x,dy=y-cargo[i].y;if(fabsf(dx)<1&&fabsf(dy)<1){float d2=dx*dx+dy*dy;if(d2<1)c=mix(c,0xff6c5b99,(1-sqrtf(d2))*.27f);}}
 return c;
}
static u32 wall_tex(float u,float v,int x,int y,int side){
 int a=(int)(u*64)&63,b=(int)(v*96)&95;int n=((a*13+b*7+x*3)&7);int seam=a<3||a>60||b<3;
 u32 c=rgb(62+n-seam*32,78+n-seam*37,86+n-seam*39);
 if(a>8&&a<55&&b>16&&b<60){c=rgb(36+n,48+n,58+n);if((b%7)<2)c=rgb(21,30,37);}
 if(b>75&&b<84){int stripe=((a+b)/10)&1;c=stripe?0xffcc9b43:0xff25323b;}
 if((a<7||a>56)&&(b==8||b==67))c=0xffa4b0ae;
 if(b>7&&b<10&&a>10&&a<53)c=0xff93dfe0;
 if((x+y)%4==0&&a>18&&a<46&&b>24&&b<48){c=0xff12232d;if(b%6==0&&a<39)c=0xff58aebb;}
 return side?mix(c,0xff111d27,.23f):c;
}
static void prepare_graphics(void){
 for(int t=0;t<16;t++)for(int side=0;side<2;side++)for(int b=0;b<96;b++)for(int a=0;a<64;a++){
  int x=t/2,y=(t&1)?(4-x%4)%4:(5-x%4)%4;
  wall_cache[t][side][b][a]=wall_tex((a+.25f)/64,(b+.25f)/96,x,y,side);
 }
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)vignette[y*W+x]=(u8)(256*.09f*(fabsf(x-W/2.f)/(W/2.f)+fabsf(y-H/2.f)/(H/2.f)));
}
static void world(void){
 float dx=cosf(G.yaw),dy=sinf(G.yaw),plane=.72f,px=-dy*plane,py=dx*plane,proj=W/(2*plane);viewbob=sinf(G.bob)*2;int horizon=166+(int)viewbob;
 /* Establish wall coverage first. Hidden floor/ceiling fragments need no work. */
 for(int x=0;x<W;x++){
  float cx=2.f*x/W-1,rx=dx+px*cx,ry=dy+py*cx;int mx=(int)G.x,my=(int)G.y,stepx=rx<0?-1:1,stepy=ry<0?-1:1,side=0;
  float ddx=1/fmaxf(fabsf(rx),.00001f),ddy=1/fmaxf(fabsf(ry),.00001f);
  float sx=(rx<0?G.x-mx:mx+1-G.x)*ddx,sy=(ry<0?G.y-my:my+1-G.y)*ddy;
  for(int i=0;i<60;i++){if(sx<sy){sx+=ddx;mx+=stepx;side=0;}else{sy+=ddy;my+=stepy;side=1;}if(mx<0||my<0||mx>=MW||my>=MH||map[my][mx]!='0')break;}
  float d=fmaxf(.05f,side?sy-ddy:sx-ddx);depth[x]=d;float u=side?G.x+d*rx:G.y+d*ry;u-=floorf(u);
  int top=horizon-(int)(.95f*proj/d),bottom=horizon+(int)(.65f*proj/d);int from=top<0?0:top,to=bottom>=H?H-1:bottom;
  wall_top[x]=from;wall_bottom[x]=to;
  unsigned fog=(unsigned)(clampf(d/20,.05f,.86f)*256);int tx=(int)(u*64)&63,t=((mx&7)*2)+((mx+my)%4==0);float scale=96.f/fmaxf(1,bottom-top);
  for(int y=from;y<=to;y++)pixels[y*W+x]=blend(wall_cache[t][side][((int)((y-top)*scale))&95][tx],0xff101923,fog);
 }
 for(int y=0;y<H;y++){
  int ceiling=y<horizon;float camera=ceiling?.95f:.65f,row=camera*proj/fmaxf(1,fabsf((float)(y-horizon)));
  float ax=G.x+row*(dx-px),ay=G.y+row*(dy-py),sx=2*px*row/W,sy=2*py*row/W;unsigned fog=(unsigned)(clampf(row/19,.05f,.96f)*256);
  for(int x=0;x<W;x++,ax+=sx,ay+=sy)if(y<wall_top[x]||y>wall_bottom[x])pixels[y*W+x]=blend(surface(ax,ay,ceiling),0xff101923,fog);
 }
}

static u32 sprite_pixel(int type,float u,float v,int hit){
 float ax=fabsf(u);if(type==0){if(ax>.70f||fabsf(v)>.45f)return 0;if(ax>.62f||fabsf(v)>.36f)return 0xff647b88;if((ax>.46f&&fabsf(v)>.20f))return 0xffc2d0cd;if(fabsf(v)<.09f)return 0xffc3944d;return (((int)((u+1)*18))%4==0)?0xff3b4e5a:0xff26353e;}
 if(type==1){float rr=u*u+v*v;if(rr>.6889f&&rr<.81f)return 0xffc0a7ff;if(ax<.33f&&fabsf(v)<.54f){if(ax>.24f||fabsf(v)>.45f)return 0xffd7d4e9;return ((int)((v+1)*30)%8<3)?0xffe6d8ff:0xff8f70d2;}if(fabsf(u*core_cos-v*core_sin)<.075f&&rr<.5625f&&rr>.1225f)return 0xffb5a3e8;return 0;}
 if(hit)return ax<.65f&&fabsf(v)<.5f?0xfff9e4b0:0;
 if(fabsf(v-.34f)<.20f&&ax>.26f&&ax<.68f)return 0xff324d5d;
 if(v<-.28f&&v>-.44f&&ax<.56f)return 0xffbcc2b1;
 if(ax<.65f&&v>-.32f&&v<.33f){if(ax>.53f||v>.23f)return 0xff52616b;if(v>-.13f&&v<.05f)return ax<.42f?0xffffc369:0xff262c30;if(ax<.22f&&v>.11f)return 0xffe0b06a;return 0xff75828a;}
 if(ax>.67f&&ax<.88f&&fabsf(v)<.13f)return 0xffffbd6b;
 return 0;
}
static void sprite(float x,float y,int type,int held,int hit){
 float dx=x-G.x,dy=y-G.y,forward=cosf(G.yaw)*dx+sinf(G.yaw)*dy,side=-sinf(G.yaw)*dx+cosf(G.yaw)*dy;if(forward<.12f)return;
 float proj=W/1.44f;float h=type==2?.95f:.73f;float z=type==2?.62f:type==1?.60f:.28f;if(held)z=.49f;
 int cx=W/2+side*proj/forward,cy=166+viewbob+(.65f-z)*proj/forward;int size=fminf(1500,h*proj/forward),left=cx-size/2,top=cy-size/2;
 int x0=left<0?-left:0,x1=left+size>W?W-left:size,y0=top<0?-top:0,y1=top+size>H?H-top:size;
 float inv=2.f/fmaxf(1,size);unsigned fog=held?0:(unsigned)(clampf(forward/22,0,.8f)*256);
 for(int yy=y0;yy<y1;yy++){int y=top+yy;float v=yy*inv-1;for(int xx=x0;xx<x1;xx++){int xx2=left+xx;if(forward>depth[xx2])continue;float u=xx*inv-1;u32 c=sprite_pixel(type,u,v,hit);if(c)pixels[y*W+xx2]=blend(c,0xff111923,fog);}}

}
static void objects(void){
 core_sin=sinf(game_clock*1.5f);core_cos=cosf(game_clock*1.5f);
 int order[12],count=0;float d[12];for(int i=0;i<9;i++)if(cargo[i].active){order[count]=i;d[count++]=dist(cargo[i].x-G.x,cargo[i].y-G.y);}for(int i=0;i<3;i++)if(drones[i].hp>0){order[count]=9+i;d[count++]=dist(drones[i].x-G.x,drones[i].y-G.y);}
 for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)if(d[j]>d[i]){float f=d[i];d[i]=d[j];d[j]=f;int a=order[i];order[i]=order[j];order[j]=a;}
 for(int i=0;i<count;i++){int id=order[i];if(id<9)sprite(cargo[id].x,cargo[id].y,cargo[id].kind,id==G.held,0);else sprite(drones[id-9].x,drones[id-9].y,2,0,drones[id-9].hit>0);}
 for(int i=0;i<96;i++)if(sparks[i].life>0){float dx=sparks[i].x-G.x,dy=sparks[i].y-G.y,f=cosf(G.yaw)*dx+sinf(G.yaw)*dy,s=-sinf(G.yaw)*dx+cosf(G.yaw)*dy;if(f>.1f){int x=W/2+s*W/1.44f/f,y=170+(0.3f-sparks[i].life*.35f)*W/1.44f/f;if(x>=0&&x<W&&f<depth[x])rect(x,y,2,2,sparks[i].color);}}
}
static void weapon(void){
 int x=455+(int)(sinf(G.bob)*3),y=283+(int)(fabsf(cosf(G.bob))*3+G.kick*65);
 rect(x+19,y+31,87,85,0xff121b24);rect(x+6,y+11,100,49,0xff15232e);rect(x+10,y+7,86,43,0xff627683);rect(x+17,y+11,79,32,0xff344955);rect(x-8,y-6,57,50,0xff1a2937);rect(x-5,y-10,52,44,0xff77939f);rect(x,y-5,43,34,0xff203442);
 for(int i=0;i<4;i++){rect(x+4+i*9,y-2,4,27,G.held<0?0xff67b3c1:0xffc3a5fa);}rect(x+64,y+13,23,11,0xff11252d);rect(x+68,y+17,15,3,0xffb5e5e6);rect(x+73,y+43,35,9,0xffd0a465);rect(x+85,y+53,12,40,0xff32434e);
 if(G.held>=0){for(int k=0;k<2;k++){int px=x+9+k*23,py=y+12;for(int i=1;i<=12;i++){int nx=px+(W/2-px)*i/12,ny=py+(218-py)*i/12+(int)(sinf(game_clock*37+i*2+k)*4);line(px,py,nx,ny,k?0xffded0ff:0xff83d9e2);px=nx;py=ny;}}}
}
static void minimap(void){
 int ox=540,oy=35,sc=4;rect(ox-5,oy-5,MW*sc+10,MH*sc+10,0xff101b25);
 for(int y=0;y<MH;y++)for(int x=0;x<MW;x++)rect(ox+x*sc,oy+y*sc,3,3,map[y][x]=='1'?0xff4d606b:0xff1e303b);
 rect(ox+12,oy+40,5,5,0xff90e6e7);for(int i=0;i<3;i++)if(cargo[i].active)rect(ox+(int)(cargo[i].x*sc)-1,oy+(int)(cargo[i].y*sc)-1,3,3,0xffcbb0ff);
 int px=ox+G.x*sc,py=oy+G.y*sc;rect(px-1,py-1,3,3,0xffffe2a9);line(px,py,px+cosf(G.yaw)*6,py+sinf(G.yaw)*6,0xffffe2a9);
}
static void dim(float a){for(int i=0;i<W*H;i++)pixels[i]=mix(pixels[i],0xff09131d,a);}
static void hud(void){
 char b[96];u32 white=0xffdfebeb,amber=0xffffc475,mutedc=0xffa0b5c0,cyan=0xff9ddfe5;
 if(G.state==TITLE){dim(.60f);rect(31,34,3,244,amber);text(48,35,1,"FERRO SALVAGE DIVISION     CONTRACT 018",mutedc);text(46,64,7,"DEAD",white);text(46,119,7,"WEIGHT",white);text(49,183,1,"A MAGNET. A BAD CONTRACT.",amber);text(49,205,1,"BANK THREE CORES AT THE LIFT.",white);text(49,220,1,"THROW SCRAP TO BREAK SECURITY DRONES.",mutedc);rect(48,248,230,28,amber);text(62,257,1,"ENTER - CLOCK IN",0xff15232b);text(48,301,1,"WASD MOVE    MOUSE LOOK    LEFT CLICK PULL OR THROW",white);text(48,316,1,"F PULL OR DROP    RIGHT CLICK DROP    SHIFT SPRINT",mutedc);text(48,331,1,"Q E TURN    M MUTE    F11 FULLSCREEN    ESC QUIT",mutedc);text(451,41,1,"SHIFT",mutedc);text(450,58,6,"01",amber);return;}
 if(G.state==PLAY||G.state==PAUSE){
  rect(0,0,W,25,0xff101a23);text(15,9,1,"DEADWEIGHT",white);text(94,9,1,"- SHIFT 01",mutedc);snprintf(b,sizeof(b),"COLLAPSE %02d:%02d",(int)G.time/60,(int)G.time%60);text(254,9,1,b,amber);text(528,9,1,muted?"AUDIO OFF":"AUDIO ON",mutedc);
  minimap();weapon();rect(12,307,148,41,0xff111d27);text(22,316,1,"HULL",mutedc);snprintf(b,sizeof(b),"%03d",(int)G.hp);text(114,315,2,b,white);rect(22,335,128,4,0xff35434a);rect(22,335,(int)(128*G.hp/100),4,amber);
  rect(178,320,164,28,0xff111d27);snprintf(b,sizeof(b),"CORES %d OF 3",G.cores);text(190,330,1,b,white);for(int i=0;i<3;i++){rect(280+i*16,328,10,10,0xffb39bdd);if(i>=G.cores)rect(282+i*16,330,6,6,0xff17222f);}
  line(313,175,317,175,white);line(323,175,327,175,white);line(320,168,320,172,white);line(320,178,320,182,white);
  int id=target();if(G.held>=0)center(260,1,cargo[G.held].kind?"CORE LOCKED - RETURN TO LIFT":"PLATE LOCKED - LEFT CLICK TO THROW",cyan);else if(id>=0)center(248,1,cargo[id].kind?"LEFT CLICK - PULL CORE":"LEFT CLICK - PULL PLATE",white);
  if(G.notice>0){rect(103,36,410,20,0xff101c27);center(42,1,message,amber);}else {center(42,1,G.held>=0?"LIFT: SOUTH WEST CORNER":"CORES: VIOLET MARKERS ON SCANNER",mutedc);}
  if(G.hit>0){int n=5+G.hit*12;rect(0,25,n,H-25,0xffc99150);rect(W-n,25,n,H-25,0xffc99150);}
 }
 if(G.state==PAUSE){dim(.73f);center(122,4,"SHIFT PAUSED",white);center(169,1,"ESC - RESUME",amber);center(202,1,"WASD MOVE   MOUSE LOOK   Q E TURN",white);center(219,1,"LEFT CLICK PULL OR THROW   F OR RIGHT CLICK DROP",white);center(244,1,"M MUTE   F11 FULLSCREEN",mutedc);}
 if(G.state==DEAD||G.state==WIN){dim(.73f);center(71,1,"FERRO SALVAGE DIVISION - SHIFT REPORT",mutedc);center(103,3,G.state==WIN?"SHIFT COMPLETE":"CONTRACT TERMINATED",G.state==WIN?cyan:amber);center(142,1,G.state==WIN?"ALL THREE CORES SECURED. YOU MADE IT OUT.":G.time<=0?"THE STATION COLLAPSED.":"SECURITY CLOSED YOUR CONTRACT.",white);snprintf(b,sizeof(b),"CORES BANKED  %d OF 3",G.cores);center(181,2,b,white);snprintf(b,sizeof(b),"DRONES SCRAPPED  %d",G.kills);center(207,1,b,mutedc);snprintf(b,sizeof(b),"SHIFT TIME  %d SECONDS",(int)(180-G.time));center(225,1,b,mutedc);center(274,1,"ENTER - ANOTHER SHIFT      ESC - QUIT",amber);}
}
static void render(void){world();objects();for(int i=0;i<W*H;i++)pixels[i]=blend(pixels[i],0xff06111a,vignette[i]);hud();if(SDL_UpdateTexture(texture,0,pixels,W*4)<0||SDL_RenderCopy(renderer,texture,0,0)<0)io_error=1;if(testmode||shot_dir){if(SDL_RenderReadPixels(renderer,0,FMT,capture,W*4)<0)io_error=1;}SDL_RenderPresent(renderer);}
static int readback(void){return !io_error;}
static void shot(const char *name){if(!shot_dir||!readback())return;char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",shot_dir,name);FILE *f=fopen(path,"wb");if(!f){perror(path);io_error=1;return;}fprintf(f,"P6\n%d %d\n255\n",W,H);for(int i=0;i<W*H;i++){u8 c[3]={capture[i]>>16,capture[i]>>8,capture[i]};if(fwrite(c,3,1,f)!=1)io_error=1;}if(fclose(f))io_error=1;}
static int push(Event *e){if(SDL_PushEvent(e)!=1){fprintf(stderr,"Input injection: %s\n",SDL_GetError());io_error=1;return 0;}return 1;}
static void inject(u32 newkeys,int dx,u32 buttons){
 for(int i=0;i<12;i++)if((keys^newkeys)&(1u<<i)){Event e={0};e.type=(newkeys&(1u<<i))?0x300:0x301;e.key.scan=scans[i];e.key.state=e.type==0x300;push(&e);}
 for(int i=0;i<3;i++)if((mouse^buttons)&(1u<<i)){Event e={0};e.type=(buttons&(1u<<i))?0x401:0x402;e.button.button=i+1;e.button.state=e.type==0x401;push(&e);}
 if(dx){Event e={0};e.type=0x400;e.motion.dx=dx;push(&e);}
}
static void telemetry(void){
 printf("{\"frame\":%d,\"state\":%d,\"x\":%.5f,\"y\":%.5f,\"yaw\":%.6f,\"hp\":%.1f,\"time\":%.4f,\"held\":%d,\"cores\":%d,\"kills\":%d,\"pulls\":%d,\"throws\":%d,\"bumps\":%d,\"muted\":%d,\"running\":%d,\"cargo\":[",frame,G.state,G.x,G.y,G.yaw,G.hp,G.time,G.held,G.cores,G.kills,G.pulls,G.throws,G.bumps,muted,running);
 for(int i=0;i<9;i++)printf("%s[%.4f,%.4f,%d]",i?",":"",cargo[i].x,cargo[i].y,cargo[i].active);
 printf("],\"drones\":[");for(int i=0;i<3;i++)printf("%s[%.4f,%.4f,%.2f]",i?",":"",drones[i].x,drones[i].y,drones[i].hp);
 printf("]}\n");fflush(stdout);
}
int main(int argc,char **argv){
 const char *video=0,*sound=0;int render_every=1;G.seed=123;
 for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--test-io"))testmode=1;else if(!strcmp(argv[i],"--software"))force_software=1;else if(!strcmp(argv[i],"--benchmark")&&i+1<argc)benchmark_seconds=strtod(argv[++i],0);else if(!strcmp(argv[i],"--frame-delay-ms")&&i+1<argc)slow_frame_ms=atoi(argv[++i]);else if(!strcmp(argv[i],"--seed")&&i+1<argc)G.seed=(u32)strtoul(argv[++i],0,10);else if(!strcmp(argv[i],"--shots")&&i+1<argc)shot_dir=argv[++i];else if(!strcmp(argv[i],"--video")&&i+1<argc)video=argv[++i];else if(!strcmp(argv[i],"--audio")&&i+1<argc)sound=argv[++i];else if(!strcmp(argv[i],"--render-every")&&i+1<argc)render_every=atoi(argv[++i]);else {fprintf(stderr,"Usage: %s [--seed N] [--software] [--benchmark SECONDS [--frame-delay-ms N]] [--test-io --shots DIR --video RAW --audio PCM --render-every N]\n",argv[0]);return 2;}}
 if(render_every<1||render_every>60||benchmark_seconds<0||benchmark_seconds>60||slow_frame_ms<0||slow_frame_ms>200||(slow_frame_ms&&!benchmark_seconds))return 2;
 if(!testmode&&(shot_dir||video||sound)){fprintf(stderr,"Capture options require --test-io.\n");return 2;}
 void *lib=dlopen("libSDL2-2.0.so.0",RTLD_NOW);if(!lib){fprintf(stderr,"SDL2 runtime missing: %s\n",dlerror());return 2;}
 LOAD(SDL_Init);LOAD(SDL_Quit);LOAD(SDL_GetError);LOAD(SDL_CreateWindow);LOAD(SDL_DestroyWindow);LOAD(SDL_CreateRenderer);LOAD(SDL_DestroyRenderer);LOAD(SDL_RenderSetLogicalSize);LOAD(SDL_CreateTexture);LOAD(SDL_DestroyTexture);LOAD(SDL_UpdateTexture);LOAD(SDL_RenderCopy);LOAD(SDL_RenderPresent);LOAD(SDL_RenderReadPixels);LOAD(SDL_PollEvent);LOAD(SDL_PushEvent);LOAD(SDL_SetRelativeMouseMode);LOAD(SDL_SetWindowFullscreen);LOAD(SDL_GetTicks);LOAD(SDL_Delay);LOAD(SDL_GetPerformanceCounter);LOAD(SDL_GetPerformanceFrequency);LOAD(SDL_GetRendererInfo);LOAD(SDL_GetCurrentVideoDriver);LOAD(SDL_OpenAudioDevice);LOAD(SDL_PauseAudioDevice);LOAD(SDL_QueueAudio);LOAD(SDL_GetQueuedAudioSize);LOAD(SDL_ClearQueuedAudio);LOAD(SDL_CloseAudioDevice);
 if(SDL_Init(0x20)<0){fprintf(stderr,"SDL video: %s\n",SDL_GetError());return 2;}
 win=SDL_CreateWindow("DEADWEIGHT - SHIFT 01",0x2fff0000,0x2fff0000,testmode?W:1280,testmode?H:720,testmode?8:32);
 if(win&&!testmode&&!force_software)renderer=SDL_CreateRenderer(win,-1,2);
 if(win&&!renderer)renderer=SDL_CreateRenderer(win,-1,1);
 if(!renderer){fprintf(stderr,"Window / software renderer: %s\n",SDL_GetError());SDL_Quit();return 2;}
 SDL_GetRendererInfo(renderer,&renderer_info);
 SDL_RenderSetLogicalSize(renderer,W,H);texture=SDL_CreateTexture(renderer,FMT,1,W,H);if(!texture){fprintf(stderr,"Texture: %s\n",SDL_GetError());SDL_Quit();return 2;}
 if(SDL_Init(0x10)==0){AudioSpec want={RATE,0x8010,2,0,1024,0,0,0,0},got={0};audio_dev=SDL_OpenAudioDevice(0,0,&want,&got,0);}
 if(!audio_dev)fprintf(stderr,"Audio unavailable; game remains playable: %s\n",SDL_GetError());
 for(int i=0;i<96;i++)notes[i]=440*powf(2,(i-69)/12.f);
 prepare_graphics();nearest=30;restart();G.state=TITLE;if(!testmode)SDL_SetRelativeMouseMode(0);
 if(video){video_file=fopen(video,"wb");if(!video_file){perror(video);SDL_Quit();return 2;}}
 if(sound){sound_file=fopen(sound,"wb");if(!sound_file){perror(sound);SDL_Quit();return 2;}}
 render();shot("title");
 if(audio_dev){if(testmode){int16_t silence[NS*8]={0};SDL_QueueAudio(audio_dev,silence,sizeof(silence));}else pump_audio();SDL_PauseAudioDevice(audio_dev,0);}
 if(testmode){char cmd[128];telemetry();while(running&&fgets(cmd,sizeof(cmd),stdin)){
  int n=0,dx=0;u32 mask=0,buttons=0;char name[64];
  if(sscanf(cmd,"shot %63s",name)==1){int safe=1;for(char *p=name;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))safe=0;if(!safe){io_error=1;break;}render();shot(name);telemetry();continue;}
  if(sscanf(cmd,"step %d %u %d %u",&n,&mask,&dx,&buttons)!=4||n<1||n>18000||mask>4095||buttons>7){fprintf(stderr,"Invalid test input\n");io_error=1;break;}
  inject(mask,dx,buttons);
  for(int j=0;j<n&&running;j++){events();int previous=G.state;step();if(frame%render_every==0||previous!=G.state||(video_file&&frame%capture_every==0)){render();if(video_file&&frame%capture_every==0&&readback())if(fwrite(capture,sizeof(capture),1,video_file)!=1)io_error=1;}
   if(previous!=G.state){render();shot(G.state==PLAY?"play":G.state==WIN?"win":G.state==DEAD?(G.time<=0?"collapse":"dead"):G.state==PAUSE?"pause":"title");}}
  telemetry();if(io_error)break;
 }}else{
  /* Draw at most once per iteration, but keep simulation at 60 Hz.
   * Slow presentation can require multiple fixed updates. Audio is filled
   * according to device demand, so it keeps playing through slower frames. */
  u64 origin=SDL_GetPerformanceCounter(),last=origin;double frequency=(double)SDL_GetPerformanceFrequency(),acc=DT,elapsed=0;
  if(benchmark_seconds)inject(ENTER,0,0);
  while(running){
   u64 now=SDL_GetPerformanceCounter();double delta=(now-last)/frequency;last=now;elapsed=(now-origin)/frequency;acc+=delta>.25?.25:delta;
   events();if(!running)break;
   pump_audio();
   if(acc+1e-9<DT){SDL_Delay(1);continue;}
   while(acc+1e-9>=DT&&running){step();acc-=DT;}
   render();rendered_frames++;
   if(benchmark_seconds&&keys&ENTER)inject(0,0,0);
   if(slow_frame_ms)SDL_Delay((u32)slow_frame_ms);
   elapsed=(SDL_GetPerformanceCounter()-origin)/frequency;
   if(benchmark_seconds&&elapsed>=benchmark_seconds)break;
  }
  if(benchmark_seconds){printf("{\"wall_seconds\":%.4f,\"rendered_frames\":%d,\"fps\":%.2f,\"simulation_steps\":%d,\"simulation_seconds\":%.4f,\"mission_seconds\":%.4f,\"audio_seconds\":%.4f,\"audio_queue_empty_events\":%d,\"renderer\":\"%s\",\"video_driver\":\"%s\",\"frame_delay_ms\":%d}\n",elapsed,rendered_frames,rendered_frames/elapsed,frame,frame/60.0,180-G.time,sample_clock/(double)RATE,audio_queue_empty_events,renderer_info.name?renderer_info.name:"unknown",SDL_GetCurrentVideoDriver(),slow_frame_ms);fflush(stdout);}
 }

 if(video_file&&fclose(video_file))io_error=1;
 if(sound_file&&fclose(sound_file))io_error=1;
 if(audio_dev)SDL_CloseAudioDevice(audio_dev);
 SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(win);SDL_Quit();dlclose(lib);return io_error?3:0;
}
