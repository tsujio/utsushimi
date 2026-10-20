#include "touch/contact_detector.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
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
    // 異なる基準値・同時接触・電極ごとの欠落が相互に干渉しない。
    ContactDetector electrodes[3];
    const uint32_t bases[] = {38000, 24000, 51000};
    for (unsigned i=0;i<3;++i) calibrate(electrodes[i], bases[i]);
    for (uint64_t t=2000;t<=2100;t+=50) {
        auto a=electrodes[0].update(bases[0]+1800,t);
        auto b=electrodes[1].update(bases[1],t);
        auto c=electrodes[2].update(bases[2]+1800,t);
        assert(a==(t==2100 ? ContactUpdate::touch_started : ContactUpdate::none));
        assert(b==ContactUpdate::none);
        assert(c==a);
    }
    electrodes[1].missing();
    for (uint64_t t=2150;t<=2250;t+=50) {
        auto a=electrodes[0].update(bases[0],t);
        auto b=electrodes[1].update(bases[1]+1800,t);
        assert(electrodes[2].update(bases[2]+1800,t)==ContactUpdate::none);
        assert(a==(t==2250 ? ContactUpdate::touch_ended : ContactUpdate::none));
        assert(b==(t==2250 ? ContactUpdate::touch_started : ContactUpdate::none));
    }
    assert(!electrodes[0].touching() && electrodes[1].touching() && electrodes[2].touching());
    for (unsigned i=0;i<3;++i) assert(electrodes[i].baseline()==bases[i]);


    // 6分で±720カウントの緩やかなドリフト。接触閾値の余裕を保つ。
    for (int direction : {-1, 1}) {
        ContactDetector drift; calibrate(drift);
        for (uint64_t t=2000;t<=362000;t+=50) {
            const uint32_t raw=38000 + direction * static_cast<int>((t-2000)/500);
            assert(drift.update(raw,t)==ContactUpdate::none);
            assert(!drift.touching());
        }
        const uint32_t current=38000+direction*720;
        assert(std::abs(drift.delta(current))<50);
        const double frozen=drift.baseline();
        for(uint64_t t=362050;t<=392050;t+=50) drift.update(current+1800,t);
        assert(drift.touching() && drift.baseline()==frozen);
        drift.update(current,392100); drift.update(current,392150);
        assert(drift.update(current,392200)==ContactUpdate::touch_ended);
        for(uint64_t t=392250;t<=394150;t+=50) drift.update(current,t);
        assert(drift.baseline()==frozen); // 離脱直後は凍結
        drift.missing();
        drift.update(current,400000);
        assert(drift.baseline()==frozen); // 停止時間をまとめて積分しない
    }
    ContactDetector guarded; calibrate(guarded);
    for(uint64_t t=2000;t<10000;t+=50) guarded.update(38700,t);
    assert(guarded.baseline()==38000); // 閾値未満でも近接らしい増加は取り込まない
    for(uint64_t t=10000;t<14000;t+=50) guarded.update(0,t);
    assert(guarded.baseline()==38000);
    for(uint64_t t=14000;t<20000;t+=50) guarded.update(t%100 ? 38300 : 38000,t);
    assert(guarded.baseline()==38000); // 急変が続く間は追従しない
}
