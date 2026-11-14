#include "shared/config.h"
#include "shared/install.h"
#include <cassert>
#include <unistd.h>
int main(){
 xp::Profile p=xp::defaults();xp::State s={0x4000,{128,128,255,0},{0,0}};
 p.map[0]=1;auto out=xp::map(s,p);assert(out.buttons==0x2000&&s.buttons==0x4000);
 p.map[0]=15;out=xp::map(s,p);assert(out.trigger[0]==255 && (out.buttons&0x100));
 s.buttons=0x100;s.trigger[0]=127;p=xp::defaults();p.map[15]=16;out=xp::map(s,p);assert(out.trigger[1]==127&&out.trigger[0]==0);
 p.map[15]=17;out=xp::map(s,p);assert(out.trigger[0]==0&&out.trigger[1]==0&&out.buttons==0);
 p=xp::defaults();p.swap=1;p.invert[0]=1;out=xp::map(s,p);assert(out.axis[0]==0&&out.axis[1]==0&&out.axis[2]==128);
 s.axis[0]=112;p=xp::defaults();assert(xp::map(s,p).axis[0]==128);s.axis[0]=111;assert(xp::map(s,p).axis[0]==111);
 char dir[]="/tmp/xp-test-XXXXXX";assert(mkdtemp(dir));std::string file=std::string(dir)+"/config.ini";
 xp::Config c=xp::defaultsConfig();c.count=1;strcpy(c.titles[0],"CUSA10484");c.game[0]=p;c.game[0].map[0]=1;assert(xp::save(file.c_str(),c));xp::Config loaded={};assert(xp::load(file.c_str(),loaded));assert(xp::selected(loaded,"CUSA10484").map[0]==1);assert(xp::selected(loaded,"CUSA00000").map[0]==0);
 assert(xp::writeFile(file,"version=2\n[global]\n"));assert(!xp::load(file.c_str(),loaded)&&loaded.count==1);
 assert(xp::writeFile(file,"version=1\n[global]\nmap0=999\n"));assert(!xp::load(file.c_str(),loaded));
 std::string ini="; keep\n[default]\n/data/GoldHEN/plugins/other.prx=true\n/data/GoldHEN/plugins/xbox_pad.prx=true\n[CUSA10484]\n; comment\n", edited;
 assert(xp::activate(ini,"CUSA10484",edited));assert(edited.find("other.prx=true")!=std::string::npos&&edited.find("; comment")!=std::string::npos);
 std::string again;assert(xp::activate(edited,"CUSA10484",again)&&again==edited);
 assert(!xp::activate("[default]\n[default]\n","CUSA10484",edited));assert(!xp::activate("foo=true[default]\n","CUSA10484",edited));
 remove(file.c_str());rmdir(dir);puts("PASS mappings, analog triggers, axes, profile roundtrip, corrupt config, INI preservation");
}
