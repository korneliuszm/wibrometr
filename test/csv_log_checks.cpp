#include "CsvLog.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
static std::string read(const std::string &p) {
 FILE *f=fopen(p.c_str(),"rb"); if(!f)return "";std::string s;char b[512];size_t n;
 while((n=fread(b,1,sizeof b,f)))s.append(b,n);fclose(f);return s;
}
int main() {
 char directory[]="/tmp/wibrometr-csv-XXXXXX";if(!mkdtemp(directory))return 1;
 std::string dir=directory,path=dir+"/ride.csv",old=dir+"/ride_old.csv";int bad=0;
 auto check=[&](bool ok,const char *name){if(!ok){++bad;printf("FAIL %s\n",name);}};
 check(storage::appendCsvFile(path.c_str(),"first\n"),"first append");
 check(storage::appendCsvFile(path.c_str(),"second\n"),"second append");
 check(read(path)==std::string(storage::logHeader)+"first\nsecond\n","two rows do not rotate matching header");
 check(access(old.c_str(),F_OK)!=0,"no spurious rotation");
 FILE *f=fopen(path.c_str(),"wb");fputs("old header\nhistoric\n",f);fclose(f);
 check(storage::appendCsvFile(path.c_str(),"new\n") && read(old)=="old header\nhistoric\n","old header preserved");
 f=fopen(path.c_str(),"wb");fputs("another old header\n",f);fclose(f);
 check(storage::appendCsvFile(path.c_str(),"next\n") && read(old)=="old header\nhistoric\n" && read(dir+"/ride_old2.csv")=="another old header\n","existing backup preserved");
 f=fopen(path.c_str(),"wb");fputs("cannot rotate\n",f);fclose(f);
 chmod(dir.c_str(),0500);
 check(!storage::appendCsvFile(path.c_str(),"bad\n") && read(path)=="cannot rotate\n" && read(old)=="old header\nhistoric\n","rotation failure leaves originals intact");
 chmod(dir.c_str(),0700);
 for(auto suffix:{"/ride.csv","/ride_old.csv","/ride_old2.csv"})remove((dir+suffix).c_str());rmdir(dir.c_str());
 printf("%s CSV rotation (%d failures)\n",bad?"FAIL":"all ok",bad);return bad?1:0;
}
