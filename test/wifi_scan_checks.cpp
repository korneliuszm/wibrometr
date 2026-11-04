#include "WifiScan.h"
#include <WiFi.h>
#include <cstdio>
int main() {
 int bad=0;auto check=[&](bool ok,const char *name){if(!ok){++bad;printf("FAIL %s\n",name);}};
 line::Fingerprint f;
 wifiscan::start();
 for(int i=0;i<3;++i) { WiFi.completion=WIFI_SCAN_FAILED;wifiscan::poll();testNow+=1000; }
 check(!wifiscan::take(f),"three failed scans must not publish an empty fingerprint");
 for(int i=0;i<4;++i) {testNow+=1000;wifiscan::poll();WiFi.completion=1;wifiscan::poll();}
 check(wifiscan::take(f)&&f.count==1&&f.ap[0].rssi==-45,"retry and average three successful scans including client AP");
 printf("%s scan retry (%d failures)\n",bad?"FAIL":"all ok",bad);return bad?1:0;
}
