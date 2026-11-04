#include "NetworkFiles.h"
#include <cstdio>
#include <string>
int main() {
 int bad=0;
 auto check=[&](bool ok,const char *name){if(!ok){++bad;printf("FAIL %s\n",name);}};
 const char *path="/tmp/wibrometr-pem-test";
 auto write=[&](const std::string &s){FILE *f=fopen(path,"wb");fwrite(s.data(),1,s.size(),f);fclose(f);};
 std::string s; const char *why=nullptr;
 check(!net::readNetworkFile("/tmp/wibrometr-file-does-not-exist",8192,s,why),"missing file");
 write(std::string(8192,'a'));check(net::readNetworkFile(path,8192,s,why)&&s.size()==8192,"8192 bytes allowed");
 write(std::string(8193,'a'));check(!net::readNetworkFile(path,8192,s,why),"8193 bytes rejected");
 write("");check(!net::readNetworkFile(path,8192,s,why),"empty file rejected");
 write(std::string("a\0b",3));check(!net::readNetworkFile(path,8192,s,why),"embedded NUL rejected");
 check(net::pemEnvelopeValid("-----BEGIN CERTIFICATE-----\na\n-----END CERTIFICATE-----",false),"certificate envelope");
 check(!net::pemEnvelopeValid("-----BEGIN CERTIFICATE-----\na",false),"truncated PEM");
 for(const char *kind:{"PRIVATE KEY","RSA PRIVATE KEY","EC PRIVATE KEY"})
  check(net::pemEnvelopeValid(std::string("-----BEGIN ")+kind+"-----\na\n-----END "+kind+"-----",true),"key envelope");
 check(!net::pemEnvelopeValid("-----BEGIN ENCRYPTED PRIVATE KEY-----\na\n-----END ENCRYPTED PRIVATE KEY-----",true),"encrypted PKCS8");
 check(!net::pemEnvelopeValid("-----BEGIN RSA PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED\n-----END RSA PRIVATE KEY-----",true),"encrypted legacy key");
 remove(path); printf("%s (%d failures)\n",bad?"FAIL":"all ok",bad);return bad?1:0;
}
