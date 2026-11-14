#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <errno.h>
namespace xp {
inline bool readFile(const std::string &p,std::string &s) {std::ifstream f(p.c_str(),std::ios::binary);if(!f)return false;std::ostringstream o;o<<f.rdbuf();if(f.bad())return false;s=o.str();return true;}
inline bool writeFile(const std::string &p,const std::string &s) {std::string t=p+".tmp";std::ofstream f(t.c_str(),std::ios::binary|std::ios::trunc);f.write(s.data(),s.size());f.close();if(!f){remove(t.c_str());return false;}return rename(t.c_str(),p.c_str())==0;}
// Reject ambiguous configuration rather than guessing its section boundaries.
inline bool activate(const std::string &input,const char *title,std::string &output) {
 if(input.size()>1024*1024 || !titleValid(title))return false;std::istringstream in(input);std::string line,section;bool found=false;std::string seen="\n";output.clear();
 while(std::getline(in,line)){std::string s=line;if(!s.empty()&&s.back()=='\r')s.pop_back();
 if(!s.empty()&&s[0]=='['){if(s.back()!=']'||s.find(']',1)!=s.size()-1)return false;section=s.substr(1,s.size()-2);if(seen.find("\n"+section+"\n")!=std::string::npos)return false;seen+=section+"\n";}
 else if(s.find('[')!=std::string::npos||s.find(']')!=std::string::npos)return false;
 if(s=="/data/GoldHEN/plugins/xbox_pad.prx" || s.find("/data/GoldHEN/plugins/xbox_pad.prx=")==0){if(section.empty())return false;if(section=="default")continue;if(section==title)continue;}
 output+=line+"\n";
 if(section==title && !s.empty()&&s[0]=='['){output+="/data/GoldHEN/plugins/xbox_pad.prx=true\n";found=true;}
 } if(!found)output+="["+std::string(title)+"]\n/data/GoldHEN/plugins/xbox_pad.prx=true\n";return true;
}
inline bool installPlugin(bool restore) {std::string source,old;std::string p="/data/GoldHEN/plugins/xbox_pad.prx";if(!readFile(restore?p+".bak":"/app0/assets/xbox_pad.prx",source)||source.empty())return false;if(!restore){struct stat st;if(stat(p.c_str(),&st)==0){if(!S_ISREG(st.st_mode)||!readFile(p,old)||!writeFile(p+".bak",old))return false;}else if(errno!=ENOENT)return false;}return writeFile(p,source);}
inline bool enableGame(const char *title) {std::string p="/data/GoldHEN/plugins.ini",old,out;struct stat st;if(stat(p.c_str(),&st)==0){if(!readFile(p,old))return false;}else if(errno!=ENOENT)return false;if(!activate(old,title,out))return false;if(!writeFile(p+".xbox_pad.bak",old))return false;return writeFile(p,out);}
}
