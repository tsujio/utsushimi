#include "touch/contact_detector.h"
#include "behavior/body_behavior.h"
#include <cassert>
#include <cmath>
using namespace utsushimi;
uint32_t never(void *) { return UINT32_MAX; }
int main() {
    ContactDetector detector;
    BodyBehavior body({never,nullptr});
    for (unsigned i=0;i<40;++i) detector.update(38000,i*50);
    assert(detector.ready() && !detector.touching());
    bool holding=false;
    auto sample=[&](uint32_t raw,uint64_t ms) {
        detector.update(raw,ms);
        if (detector.touching()!=holding) {
            holding=detector.touching();
            body.input(holding ? BodyEvent::touch_started : BodyEvent::touch_ended);
        }
        body.advance(50000);
    };
    sample(40000,2000); sample(40000,2020);
    assert(body.state().valence==0);
    sample(40000,2100);
    assert(body.stimulus().holding && body.state().valence>0);
    for(uint64_t t=2150;t<=32100;t+=50) sample(40000,t);
    // 撫での即時加算ではなく、保持の積分だけで快を得る。
    assert(std::abs(body.state().valence-.4*(1-std::exp(-30.05/20)))<.0001);
    assert(std::abs(body.expression_openness()-.5f)<1e-6);
    const float before=body.state().valence;
    sample(38000,32150); sample(38000,32200); sample(38000,32250);
    assert(!body.stimulus().holding);
    for(uint64_t t=32300;t<=132250;t+=50) sample(38000,t);
    assert(body.state().valence<before*.01 && body.expression_openness()>.998);
}
