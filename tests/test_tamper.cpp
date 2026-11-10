#include "tamper_policy.h"
#include <cassert>
#include <cstdio>
static bool hit(TamperPolicy &p, uint32_t time, float deviation=0.46f, float gyro=0, bool armed=true) {
    float peak=0;
    return p.sample(1.1f,deviation,gyro,armed,time,peak);
}
int main() {
    assert(TAMPER_ACC_DEV_G==0.45f && TAMPER_GYRO_DPS==120.0f && TAMPER_SAMPLES==4);
    assert(!TamperPolicy::over(0.45f,120) && TamperPolicy::over(0.451f,0) && TamperPolicy::over(0,120.1f));
    TamperPolicy p;
    assert(!hit(p,0) && !hit(p,50) && !hit(p,100));
    assert(!hit(p,150,0.1f)); // A normal sample breaks the streak.
    assert(!hit(p,200) && !hit(p,250) && !hit(p,300) && hit(p,350));
    for(uint32_t t=400;t<3350;t+=50) assert(!hit(p,t));
    assert(hit(p,3350)); // Cooldown ends exactly at 3000 ms with a complete streak.
    p=TamperPolicy{};
    for(int i=0;i<8;++i) assert(!hit(p,i*50,1,200,false));
    assert(!hit(p,400,0,121) && !hit(p,450,0,121) && !hit(p,500,0,121) && hit(p,550,0,121));
    p=TamperPolicy{};
    for(int i=0;i<3;++i) assert(!hit(p,i*50));
    p.interrupted();assert(!hit(p,200) && !hit(p,250) && !hit(p,300) && hit(p,350));
    p=TamperPolicy{};
    uint32_t start=UINT32_MAX-150;
    assert(!hit(p,start) && !hit(p,start+50) && !hit(p,start+100) && hit(p,start+150));
    for(unsigned n=1;n<60;++n) assert(!hit(p,start+150+n*50));
    assert(hit(p,start+150+3000));
    puts("PASS tamper: thresholds, sustained samples, interruptions, disarmed gate, cooldown and rollover");
}
