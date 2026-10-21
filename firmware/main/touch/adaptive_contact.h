#pragma once
#include "contact_detector.h"
#include <algorithm>
#include <cmath>

namespace utsushimi {
// 試験用。身体イベントには接続せず、同じraw系列で現行方式と比較する。
class AdaptiveContact {
public:
    static constexpr uint64_t start_debounce_ms = 150;
    bool ready() const { return ready_; }
    bool touching() const { return held_; }
    bool fault() const { return fault_; }
    double baseline() const { return base_; }
    double threshold() const { return std::max(800.0, 8 * noise_); }
    void missing() {
        count_ = 0; have_time_ = false; candidate_ = false;
        settling_ = false; calibration_.missing();
    }
    void update(uint32_t raw, uint64_t now) {
        if (!raw) { missing(); return; }
        if (have_time_ && (now <= last_ || now - last_ > 150)) missing();
        double dt = have_time_ ? now - last_ : 0;
        last_ = now; have_time_ = true;
        if (!ready_) {
            calibration_.update(raw, now);
            if (!calibration_.ready()) return;
            ready_ = true; base_ = slow_ = previous_ = raw;
            base_ = calibration_.baseline();
        }
        window_[count_ % 3] = raw;
        ++count_;
        if (count_ < 3) return;
        double values[3] = {window_[0], window_[1], window_[2]};
        std::sort(values, values + 3);
        double value = values[1];
        if (count_ == 3) { slow_ = previous_ = value; dt = 0; }
        const double delta = value - base_;
        // 電極ごとに変化方向が逆転している記録があるため、極性を固定せず絶対差で判定。
        if (std::abs(delta) > 4000) fault_ = true;
        if (fault_) {
            candidate_ = false;
            if (std::abs(delta) < 300) {
                if (!settling_) { settling_ = true; settle_since_ = now; }
                if (now - settle_since_ >= 2000) { fault_ = false; held_ = false; }
            } else settling_ = false;
            slow_ = previous_ = value;
            return;
        }
        slow_ += (value - slow_) * -std::expm1(-dt / 1000.0);
        // 二つの時間尺度の差で立ち上がり候補を見つけ、baselineを凍結する。
        const bool rising = std::abs(value - slow_) > std::max(150.0, 4 * noise_);
        const bool crossing = held_ ? std::abs(delta) <= release_ : std::abs(delta) >= threshold();
        if (crossing) {
            if (!candidate_) { candidate_ = true; since_ = now; }
            if (now - since_ >= start_debounce_ms) {
                held_ = !held_; candidate_ = false;
                if (held_) release_ = threshold() * 0.45;
            }
            settling_ = false;
        } else {
            candidate_ = false;
            if (!held_ && !rising && std::abs(delta) < threshold() * 0.5) {
                if (!settling_) { settling_ = true; settle_since_ = now; }
                if (now - settle_since_ >= 500) {
                    base_ += std::clamp(delta * -std::expm1(-dt / 3000.0), -80 * dt / 1000, 80 * dt / 1000);
                    noise_ += (std::min(100.0, std::abs(value - previous_)) - noise_) * -std::expm1(-dt / 5000.0);
                }
            } else settling_ = false;
        }
        previous_ = value;
    }
private:
    ContactDetector calibration_;
    bool ready_ = false, held_ = false, fault_ = false, have_time_ = false;
    bool candidate_ = false, settling_ = false;
    uint64_t last_ = 0, since_ = 0, settle_since_ = 0;
    unsigned count_ = 0;
    double window_[3]{}, base_ = 0, slow_ = 0, previous_ = 0, noise_ = 20, release_ = 270;
};
}
