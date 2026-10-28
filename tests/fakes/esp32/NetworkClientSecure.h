#pragma once
#include "Arduino.h"
inline unsigned test_live_tls,test_max_tls;
class NetworkClientSecure {
public:
    bool verified=false,live=false;
    void useBuiltinCACertBundle() {verified=true;}
    void setHandshakeTimeout(unsigned seconds) {assert(seconds==8);}
    void stop() {if(live){assert(test_live_tls);--test_live_tls;live=false;}}
    void open() {assert(verified && !live && test_live_tls==0);live=true;++test_live_tls;test_max_tls=max(test_max_tls,test_live_tls);}
    int lastError(char *buffer,size_t size) {if(size)buffer[0]=0;return -1;}
};
