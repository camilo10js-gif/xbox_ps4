// XboxPad shared configuration and mapping. No PS4 dependencies.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
namespace xp {
static const char *const path = "/data/GoldHEN/xbox_pad/config.ini";
static const uint32_t bits[] = {0x4000,0x2000,0x8000,0x1000,0x10,0x20,0x40,0x80,0x400,0x800,2,4,8,0x100000,0x10000,0x100,0x200};
static const char *const names[] = {"A / Cruz","B / Circulo","X / Cuadrado","Y / Triangulo","Arriba","Derecha","Abajo","Izquierda","LB / L1","RB / R1","LS / L3","RS / R3","Menu / Options","View / Touch","Xbox / PS","LT / L2","RT / R2","Desactivado"};
struct Profile { int map[17]; int dead[2]; int invert[4]; int swap; };
struct Config { Profile global, game[32]; char titles[32][10]; int count, diagnostic; };
struct State { uint32_t buttons; uint8_t axis[4], trigger[2]; };
inline Profile defaults() { Profile p={}; for(int i=0;i<17;i++) p.map[i]=i; p.dead[0]=p.dead[1]=16; return p; }
inline Config defaultsConfig() { Config c={}; c.global=defaults(); return c; }
inline bool titleValid(const char *s) { if(strlen(s)!=9 || strncmp(s,"CUSA",4)) return false; for(int i=4;i<9;i++) if(s[i]<'0'||s[i]>'9') return false; return true; }
inline int find(const Config &c,const char *title) { for(int i=0;i<c.count;i++) if(!strcmp(c.titles[i],title)) return i; return -1; }
inline Profile selected(const Config &c,const char *title) { int i=find(c,title); return i<0?c.global:c.game[i]; }
inline bool load(const char *file,Config &out) {
 FILE *f=fopen(file,"r"); if(!f)return false; Config c=defaultsConfig(); Profile *p=0; char line[256]; bool ok=true,version=false,global=false; int lines=0;
 while(fgets(line,sizeof(line),f)) { if(++lines>2048 || (!strchr(line,'\n')&&!feof(f))){ok=false;break;} line[strcspn(line,"\r\n")]=0; if(!*line||line[0]==';'||line[0]=='#')continue;
 if(line[0]=='[') { size_t n=strlen(line); if(n<3||line[n-1]!=']'){ok=false;break;} line[n-1]=0;
 if(!strcmp(line+1,"global")) {if(global||c.count){ok=false;break;}global=true;p=&c.global;} else { if(!titleValid(line+1)||c.count==32||find(c,line+1)>=0){ok=false;break;} strcpy(c.titles[c.count],line+1); c.game[c.count]=c.global; p=&c.game[c.count++]; } continue; }
 char *eq=strchr(line,'='); if(!eq){ok=false;break;} *eq++=0; char *end=0; long v=strtol(eq,&end,10); if(!*eq||*end||v<0||v>10000){ok=false;break;}
 if(!strcmp(line,"version")&&!p){if(version||v!=1){ok=false;break;}version=true;continue;}
 if(!strcmp(line,"diagnostic")&&!p){if(v>1){ok=false;break;} c.diagnostic=v;continue;}
 if(!p){ok=false;break;} int idx=-1; char tail=0;
 if(sscanf(line,"map%d%c",&idx,&tail)==1 && idx>=0 && idx<17 && v<=17)p->map[idx]=v;
 else if(!strcmp(line,"dead_left")&&v<=64)p->dead[0]=v;
 else if(!strcmp(line,"dead_right")&&v<=64)p->dead[1]=v;
 else if(!strcmp(line,"swap")&&v<=1)p->swap=v;
 else if(sscanf(line,"invert%d%c",&idx,&tail)==1&&idx>=0&&idx<4&&v<=1)p->invert[idx]=v;
 else {ok=false;break;}
 } if(ferror(f))ok=false; fclose(f); if(ok&&version)out=c; return ok&&version;
}
inline void printProfile(FILE *f,const Profile &p) {for(int i=0;i<17;i++)fprintf(f,"map%d=%d\n",i,p.map[i]);fprintf(f,"dead_left=%d\ndead_right=%d\nswap=%d\n",p.dead[0],p.dead[1],p.swap);for(int i=0;i<4;i++)fprintf(f,"invert%d=%d\n",i,p.invert[i]);}
inline bool save(const char *file,const Config &c) { char tmp[512]; if(snprintf(tmp,sizeof(tmp),"%s.tmp",file)>=(int)sizeof(tmp))return false; FILE *f=fopen(tmp,"w");if(!f)return false;fprintf(f,"version=1\ndiagnostic=%d\n[global]\n",c.diagnostic);printProfile(f,c.global);for(int i=0;i<c.count;i++){fprintf(f,"[%s]\n",c.titles[i]);printProfile(f,c.game[i]);} bool ok=!ferror(f);if(fclose(f))ok=false;Config check; if(!ok||!load(tmp,check)){remove(tmp);return false;} if(rename(tmp,file)){remove(tmp);return false;}return true; }
inline State map(const State &s,const Profile &p) {State o={};for(int i=0;i<17;i++){int dest=p.map[i];if(dest<0||dest>=17)continue; bool down=(s.buttons&bits[i])!=0;uint8_t strength=i>=15?s.trigger[i-15]:(down?255:0);if(down)o.buttons|=bits[dest];if(dest>=15&&strength>o.trigger[dest-15])o.trigger[dest-15]=strength;}
 for(int i=0;i<4;i++){int src=p.swap?(i+2)%4:i;int v=s.axis[src];if(p.invert[i])v=255-v;int delta=v-128;o.axis[i]=(delta>=-p.dead[i/2]&&delta<=p.dead[i/2])?128:v;} return o; }
}
