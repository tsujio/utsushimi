#include "touch/contact_detector.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <cstdio>
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
    assert(d.update(39000,2199)==ContactUpdate::none);
    assert(d.update(39000,2200)==ContactUpdate::touch_started);
    for (uint64_t t=2300;t<10000;t+=50) assert(d.update(38800,t)==ContactUpdate::none);
    assert(d.touching() && d.baseline()==38000); // 安定10秒未満では更新しない。
    d.update(38600,10000); d.update(38600,10020);
    d.missing(); // 失敗を挟む継続判定は無効。
    assert(d.touching());
    assert(d.update(38600,10100)==ContactUpdate::none);
    assert(d.update(38600,10149)==ContactUpdate::none);
    assert(d.update(38600,10150)==ContactUpdate::touch_ended);
    assert(d.update(38000,10250)==ContactUpdate::none);
    d.update(40000,10300);
    assert(d.update(40000,11000)==ContactUpdate::none); // 長い更新停止で即確定しない。
    d.update(40000,11020);
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
    for (uint64_t t=2000;t<=2050;t+=50) {
        auto a=electrodes[0].update(bases[0]+1800,t);
        auto b=electrodes[1].update(bases[1],t);
        auto c=electrodes[2].update(bases[2]+1800,t);
        assert(a==(t==2050 ? ContactUpdate::touch_started : ContactUpdate::none));
        assert(b==ContactUpdate::none);
        assert(c==a);
    }
    electrodes[1].missing();
    for (uint64_t t=2150;t<=2200;t+=50) {
        auto a=electrodes[0].update(bases[0],t);
        auto b=electrodes[1].update(bases[1]+1800,t);
        assert(electrodes[2].update(bases[2]+1800,t)==ContactUpdate::none);
        assert(a==(t==2200 ? ContactUpdate::touch_ended : ContactUpdate::none));
        assert(b==(t==2200 ? ContactUpdate::touch_started : ContactUpdate::none));
    }
    assert(!electrodes[0].touching() && electrodes[1].touching() && electrodes[2].touching());
    for (unsigned i=0;i<3;++i) assert(electrodes[i].baseline()==bases[i]);


    // 接触/非接触・大小・正負で同じ40秒EMA。待機終了まで完全に凍結。
    for (int offset : {300, -300, 2000, -2000, 10000, -10000}) {
        ContactDetector follow; calibrate(follow);
        const uint32_t raw = 38000 + offset;
        unsigned starts = 0, ends = 0;
        uint64_t ended_at = 0;
        for (uint64_t t=2000;t<=172000;t+=50) {
            auto event=follow.update(raw,t);
            if (event==ContactUpdate::touch_started) ++starts;
            if (event==ContactUpdate::touch_ended) { ++ends; ended_at=t; }
            const double elapsed = t>12000 ? (t-12000)/1000.0 : 0;
            const double expected=raw-offset*std::exp(-elapsed/40);
            assert(std::abs(follow.baseline()-expected)<1e-7);
            if (t<=12000) assert(follow.baseline()==38000);
        }
        assert(starts==(offset>=1000 ? 1u : 0u));
        assert(ends==starts && !follow.touching());
        if (offset==2000) {
            // 40 ln(2000/600)=48.159秒 + 10秒待機 + 50msデバウンス。
            assert(ended_at==60250);
            std::printf("Contact: delta 2000 ended after %.2fs (including debounce)\n", (ended_at-2000)/1000.0);
        }
    }
    // ±2 counts/sの連続ドリフトは固定起点から遠ざかっても追従。
    for (int direction : {-1, 1}) {
        ContactDetector drift; calibrate(drift);
        for (uint64_t t=2000;t<=362000;t+=50) {
            const uint32_t raw=38000 + direction * static_cast<int>((t-2000)/500);
            assert(drift.update(raw,t)==ContactUpdate::none);
        }
        assert(std::abs(drift.delta(38000+direction*720))<81);
    }
    // 隣接差だけでは見逃すランプ状の揺れも短期EMAとの差で拒否。
    for (bool ramp : {false, true}) {
        ContactDetector noisy; calibrate(noisy);
        for (uint64_t t=2000;t<62000;t+=50) {
            const int phase=((t-2000)/50)%80;
            const int noise=ramp ? (phase<40 ? phase : 80-phase)*50 : (phase%2)*300;
            noisy.update(38000+noise,t);
            assert(noisy.baseline()==38000);
        }
    }
    // 許容ノイズ(±30)があっても追従する。
    ContactDetector noise; calibrate(noise);
    for (uint64_t t=2000;t<=52000;t+=50) noise.update(t%100 ? 40030 : 39970,t);
    assert(noise.baseline()>39200 && noise.baseline()<39300);

    // 追従中の欠落・0・単発外れ値・時刻異常で凍結し、復帰後10秒待つ。
    for (unsigned fault=0;fault<6;++fault) {
        ContactDetector recovery; calibrate(recovery);
        for (uint64_t t=2000;t<=22000;t+=50) recovery.update(40000,t);
        const double frozen=recovery.baseline();
        uint64_t resume=22100;
        if (fault==0) recovery.missing();
        if (fault==1) recovery.update(0,22050);
        if (fault==2) recovery.update(UINT32_MAX,22050);
        if (fault==3) resume=30000;
        if (fault==4) recovery.update(40000,22000);
        if (fault==5) recovery.update(40000,21000);
        assert(recovery.baseline()==frozen);
        for(uint64_t t=resume;t<=resume+10000;t+=50) {
            recovery.update(40000,t);
            assert(recovery.baseline()==frozen);
        }
        recovery.update(40000,resume+10050);
        assert(std::abs(recovery.baseline()-(frozen+(40000-frozen)*-std::expm1(-.05/40)))<1e-8);
    }
}
