#include "behavior/body_behavior.h"
#include "display/frame_timing.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using namespace utsushimi;
namespace {
struct Rng {
    uint32_t state;
    static uint32_t next(void *context) {
        auto &x = static_cast<Rng *>(context)->state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return x;
    }
    RandomSource source() { return {next, this}; }
};
uint32_t never(void *) { return UINT32_MAX; }
void near(double a, double b, double tolerance = 0.0001) { assert(std::abs(a-b) < tolerance); }
void advance(BodyBehavior &b, uint64_t total, uint64_t chunk = 10000) {
    while (total) { auto dt = std::min(total, chunk); assert(!b.advance(dt)); total -= dt; }
}
void units() {
    BehaviorParameters p;
    assert(p.valid());
    BodyState s{0.7f};
    update_body(s, {}, p, 1);
    near(s.valence, .7 * std::exp(-1.0/20));
    s = {-0.7f}; update_body(s, {}, p, 1);
    near(s.valence, -.7 * std::exp(-1.0/20));
    s = {}; update_body(s, {true}, p, 1);
    near(s.valence, .4 * (1-std::exp(-1.0/20)));
    auto strong = p; strong.holding_sensitivity = 1;
    for (int i=0; i<1000; ++i) update_body(s, {true}, strong, .01f);
    assert(s.valence == 1);
    Expression e; e.advance(1, .2f);
    near(e.openness(), .5+.5*(1-.896));
    Expression negative; negative.advance(-1, 1); near(negative.openness(), 1);
    BodyBehavior b({never, nullptr});
    assert(!b.used_default_parameters());
    b.input(BodyEvent::petting_detected); near(b.state().valence, .35);
    near(b.expression_openness(), 1); // イベントからアニメーションを直接起動しない。
    for (int i=0; i<10; ++i) b.input(BodyEvent::petting_detected);
    assert(b.state().valence == 1);
    b.input(BodyEvent::touch_started); b.input(BodyEvent::touch_started);
    assert(b.stimulus().holding);
    b.input(BodyEvent::touch_ended); assert(!b.stimulus().holding);
    advance(b, 1000000); near(b.expression_openness(), .5);
    advance(b, 600000000); near(b.state().valence, 0); near(b.expression_openness(), 1);

    near(expected_blink_duration(.15f), .175);
    const double scale = blink_wait_scale(5, .15f);
    near(scale, 3.825/std::tgamma(1.5), 1e-12);
    near(blink_wait_scale(5, .2f), (5-.23-1)/std::tgamma(1.5), 1e-12);
    for (double t : {0., 1., 5., 20.}) {
        const auto prob = weibull_blink_probability(t, .01, scale);
        near(prob, 1-std::exp(-(std::pow((t+.01)/scale,2)-std::pow(t/scale,2))), 1e-12);
        near(weibull_blink_probability(t,.02,scale),
             1-(1-prob)*(1-weibull_blink_probability(t+.01,.01,scale)), 1e-12);
    }
    assert(weibull_blink_probability(2,.01,scale) > weibull_blink_probability(1,.01,scale));
    assert(weibull_blink_probability(0,0,scale) == 0);
    assert(weibull_blink_probability(1e10, .01, scale) == 1);
    for (float invalid : {0.f, -1.f, 1.f, 3601.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        auto q=p; q.blink_interval=invalid; assert(!q.valid());
        BodyBehavior fallback({never,nullptr},q);
        assert(fallback.used_default_parameters()); near(fallback.parameters().blink_interval,5);
        q=p; q.blink_duration=invalid;
        if (invalid != 1.f) assert(!q.valid());
    }
    // 境界の両側で正の待機時間を厳密に検証する。
    auto q=p;
    const float boundary = static_cast<float>(expected_blink_duration(p.blink_duration)+1);
    q.blink_interval=std::nextafter(boundary, 0.f); assert(!q.valid());
    q.blink_interval=std::nextafter(boundary, 2.f); assert(q.valid());
    q=p; q.blink_duration=.9f; q.blink_interval=2;
    assert(expected_blink_duration(q.blink_duration)==1 && !q.valid());
    q.blink_interval=std::nextafter(2.f,3.f); assert(q.valid());
    BodyBehavior paused({never,nullptr}), control({never,nullptr});
    paused.advance(5000); control.advance(5000);
    assert(paused.advance(UINT64_MAX)); control.advance(100000);
    assert(paused.now_us()==100000 && paused.now_us()==control.now_us());
    paused.advance(5000); control.advance(5000);
    assert(paused.now_us()==110000 && paused.now_us()==control.now_us());
}
struct Forced {
    bool twice; unsigned calls=0;
    static uint32_t next(void *ctx) {
        auto &r=*static_cast<Forced *>(ctx);
        return (++r.calls % 2) ? 0 : (r.twice ? 0 : UINT32_MAX);
    }
};
void touch_gaze() {
    BodyBehavior b({never, nullptr});
    assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_ended);
    assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_started);
    assert(b.touch_gaze_offset() == 0 && b.state().valence == 0);
    advance(b, 75000); near(b.touch_gaze_offset(), -.15);
    advance(b, 75000); near(b.touch_gaze_offset(), -.3);
    advance(b, 2999999); near(b.touch_gaze_offset(), -.3);
    advance(b, 1); near(b.touch_gaze_offset(), -.3);
    advance(b, 175000); near(b.touch_gaze_offset(), -.15);
    advance(b, 175000); assert(b.touch_gaze_offset() == 0);
    advance(b, 1000000); assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_started); // 重複イベントでも保持中は再発火しない。
    advance(b, 150000); assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_ended);
    advance(b, 150000); assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_started);
    advance(b, 75000); near(b.touch_gaze_offset(), -.15);
    b.input(BodyEvent::touch_ended);
    near(b.touch_gaze_offset(), -.15); // 上昇途中でも位置が飛ばない。
    b.input(BodyEvent::touch_started); // 復帰中の再接触は捨て、予約もしない。
    advance(b, 175000); near(b.touch_gaze_offset(), -.075);
    b.input(BodyEvent::touch_ended); // 復帰を再起動しない。
    b.input(BodyEvent::touch_started);
    advance(b, 175000); assert(b.touch_gaze_offset() == 0);
    advance(b, 150000); assert(b.touch_gaze_offset() == 0);
    b.input(BodyEvent::touch_ended);
    b.input(BodyEvent::touch_started);
    advance(b, 150000); near(b.touch_gaze_offset(), -.3); // 再受付で全移動量に戻る。
    advance(b, 1000000); near(b.touch_gaze_offset(), -.3);
    b.input(BodyEvent::touch_ended);
    near(b.touch_gaze_offset(), -.3);
    advance(b, 175000); near(b.touch_gaze_offset(), -.15);
    advance(b, 175000); assert(b.touch_gaze_offset() == 0);

    // 開始直後、到達時、保持期限の前後、自然復帰中、完了時の離脱。
    for (uint64_t release : {0ULL, 150000ULL, 3149999ULL, 3150000ULL, 3325000ULL, 3500000ULL}) {
        BodyBehavior released({never, nullptr});
        released.input(BodyEvent::touch_started);
        advance(released, release);
        const float position = released.touch_gaze_offset();
        released.input(BodyEvent::touch_ended);
        assert(released.touch_gaze_offset() == position);
        if (release < 3150000) {
            advance(released, 175000); near(released.touch_gaze_offset(), position * .5f);
            released.input(BodyEvent::touch_ended); // 重複離脱でも期限を維持。
            advance(released, 175000);
        } else {
            advance(released, 3500000 - release);
        }
        assert(released.touch_gaze_offset() == 0);
    }

    BodyBehavior skipped({never, nullptr}), fine({never, nullptr});
    skipped.input(BodyEvent::touch_started); fine.input(BodyEvent::touch_started);
    assert(skipped.advance(3325000)); advance(fine, 3325000, 37000);
    near(skipped.touch_gaze_offset(), -.15);
    assert(skipped.touch_gaze_offset() == fine.touch_gaze_offset());
    assert(skipped.advance(UINT64_MAX)); assert(skipped.touch_gaze_offset() == 0);

    // 保持刺激を0にして視線自体がvalence・表情・瞬き系列を変えないことを確認。
    BehaviorParameters p; p.holding_sensitivity = 0;
    Forced r1{true}, r2{true};
    BodyBehavior moving({Forced::next, &r1}, p), control({Forced::next, &r2}, p);
    moving.input(BodyEvent::petting_detected); control.input(BodyEvent::petting_detected);
    moving.input(BodyEvent::touch_started);
    for (int i = 1; i <= 400; ++i) {
        moving.advance(10000); control.advance(10000);
        assert(moving.state().valence == control.state().valence);
        assert(moving.expression_openness() == control.expression_openness());
        assert(moving.blink_openness() == control.blink_openness());
        assert(moving.action_count() == control.action_count());
        assert(moving.refractory_until_us() == control.refractory_until_us());
        if (i == 6) { near(moving.blink_openness(), 0); assert(moving.touch_gaze_offset() < 0); }
        if (i == 31) { near(moving.blink_openness(), 0); near(moving.touch_gaze_offset(), -.3); }
        if (i == 350) assert(moving.touch_gaze_offset() == 0);
    }
    assert(r1.calls == r2.calls);
}
void display_timing() {
    const int64_t epoch = 9000000000000LL;
    int64_t now = epoch;
    // 1時間分の期限に切り捨て誤差が累積しない。
    for (int i = 1; i <= 30 * 3600; ++i) {
        const auto next = next_display_frame_us(epoch, now);
        assert(next - now == 33333 || next - now == 33334);
        now = next;
        if (i % 30 == 0) assert(now - epoch == int64_t(i / 30) * 1000000);
    }
    // tick丸め相当の遅延・処理超過・長いアイドル後も未来の枠へ進む。
    assert(next_display_frame_us(epoch, epoch + 40000) == epoch + 66667);
    assert(next_display_frame_us(epoch, epoch + 110000) == epoch + 133334);
    assert(next_display_frame_us(epoch, epoch + 3600000000LL) == epoch + 3600033334LL);
    // 表示側が30Hzで読み出しても、表情は10ms更新で250/400msに完了する。
    Expression expression;
    int64_t display_at = 0;
    for (int ms = 10; ms <= 650; ms += 10) {
        expression.advance(ms <= 250 ? 1 : 0, .01f);
        if (ms * 1000 >= display_at) {
            const auto openness = expression.openness();
            assert(openness >= .5f && openness <= 1);
            display_at = next_display_frame_us(0, ms * 1000);
        }
        if (ms == 240) assert(expression.openness() > .5f);
        if (ms == 250) assert(expression.openness() == .5f);
        if (ms == 640) assert(expression.openness() < 1);
        if (ms == 650) assert(expression.openness() == 1);
    }
}
void timing() {
    for (bool twice : {false,true}) {
        Forced rng{twice}; BodyBehavior b({Forced::next,&rng});
        b.advance(10000); assert(b.action_count()==1 && b.last_start_us()==10000);
        assert(b.double_count()==unsigned(twice));
        const uint64_t end=10000+(twice ? 400000 : 150000);
        assert(b.refractory_until_us()==end+1000000);
        while (b.now_us()<b.refractory_until_us()) {
            b.advance(10000);
            assert(b.action_count()==1 && rng.calls==2);
            const auto offset=b.now_us()-10000;
            if (offset==50000 || (twice && offset==300000)) near(b.blink_openness(),0);
            if (offset==150000 || (twice && offset==250000)) near(b.blink_openness(),1);
        }
        b.advance(10000);
        assert(b.action_count()==2 && b.last_start_us()==end+1010000);
        // 長い停止でも、最大100msだけ進め、過去の動作をまとめて開始しない。
        const auto count=b.action_count(); assert(b.advance(3600000000)); assert(b.action_count()==count);
    }
    // 10ms格子に乗らない終了時刻でも、不応期を短縮しない。
    Forced rng{false}; AutomaticBlink b({Forced::next,&rng},5,.153123f);
    b.advance(10000);
    assert(b.refractory_until_us()==1163123);
    for (uint64_t t=20000;t<=1160000;t+=10000) b.advance(t);
    assert(b.action_count()==1 && rng.calls==2);
    b.advance(1170000); assert(b.action_count()==2);
}
void reproducibility() {
    Rng r1{12345},r2{12345},r3{12345},r4{23456};
    BodyBehavior a(r1.source()),b(r2.source()),pleasant(r3.source()),other(r4.source());
    pleasant.input(BodyEvent::touch_started);
    for (int i=0;i<1000;++i) {
        pleasant.input(BodyEvent::petting_detected);
        advance(a,1000000,5000); advance(b,1000000,37000);
        advance(pleasant,1000000,20000); advance(other,1000000);
        assert(a.state().valence==b.state().valence && a.expression_openness()==b.expression_openness());
        assert(a.last_start_us()==b.last_start_us() && a.blink_openness()==b.blink_openness());
        assert(a.last_start_us()==pleasant.last_start_us() && a.double_count()==pleasant.double_count());
        assert(a.action_count()==pleasant.action_count() && a.blink_openness()==pleasant.blink_openness());
        assert(pleasant.state().valence>0 && a.state().valence==0);
    }
    assert(r1.state==r2.state && r1.state==r3.state && r1.state!=r4.state);
}
struct Stats {
    double sum=0,square=0; unsigned doubles=0; std::vector<double> waits;
    void add(double interval,double wait,bool twice) {
        sum+=interval; square+=interval*interval; doubles+=twice; waits.push_back(wait);
    }
    double mean() const { return sum/waits.size(); }
    double sd() const { return std::sqrt(square/waits.size()-mean()*mean()); }
    double ratio() const { return double(doubles)/waits.size(); }
    double ks(double scale) {
        std::sort(waits.begin(),waits.end()); double d=0;
        for (size_t i=0;i<waits.size();++i) {
            const double f=-std::expm1(-std::pow(waits[i]/scale,2));
            d=std::max({d,std::abs(f-double(i)/waits.size()),std::abs(f-double(i+1)/waits.size())});
        }
        return d;
    }
};
void simulation() {
    Stats pooled; const double scale=blink_wait_scale(5,.15f);
    double means_square=0,means_sum=0;
    for (unsigned i=0;i<8;++i) {
        Rng rng{9001+i}; BodyBehavior b(rng.source()); Stats s;
        uint64_t seen=0,previous=0,allowed=0,doubles=0,first=0;
        const uint64_t chunks[]={5000,10000,20000,37000};
        while (s.waits.size()<10000) {
            b.advance(chunks[i%4]);
            if (b.action_count()==seen) continue;
            const auto start=b.last_start_us();
            if (seen) {
                assert(start>allowed);
                const double interval=(start-previous)/1e6, wait=(start-allowed)/1e6;
                const bool twice=b.double_count()!=doubles;
                s.add(interval,wait,twice); pooled.add(interval,wait,twice);
            } else first=start;
            previous=start; allowed=b.refractory_until_us(); seen=b.action_count(); doubles=b.double_count();
        }
        const double ks=s.ks(scale);
        std::printf("seed=%u chunk_us=%llu n=%zu observation=%.2fs mean=%.6fs sd=%.6fs double=%.5f KS=%.6f\n",
            9001+i,(unsigned long long)chunks[i%4],s.waits.size(),(previous-first)/1e6,s.mean(),s.sd(),s.ratio(),ks);
        assert(std::abs(s.mean()-5)<.15 && std::abs(s.ratio()-.1)<.015 && ks<.03);
        means_sum+=s.mean(); means_square+=s.mean()*s.mean();
    }
    const double ks=pooled.ks(scale);
    const double theoretical_sd=std::sqrt(scale*scale*(1-std::acos(-1)/4)+.005625);
    std::printf("pooled n=%zu mean=%.6fs (theory=5, tolerance=.10s) sd=%.6fs (theory=%.6f) double=%.6f (theory=.1) KS=%.6f (limit=.015) seed_mean_sd=%.6fs scale=%.9fs\n",
        pooled.waits.size(),pooled.mean(),pooled.sd(),theoretical_sd,pooled.ratio(),ks,std::sqrt(means_square/8-std::pow(means_sum/8,2)),scale);
    for (double p : {.1,.5,.9}) std::printf("waiting q%.0f measured=%.6fs theory=%.6fs\n",p*100,
        pooled.waits[size_t(p*pooled.waits.size())],scale*std::sqrt(-std::log1p(-p)));
    assert(std::abs(pooled.mean()-5)<.1 && std::abs(pooled.ratio()-.1)<.01 && ks<.015);
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    units(); touch_gaze(); display_timing(); timing(); reproducibility(); simulation();
}
