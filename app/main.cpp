// Native PS4 configurator; UI navigation always uses the DualShock.
#include "graphics.h"
#include <sstream>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include "shared/config.h"
#include "shared/install.h"
std::stringstream debugLogStream;
static xp::Config cfg;
static char title[10]="CUSA10484";
static bool dirty=false;
static std::string message="Cierra el juego antes de instalar o actualizar.";
static int profile=-1,tab=0,row=0,digit=4;
static xp::Profile &current(){return profile<0?cfg.global:cfg.game[profile];}
static void text(Scene2D &s,FT_Face f,int y,const std::string &v){std::string clipped=v.substr(0,100);s.DrawText((char*)clipped.c_str(),f,70,y,{16,22,30},{235,240,245});}
static void change(int direction){xp::Profile &p=current();if(tab==1||tab==2)dirty=true;if(tab==1)p.map[row]=(p.map[row]+direction+18)%18;else if(tab==2){if(row<2){p.dead[row]+=direction;if(p.dead[row]<0)p.dead[row]=0;if(p.dead[row]>64)p.dead[row]=64;}else if(row==2)p.swap^=1;else p.invert[row-3]^=1;}else if(tab==0&&row==1){title[digit]=(char)('0'+(title[digit]-'0'+direction+10)%10);}}
int main(){
 mkdir("/data/GoldHEN",0777);mkdir("/data/GoldHEN/plugins",0777);mkdir("/data/GoldHEN/xbox_pad",0777);
 cfg=xp::defaultsConfig();xp::load(xp::path,cfg);
 Scene2D &scene=*new Scene2D(1920,1080,4);if(!scene.Init(0x2000000,2))return 1;FT_Face font;if(!scene.InitFont(&font,"/app0/assets/font.ttf",30))return 2;
 OrbisUserServiceInitializeParams init={};init.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;sceUserServiceInitialize(&init);int user=0;sceUserServiceGetInitialUser(&user);scePadInit();int handle=scePadOpen(user,0,0,0);if(handle<0)return 3;
 uint32_t previous=0;int frame=0;
 for(;;){OrbisPadData pad={};uint32_t buttons=0;if(!scePadReadState(handle,&pad))buttons=pad.buttons;uint32_t press=buttons&~previous;previous=buttons;
 if(press&0x400){tab=(tab+3)%4;row=0;}if(press&0x800){tab=(tab+1)%4;row=0;}
 int rows=tab==0?5:tab==1?17:tab==2?7:4;
 if(press&0x10)row=(row+rows-1)%rows;if(press&0x40)row=(row+1)%rows;
 if(press&0x80)change(-1);if(press&0x20)change(1);
 if(press&0x8000){if(tab==0&&row==1)digit=digit==8?4:digit+1;}
 if(press&0x1000){if(xp::save(xp::path,cfg)){dirty=false;message="Configuracion guardada. Abre el juego para probar.";}else message="No se pudo guardar. Revisa permisos y espacio.";}
 if(press&0x4000){if(tab==0){if(row==0)profile=-1;if(row==2){int i=xp::find(cfg,title);if(i<0&&cfg.count<32){i=cfg.count++;strcpy(cfg.titles[i],title);cfg.game[i]=cfg.global;dirty=true;}if(i>=0)profile=i;else message="Limite: 32 perfiles.";}if(row==3){current()=xp::defaults();dirty=true;}if(row==4){cfg.diagnostic^=1;dirty=true;}}
 if(tab==3){bool ok=false;if(row==0)ok=xp::installPlugin(false);if(row==1)ok=xp::installPlugin(true);if(row==2)ok=xp::enableGame(title);if(row==3){xp::Config loaded;if(xp::load(xp::path,loaded)){cfg=loaded;profile=-1;dirty=false;ok=true;}}message=ok?"Operacion completada. Reinicia el juego.":"Operacion fallida; revisa archivos, INI y permisos.";}}
 scene.FrameBufferFill({16,22,30});text(scene,font,65,"XboxPad Configurator 1.0 | plugin 2.11");
 static const char *tabs[]={"Perfiles","Botones","Sticks","Instalacion"};text(scene,font,115,std::string("L1 / R1: ")+tabs[tab]+"    Perfil: "+(profile<0?"Global":cfg.titles[profile])+(dirty?"   * Sin guardar":""));
 for(int i=0;i<rows;i++){std::string label;if(tab==0){const char *items[]={"Editar perfil global","CUSA: ","Crear / editar el CUSA indicado","Restaurar valores del perfil","Diagnostico: "};label=items[i];if(i==1)label+=std::string(title)+"   digito "+std::to_string(digit-3)+" (Cuadrado: siguiente)";if(i==4)label+=cfg.diagnostic?"Si":"No";}if(tab==1)label=std::string(xp::names[i])+"  ->  "+xp::names[current().map[i]];
 if(tab==2){const char *items[]={"Zona muerta izquierda","Zona muerta derecha","Intercambiar sticks","Invertir X izquierdo","Invertir Y izquierdo","Invertir X derecho","Invertir Y derecho"};int v=i<2?current().dead[i]:i==2?current().swap:current().invert[i-3];label=std::string(items[i])+": "+std::to_string(v);}
 if(tab==3){const char *items[]={"Instalar / actualizar PRX (respalda el anterior)","Restaurar PRX anterior","Activar para el CUSA indicado (quita carga global XboxPad)","Descartar cambios y recargar configuracion"};label=items[i];}
 text(scene,font,180+i*40,(row==i?"> ":"  ")+label);}
 text(scene,font,930,"Cruceta: elegir / cambiar | Cruz: ejecutar | Triangulo: guardar");text(scene,font,975,message);text(scene,font,1020,"Usa el DualShock. Los cambios del PRX requieren cerrar y abrir el juego.");scene.SubmitFlip(frame);scene.FrameWait(frame++);scene.FrameBufferSwap();
 }
}
