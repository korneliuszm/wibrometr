#include "WifiConfig.h"
#include <cstdio>
#include <cstdlib>
#include <string>
static int failures;
static void check(bool ok, const char *name) { if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
int main() {
 using namespace net;
 WifiConfig c; ConfigError e;
 auto parse=[&](const std::string &s){return parseWifiConfig(s.c_str(),c,&e);};
 check(parse("ssid=xiaomi\npassword=test-password\n"),"PSK");
 check(c.eap==Eap::None && c.hostname=="wibrometr" && c.ntp=="pool.ntp.org" && c.tz=="CET-1CEST,M3.5.0,M10.5.0/3","defaults");
 for(const char *mode:{"peap","ttls"}) {
  check(parse(std::string("ssid=enterprise\neap=")+mode+"\nusername=user\npassword=secret\nca=cert/ca.pem\n"),"password EAP");
  check(c.identity=="user" && c.caLine==5,"identity default and line");
  check(parse(std::string("ssid=e\neap=")+mode+"\nidentity=outer\nusername=inner\npassword=secret"),"EAP without CA");
  check(c.identity=="outer","explicit identity");
 }
 check(parse("ssid=e\neap=tls\nidentity=device\ncert=cert/client.crt\nkey=cert/client.key"),"TLS");
 check(c.eap==Eap::Tls && c.username.empty(),"TLS without username");
 check(parse("\xef\xbb\xbf# comment\r\n\r\n ssid = xiaomi # end\r\n password = a=b \r\n"),"BOM CRLF comments trim");
 check(c.password=="a=b" && c.ssid=="xiaomi","first equals only");
 check(!parse("ssid=x\npassword=p\nunknown=x") && e.line==3,"unknown line");
 check(!parse("# first\nssid=x\nbroken") && e.line==3,"missing equals line");
 check(!parse("ssid=x\nssid=y\npassword=p") && e.line==2,"duplicate key");
 check(!parse("ssid=x\npassword=p\neap=PEAP") && e.line==3,"invalid EAP");
 for(const char *bad:{"", "ssid=\npassword=p", "ssid=x", "ssid=x\neap=peap\npassword=p", "ssid=x\neap=ttls\nusername=u", "ssid=x\neap=tls\ncert=c\nkey=k", "ssid=x\neap=tls\nidentity=i\nkey=k", "ssid=x\neap=tls\nidentity=i\ncert=c", "ssid=x\npassword=p\nca=c", "ssid=x\npassword=p\ncert=c", "ssid=x\npassword=p\nkey=k"})
  check(!parse(bad),"required fields/conflicts");
 for(const char *path:{"/ca.pem","../ca.pem","cert/../ca.pem","cert/./ca.pem","cert\\ca.pem"})
  check(!parse(std::string("ssid=e\neap=peap\nusername=u\npassword=p\nca=")+path) && e.line==5,"bad path");
 check(parse("ssid="+std::string(32,'x')+"\npassword=p"),"SSID 32");
 check(!parse("ssid="+std::string(33,'x')+"\npassword=p"),"SSID 33");
 check(parse("ssid=e\neap=peap\nusername=u\npassword="+std::string(255,'p')),"value 255");
 check(!parse("ssid=e\neap=peap\nusername=u\npassword="+std::string(256,'p')),"value 256");
 for(const char *host:{"bad name","-bad","bad-","bad.local",""}) check(!parse(std::string("ssid=x\npassword=p\nhostname=")+host),"hostname");
 check(!parse("ssid=e\neap=peap\nusername=u\npassword=p\ncert=cert/c.pem"),"PEAP cert without key rejected");
 check(!parse("ssid=e\neap=ttls\nusername=u\npassword=p\nkey=cert/k.pem"),"TTLS key without cert rejected");
 check(parse("ssid=stable\npassword=p"),"atomic setup");
 check(!parse("ssid=other\npassword=p\nunknown=x") && c.ssid=="stable","atomic rejection");
 check(!parse(std::string(4097,'x')),"file limit");
 std::printf("%s (%d failures)\n",failures?"FAIL":"all ok",failures);
 return failures?1:0;
}
