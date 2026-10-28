#pragma once
#include "NetworkClientSecure.h"
#include <functional>
#include <map>
struct HttpPlan {
    int code=200;
    String body="null";
    int advertised=-2;
    std::function<void()> during;
};
struct HttpCall {String method,url,body;std::map<std::string,std::string> headers;};
inline std::deque<HttpPlan> test_http_plans;
inline std::vector<HttpCall> test_http_calls;
inline bool test_http_begin_ok=true;
inline unsigned test_http_objects;
class HTTPClient {
    NetworkClientSecure *client_=nullptr;
    String url_,body_;
    size_t position_=0;
    int advertised_=-2;
    std::map<std::string,std::string> headers_;
public:
    HTTPClient() {++test_http_objects;}
    ~HTTPClient() {end();--test_http_objects;}
    void setReuse(bool value) {assert(!value);}
    void setConnectTimeout(unsigned ms) {assert(ms>0 && ms<=8000);}
    void setTimeout(unsigned ms) {assert(ms>0 && ms<=8000);}
    void useHTTP10(bool value) {(void)value;}
    bool begin(NetworkClientSecure &client,const String &url) {client_=&client;url_=url;return test_http_begin_ok;}
    void addHeader(const String &key,const String &value) {headers_[key]=value;}
    int sendRequest(const char *method,const String &body) {
        assert(client_ && !test_http_plans.empty());
        client_->open();
        HttpPlan plan=test_http_plans.front();test_http_plans.pop_front();
        test_http_calls.push_back({method,url_,body,headers_});
        body_=plan.body;advertised_=plan.advertised;position_=0;
        if(plan.during)plan.during();
        return plan.code;
    }
    int POST(const String &body) {return sendRequest("POST",body);}
    String getString() {return body_;}
    int getSize() const {return advertised_==-2?(int)body_.size():advertised_;}
    HTTPClient *getStreamPtr() {return this;}
    int available() const {return (int)(body_.size()-position_);}
    size_t readBytes(char *buffer,size_t size) {size=min(size,body_.size()-position_);memcpy(buffer,body_.data()+position_,size);position_+=size;return size;}
    bool connected() const {return position_<body_.size();}
    void end() {if(client_)client_->stop();client_=nullptr;}
    static String errorToString(int code) {(void)code;return "fixture transport error";}
};
