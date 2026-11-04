#include "NetworkJson.h"
#include <cstdio>
#include <cstring>
int main() {
 net::Status s; strcpy(s.ssid,"a\"b\\c\n"); strcpy(s.message,"bad\tfile");s.errorLine=3;
 const auto json=net::statusJson(s);
 bool ok=json.find("a\\\"b\\\\c\\n")!=std::string::npos && json.find("bad\\tfile")!=std::string::npos &&
         json.find("\"error_line\":3")!=std::string::npos && json.find("password")==std::string::npos;
 printf("%s JSON escaping and no secrets\n",ok?"all ok":"FAIL");return ok?0:1;
}
