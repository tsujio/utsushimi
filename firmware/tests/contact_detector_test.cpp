#include "touch/contact_detector.h"
#include <cassert>
using namespace utsushimi;
void calibrate(ContactDetector &d, uint32_t base=38000) {
    for (unsigned i=0;i<40;++i)
        assert(d.update(base,i*50)==(i==39 ? ContactUpdate::calibrated : ContactUpdate::none));
    assert(d.ready() && d.baseline()==base);
}
int main() {
    ContactDetector d; calibrate(d);
    // 単発ノイズ・ヒステリシス領域では開始しない。
    assert(d.update(41000,2000)==ContactUpdate::none);
    assert(d.update(38000,2050)==ContactUpdate::none);
    assert(d.update(38999,2100)==ContactUpdate::none);
    assert(d.update(39000,2150)==ContactUpdate::none);
    assert(d.update(39000,2200)==ContactUpdate::none);
    assert(d.update(39000,2250)==ContactUpdate::touch_started);
    for (uint64_t t=2300;t<10000;t+=50) assert(d.update(38800,t)==ContactUpdate::none);
    assert(d.touching() && d.baseline()==38000); // 長押しで基準値を更新しない。
    d.update(38600,10000); d.update(38600,10050);
    d.missing(); // 失敗を挟む継続判定は無効。
    assert(d.touching());
    assert(d.update(38600,10100)==ContactUpdate::none);
    assert(d.update(38600,10150)==ContactUpdate::none);
    assert(d.update(38600,10200)==ContactUpdate::touch_ended);
    assert(d.update(38000,10250)==ContactUpdate::none);
    d.update(40000,10300);
    assert(d.update(40000,11000)==ContactUpdate::none); // 長い更新停止で即確定しない。
    d.update(40000,11050);
    assert(d.update(40000,11100)==ContactUpdate::touch_started);

    ContactDetector noisy;
    for(unsigned i=0;i<40;++i)
        assert(noisy.update(i%2 ? 38000 : 40000,i*50)==(i==39 ? ContactUpdate::calibration_rejected : ContactUpdate::none));
    assert(!noisy.ready()); calibrate(noisy);
    ContactDetector zero;
    for(unsigned i=0;i<40;++i)
        assert(zero.update(0,i*50)==(i==39 ? ContactUpdate::calibration_rejected : ContactUpdate::none));
    assert(!zero.ready());
    ContactDetector interrupted;
    for(unsigned i=0;i<30;++i) interrupted.update(38000,i*50);
    interrupted.missing();
    for(unsigned i=0;i<39;++i) assert(interrupted.update(38000,2000+i*50)==ContactUpdate::none);
    assert(interrupted.update(38000,3950)==ContactUpdate::calibrated);
    // 計数の和は32bitを超えても安全。差分は符号付き。
    ContactDetector large; calibrate(large,UINT32_MAX);
    assert(large.delta(0)<0);
    ContactDetector timed;
    for(unsigned i=0;i<40;++i) assert(timed.update(38000,i)==ContactUpdate::none);
    assert(!timed.ready()); // サンプル数だけで2秒の校正を省略しない。
}
