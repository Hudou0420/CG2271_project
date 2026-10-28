#pragma once
#include "runtime.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
using std::min;
using std::max;
class String : public std::string {
public:
    using std::string::string;
    using std::string::operator=;
    String()=default;
    String(const std::string &s):std::string(s) {}
    explicit String(unsigned value):std::string(std::to_string(value)) {}
    String &operator=(const char *s) {std::string::operator=(s?s:"");return *this;}
    void concat(const char *data,size_t size) {append(data,size);}
    size_t write(uint8_t byte) {push_back((char)byte);return 1;}
    size_t write(const uint8_t *data,size_t size) {append((const char *)data,size);return size;}
    size_t position_=0;
    int read() {return position_<size()?(uint8_t)(*this)[position_++]:-1;}
    size_t readBytes(char *out,size_t count) {count=min(count,size()-position_);memcpy(out,data()+position_,count);position_+=count;return count;}
    bool startsWith(const char *prefix) const {return rfind(prefix,0)==0;}
    String substring(size_t start,size_t end=std::string::npos) const {return substr(start,end==npos?npos:end-start);}
    void trim() {size_t start=find_first_not_of(" \r\n\t"),end=find_last_not_of(" \r\n\t");*this=start==npos?"":substr(start,end-start+1);}
};
struct TestESP {
    size_t free=100000,largest=50000,minimum=25000;
    size_t getFreeHeap() const {return free;}
    size_t getMaxAllocHeap() const {return largest;}
    size_t getMinFreeHeap() const {return minimum;}
};
inline TestESP ESP;
template<class T> T constrain(T value,T low,T high) {return min(max(value,low),high);}
constexpr int SERIAL_8N1=0;
inline uint32_t millis() {return test_now;}
inline void delay(uint32_t ms) {vTaskDelay(ms);}
inline uint32_t esp_random() {return 0x2271;}
inline size_t strlcpy(char *out,const char *in,size_t n) {
    size_t len=strlen(in);if(n){size_t count=min(n-1,len);memcpy(out,in,count);out[count]=0;}return len;
}
struct TestSerial {
    std::deque<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
    void setRxBufferSize(size_t size) {(void)size;}
    void begin(unsigned baud,int mode=0,int rx=0,int tx=0) {(void)baud;(void)mode;(void)rx;(void)tx;}
    int available() {return (int)incoming.size();}
    int read() {if(incoming.empty())return -1;int value=incoming.front();incoming.pop_front();return value;}
    size_t write(const uint8_t *data,size_t size) {outgoing.insert(outgoing.end(),data,data+size);return size;}
    void printf(const char *format,...) {(void)format;}
    void print(const char *text) {(void)text;}
    void println(const char *text="") {(void)text;}
};
inline TestSerial Serial,Serial1;
