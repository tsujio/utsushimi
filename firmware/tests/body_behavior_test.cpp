#include "behavior/body_behavior.h"
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
void near(float a, float b, float tolerance = 0.0001f) { assert(std::abs(a - b) < tolerance); }
void advance(BodyBehavior &b, uint64_t total, uint64_t chunk = 10000) {
    while (total) {
        const auto dt = std::min(total, chunk);
        assert(!b.advance(dt));
        total -= dt;
    }
}
void unit_tests() {
    BehaviorParameters p;
    assert(p.valid());
    BodyState s;
    for (int i = 0; i < 500; ++i) update_body(s, {}, p, 0.01f);
    near(s.eye_dryness, 0.6f);
    assert(s.valence == 0);
    for (int i = 0; i < 10000; ++i) update_body(s, {}, p, 0.01f);
    assert(s.valence < -0.5f && s.valence >= -1 && s.eye_dryness <= 1);
    s = {0, 0.7f};
    for (int i = 0; i < 100; ++i) update_body(s, {}, p, 0.01f);
    near(s.valence, 0.7f * std::exp(-1.0f / 20));
    s = {0, -0.7f};
    update_body(s, {}, p, 1);
    assert(s.valence > -0.7f && s.valence < 0);
    s = {};
    for (int i = 0; i < 100; ++i) update_body(s, {true}, p, 0.01f);
    near(s.valence, 0.4f * (1 - std::exp(-1.0f / 20)));

    BodyBehavior b({never, nullptr});
    b.input(BodyEvent::petting_detected);
    near(b.state().valence, 0.35f);
    for (int i = 0; i < 10; ++i) b.input(BodyEvent::petting_detected);
    assert(b.state().valence == 1);
    b.input(BodyEvent::touch_started);
    b.input(BodyEvent::touch_started); // 保持は重複イベントでも加算しない。
    assert(b.stimulus().holding);
    b.input(BodyEvent::touch_ended);
    assert(!b.stimulus().holding);
    assert(b.state().valence == 1); // 保持自体を撫でとしない。
    advance(b, 1000000);
    assert(b.expression_openness() < 0.6f);
    near(b.openness(), b.expression_openness());
    advance(b, 600000000);
    assert(b.state().valence >= -1 && b.state().eye_dryness <= 1);
    near(b.expression_openness(), 1);
    near(eye_moisture_need(0.3f), 0);
    near(eye_moisture_need(1), 1);
    near(eye_moisture_need(0.65f), 0.25f);
    float previous = -1;
    for (int i = 0; i <= 100; ++i) {
        const float probability = blink_probability(eye_moisture_need(i / 100.0f), 0.01f, p);
        assert(probability >= previous && probability >= 0 && probability <= 1);
        previous = probability;
    }
    const float prob = blink_probability(0.5f, 0.01f, p);
    near(blink_probability(0.5f, 0.02f, p), 1 - (1-prob)*(1-prob));
    near(blink_probability(0.5f, 0, p), 0);
    near(update_expression(1, 1, 0.2f, p), 0.5f + 0.5f * std::exp(-1.0f));
    near(update_expression(1, -1, 1, p), 1);
    auto invalid = p;
    invalid.blink_duration = std::numeric_limits<float>::quiet_NaN();
    assert(!invalid.valid());
    BodyBehavior fallback({never, nullptr}, invalid);
    near(fallback.parameters().blink_duration, p.blink_duration);
    assert(fallback.advance(100000000));
    assert(fallback.now_us() == 100000); // 休止後も追いつきループを制限。
    assert(!fallback.advance(0));
}
// 乾燥欲求が発生する境界まで乱数0で進め、開始時だけ種別を指定。
struct ActionRng {
    BodyBehavior *body = nullptr;
    bool twice;
    unsigned calls = 0;
    static uint32_t next(void *context) {
        auto &r = *static_cast<ActionRng *>(context);
        ++r.calls;
        // Body更新後に欲求が正なら、最初の乱数は判定、次は種別。
        if (eye_moisture_need(r.body->state().eye_dryness) > 0) {
            r.choose = !r.choose;
            if (!r.choose) return r.twice ? 0 : UINT32_MAX;
        }
        return 0;
    }
    bool choose = false;
};
void actions() {
    for (bool twice : {false, true}) {
        ActionRng rng{nullptr, twice, 0, false};
        BodyBehavior b({ActionRng::next, &rng});
        rng.body = &b;
        BodyState before;
        while (b.action_count() == 0) { before = b.state(); b.advance(10000); }
        update_body(before, {}, b.parameters(), 0.01f);
        near(b.state().eye_dryness, before.eye_dryness * 0.2f);
        assert(b.double_count() == static_cast<unsigned>(twice));
        const auto start = b.last_start_us();
        const auto end = start + (twice ? 400000 : 150000);
        assert(b.refractory_until_us() == end + 1000000);
        BodyState expected = b.state();
        b.input(BodyEvent::petting_detected);
        const unsigned calls = rng.calls;
        while (b.now_us() < end + 990000) {
            update_body(expected, {}, b.parameters(), 0.01f);
            b.advance(10000);
            near(b.state().eye_dryness, expected.eye_dryness); // 2回目も回復しない。
            assert(b.action_count() == 1);
            near(b.openness(), b.expression_openness() * b.blink_openness());
            if (twice && b.now_us() == start + 300000) assert(b.blink_openness() == 0);
        }
        assert(rng.calls == calls); // 行動中・終了後1秒未満は判定しない。
        b.advance(10000);
        assert(rng.calls > calls); // 境界から判定を再開。
    }
    // 十分な欲求があっても終了後1秒未満は開始できず、境界で再開する。
    for (bool twice : {false, true}) {
        BehaviorParameters fast;
        fast.blink_interval = 0.1f;
        ActionRng rng{nullptr, twice, 0, false};
        BodyBehavior b({ActionRng::next, &rng}, fast);
        rng.body = &b;
        while (!b.action_count()) b.advance(10000);
        const auto allowed = b.refractory_until_us();
        while (b.now_us() + 10000 < allowed) {
            b.advance(10000);
            assert(b.action_count() == 1);
        }
        b.advance(10000);
        assert(b.action_count() == 2 && b.last_start_us() == allowed);
    }
    // 強い保持で上限へ達しても範囲を超えない。
    BehaviorParameters pleasant;
    pleasant.holding_sensitivity = 1;
    pleasant.dryness_discomfort_strength = 0;
    BodyState saturated;
    for (int i = 0; i < 1000; ++i) update_body(saturated, {true}, pleasant, 0.01f);
    assert(saturated.valence == 1);
    Rng r1{12345}, r2{12345}, r3{23456};
    BodyBehavior a(r1.source()), b(r2.source()), c(r3.source());
    for (int i = 0; i < 100; ++i) {
        a.input(BodyEvent::petting_detected); b.input(BodyEvent::petting_detected);
        advance(a, 1000000, 5000); advance(b, 1000000, 37000); advance(c, 1000000, 20000);
        assert(a.state().eye_dryness == b.state().eye_dryness);
        assert(a.state().valence == b.state().valence);
        assert(a.openness() == b.openness());
        assert(a.last_start_us() == b.last_start_us());
    }
    assert(r1.state == r2.state && a.last_start_us() != c.last_start_us());
}
struct Stats { double mean, sd; uint64_t count; double double_ratio; };
Stats simulate(uint32_t seed, uint64_t seconds, uint64_t chunk) {
    Rng rng{seed};
    BodyBehavior b(rng.source());
    uint64_t previous = 0, count = 0, actions_seen = 0, allowed = 0;
    double sum = 0, square = 0;
    const uint64_t warmup = 600000000;
    while (b.now_us() < (seconds + 600) * 1000000) {
        b.advance(chunk);
        if (b.action_count() != actions_seen) {
            const uint64_t start = b.last_start_us();
            assert(start >= allowed);
            allowed = b.refractory_until_us();
            if (previous >= warmup) {
                const double interval = (start - previous) / 1e6;
                assert(interval >= 1.15);
                sum += interval; square += interval * interval; ++count;
            }
            previous = start;
            actions_seen = b.action_count();
        }
    }
    const double mean = sum / count;
    return {mean, std::sqrt(square/count - mean*mean), count,
            static_cast<double>(b.double_count()) / b.action_count()};
}
}
int main(int argc, char **) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    unit_tests(); actions();
    double sum = 0, square = 0;
    uint64_t count = 0;
    const bool calibration = argc > 1;
    const unsigned trials = calibration ? 4 : 8;
    const uint64_t seconds = calibration ? 21600 : 43200;
    std::vector<double> means;
    for (unsigned i = 0; i < trials; ++i) {
        // 較正用と評価用のシードを分離。評価は5/10/20/37msの呼出周期。
        const uint32_t seed = calibration ? 1001 + i : 9001 + i;
        const uint64_t chunks[] = {5000, 10000, 20000, 37000};
        const auto s = simulate(seed, seconds, calibration ? 10000 : chunks[i % 4]);
        std::printf("seed=%u hours=%.0f chunk_us=%llu n=%llu mean=%.6fs sd=%.6fs double=%.4f\n",
                    seed, seconds/3600.0, (unsigned long long)(calibration ? 10000 : chunks[i%4]),
                    (unsigned long long)s.count, s.mean, s.sd, s.double_ratio);
        sum += s.mean * s.count; square += (s.sd*s.sd+s.mean*s.mean)*s.count; count += s.count;
        means.push_back(s.mean);
        if (!calibration) {
            assert(std::abs(s.mean - 5) <= 0.15); // 各試行±3%。
            assert(std::abs(s.double_ratio - 0.1) <= 0.015);
        }
    }
    const double mean = sum/count;
    double spread = 0;
    for (double m : means) spread += (m-mean)*(m-mean);
    std::printf("pooled n=%llu mean=%.6fs interval_sd=%.6fs seed_mean_sd=%.6fs tolerance=0.10s\n",
                (unsigned long long)count, mean, std::sqrt(square/count-mean*mean), std::sqrt(spread/trials));
    if (!calibration) assert(std::abs(mean - 5) <= 0.1); // 全体±2%。
}
