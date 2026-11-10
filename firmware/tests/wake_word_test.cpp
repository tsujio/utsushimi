#include <cassert>
#include <cmath>
#include <cstdio>
#include "behavior/body_behavior.h"
#include "audio/pcm_decimator.h"
#include "audio/wake_result.h"
using namespace utsushimi;
uint32_t fixed(void *value) { return *static_cast<uint32_t *>(value); }
void advance(BodyBehavior &body, unsigned us) {
    while (us) { const auto step = std::min(us, 10000u); body.advance(step); us -= step; }
}
double response(double hz) {
    PcmDecimator converter;
    double energy = 0; unsigned count = 0, produced = 0;
    for (unsigned i = 0; i < 48000; ++i) {
        int16_t out;
        const int16_t in = std::lround(12000 * std::sin(2 * 3.141592653589793 * hz * i / 48000));
        if (converter.add(in, out)) {
            ++produced;
            if (i > 2400) { energy += double(out)*out; ++count; }
        }
    }
    assert(produced == 15958); // FIR warmup removes 42 output samples.
    return std::sqrt(energy/count)/(12000/std::sqrt(2.0));
}
int main() {
    static_assert(classify_wake_result(-1) == WakeResult::verified);
    static_assert(classify_wake_result(0) == WakeResult::none);
    static_assert(classify_wake_result(1) == WakeResult::detected);
    static_assert(classify_wake_result(2) == WakeResult::detected);
    static_assert(classify_wake_result(-2) == WakeResult::error);
    for (uint32_t direction : {0u, 1u}) {
        BodyBehavior body({fixed, &direction});
        BodyBehavior baseline({fixed, &direction});
        body.input(BodyEvent::wake_word_detected);
        assert(body.searching() && body.search_count() == 1 && body.search_gaze_x() == 0);
        const float side = direction ? 0.8f : -0.8f;
        advance(body, 150000); assert(std::abs(body.search_gaze_x() - side/2) < 1e-6);
        advance(body, 150000); assert(body.search_gaze_x() == side);
        advance(body, 200000); assert(std::abs(body.search_gaze_x()) < 1e-6);
        advance(body, 200000); assert(body.search_gaze_x() == -side);
        advance(body, 150000); assert(std::abs(body.search_gaze_x() + side/2) < 1e-6);
        advance(body, 150000); assert(!body.searching() && body.search_gaze_x() == 0);
        body.input(BodyEvent::wake_word_detected); assert(body.search_count() == 1);
        advance(body, 2000000);
        // Keep model time equal and compare blinking during a subsequent search.
        advance(baseline, 3000000);
        body.input(BodyEvent::wake_word_detected);
        bool blink = false;
        for (unsigned i = 0; i < 100; ++i) {
            body.advance(10000); baseline.advance(10000);
            assert(body.blink_openness() == baseline.blink_openness());
            blink |= body.blink_openness() < 1;
            for (int j=0;j<10;++j) body.input(BodyEvent::wake_word_detected);
            assert(body.search_count() == 2 && std::abs(body.search_gaze_x()) <= 0.8f);
        }
        // A guaranteed blink overlap is covered below using a known active blink.
        (void)blink;
        advance(body, 2000000);
        body.input(BodyEvent::wake_word_detected); advance(body, 150000);
        const auto x = body.search_gaze_x();
        body.input(BodyEvent::touch_started);
        assert(body.search_gaze_x() == x);
        advance(body, 150000); assert(body.touch_gaze_offset() == -0.3f);
        advance(body, 150000); assert(!body.searching() && body.search_gaze_x() == 0);
        advance(body, 4000000);
        body.input(BodyEvent::wake_word_detected); assert(!body.searching()); // holding wins even after reflex
        body.input(BodyEvent::touch_ended);
        body.input(BodyEvent::petting_detected);
        const float valence = body.state().valence;
        body.input(BodyEvent::wake_word_detected); assert(body.searching() && body.state().valence == valence);
        body.advance(10000000); assert(!body.searching() && body.search_gaze_x() == 0);
    }
    uint32_t random = 0;
    BodyBehavior touch_priority({fixed, &random});
    touch_priority.input(BodyEvent::touch_started);
    advance(touch_priority, 50000);
    touch_priority.input(BodyEvent::touch_ended);
    touch_priority.input(BodyEvent::wake_word_detected);
    assert(!touch_priority.searching()); // touch return still has priority
    advance(touch_priority, 350000);
    touch_priority.input(BodyEvent::wake_word_detected);
    assert(touch_priority.searching());
    BodyBehavior short_cooldown({fixed, &random}, {}, {1000000, 9.0f});
    short_cooldown.input(BodyEvent::wake_word_detected);
    advance(short_cooldown, 300000);
    assert(std::abs(short_cooldown.search_gaze_x()) == 1); // clamp geometry
    advance(short_cooldown, 700000);
    short_cooldown.input(BodyEvent::wake_word_detected);
    assert(short_cooldown.search_count() == 2); // adjustable 1s cooldown

    BodyBehavior blinking({fixed, &random});
    while (!blinking.action_count()) blinking.advance(10000);
    blinking.input(BodyEvent::wake_word_detected);
    advance(blinking, 50000);
    assert(blinking.searching() && blinking.blink_openness() < 1);
    AudioContinuity continuity;
    assert(!continuity.accept(0,0,1000,1000));
    assert(continuity.accept(1,0,6000,7000));
    assert(!continuity.accept(3,0,16000,17000)); // bounded queue lost block
    assert(continuity.accept(4,0,21000,22000));
    assert(!continuity.accept(5,1,26000,27000)); // DMA overflow/error
    assert(!continuity.accept(6,1,31000,400000)); // stale
    assert(continuity.accept(7,1,405000,406000));
    assert(!continuity.accept(8,1,500000,406000)); // bad timestamp
    int16_t frame[512]{};
    WakePcmInput stream(frame, 512);
    assert(!stream.begin(0, 0, 0, 0));
    assert(stream.begin(1, 0, 5000, 5000));
    for (int i=0;i<1000;++i) assert(!stream.add(20000)); // incomplete frame
    assert(!stream.begin(3, 0, 15000, 15000)); // lost block must discard partial frame + FIR
    assert(stream.begin(4, 0, 20000, 20000));
    int completed=0;
    for (int i=0;i<2000;++i) if(stream.add(-10000)) {
        ++completed;
        for(auto sample : frame) assert(std::abs(sample+10000)<=1);
    }
    assert(completed==1);
    assert(!stream.begin(5,1,25000,25000)); // DMA loss resets again
    for(int i=0;i<126;++i) assert(!stream.add(32767));
    PcmDecimator converter;
    int16_t out=0; unsigned outputs=0;
    for(int i=0;i<4800;++i) if(converter.add(10000,out)) {++outputs; assert(std::abs(out-10000)<=1);}
    assert(outputs==1558);
    for (int level : {-32768, 32767}) {
        PcmDecimator full_scale;
        for(int i=0;i<1000;++i) if(full_scale.add(static_cast<int16_t>(level),out))
            assert(std::abs(int(out)-level)<=1);
    }
    converter.reset();
    for(int i=0;i<126;++i) assert(!converter.add(-20000,out));
    for(int i=0;i<3;++i) if(converter.add(-20000,out)) assert(std::abs(out+20000)<=1);
    for (double hz : {100.0, 1000.0, 4000.0, 5000.0}) assert(std::abs(response(hz)-1)<0.01);
    for (double hz : {8000.0, 8100.0, 9000.0, 10000.0, 12000.0, 16000.0, 20000.0, 23900.0}) assert(response(hz)<0.001);
    std::puts("Wake search, blink/touch coexistence, audio continuity and FIR tests passed");
}
