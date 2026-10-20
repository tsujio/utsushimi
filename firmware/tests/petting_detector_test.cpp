#include "touch/petting_detector.h"
#include "behavior/body_behavior.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace utsushimi;
struct Trial {
    PettingDetector detector;
    uint64_t now = 0;
    unsigned events = 0;
    PettingDirection last = PettingDirection::none;
    void hold(uint8_t mask, unsigned ticks = 3) {
        for (unsigned i=0; i<ticks; ++i,now+=50) {
            auto result=detector.update(mask,now);
            if (result!=PettingDirection::none) { ++events; last=result; }
        }
    }
    Trial() { hold(0); }
};
uint32_t never(void *) { return UINT32_MAX; }
int main() {
    Trial forward;
    forward.hold(1); forward.hold(3); forward.hold(2); forward.hold(6);
    assert(forward.events==1 && forward.last==PettingDirection::a_to_c);
    forward.hold(4,100); assert(forward.events==1);
    forward.hold(0); forward.hold(4); forward.hold(6); forward.hold(2); forward.hold(3);
    assert(forward.events==2 && forward.last==PettingDirection::c_to_a);
    Trial separated;
    separated.hold(1); separated.hold(0); separated.hold(2); separated.hold(0); separated.hold(4);
    assert(separated.events==1);
    for (auto sequence : {1,2,3,4}) {
        Trial invalid;
        if (sequence==1) { invalid.hold(7); }
        if (sequence==2) { invalid.hold(1); invalid.hold(3); invalid.hold(7); }
        if (sequence==3) { invalid.hold(1); invalid.hold(2); invalid.hold(1); invalid.hold(4); }
        if (sequence==4) { invalid.hold(2); invalid.hold(4); invalid.hold(1); }
        assert(invalid.events==0);
    }
    Trial fast;
    fast.hold(1,1); fast.hold(2,1); fast.hold(4);
    assert(fast.events==0);
    Trial boundary;
    boundary.hold(1,2); boundary.hold(2,2); boundary.hold(4);
    assert(boundary.events==1);
    Trial slow;
    slow.hold(1,20); slow.hold(2,20); slow.hold(4);
    assert(slow.events==1); // 1000ms per step, 2000ms total inclusive
    Trial timeout;
    timeout.hold(1,21); timeout.hold(2); timeout.hold(4);
    assert(timeout.events==0);
    for (unsigned failure=0;failure<3;++failure) {
        Trial interrupted;
        interrupted.hold(1);
        if(failure==0) interrupted.detector.missing();
        if(failure==1) interrupted.now+=200;
        if(failure==2) interrupted.now=0;
        interrupted.hold(2); interrupted.hold(4);
        assert(interrupted.events==0);
        interrupted.hold(0); interrupted.hold(1); interrupted.hold(2); interrupted.hold(4);
        assert(interrupted.events==1);
    }
    Trial rearm;
    rearm.hold(1); rearm.hold(2); rearm.hold(4);
    rearm.hold(0,1); rearm.hold(1); rearm.hold(2); rearm.hold(4);
    assert(rearm.events==1);
    BodyBehavior body({never,nullptr});
    body.input(BodyEvent::petting_detected);
    assert(std::abs(body.state().valence-.35f)<1e-6);
    body.advance(200000);
    assert(body.expression_openness()<1);
    assert(body.state().valence>0 && body.state().valence<.35f);
    puts("Petting: directions, overlap, rejection, timing, rearm, missing and body response passed");
}
