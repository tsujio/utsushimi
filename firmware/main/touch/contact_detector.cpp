#include "contact_detector.h"
#include <algorithm>
#include <cmath>

namespace utsushimi {
void ContactDetector::missing() {
    baseline_stable_ = false;
    candidate_ = false;
    have_time_ = false;
    samples_ = 0;
    sum_ = 0;
    minimum_ = UINT32_MAX;
    maximum_ = 0;
    // 測定欠落を離脱とは扱わず、確定状態と校正済み基準値を保つ。
}
ContactUpdate ContactDetector::update(uint32_t raw, uint64_t now) {
    if (have_time_ && now <= last_ms_) { missing(); return ContactUpdate::none; }
    if (have_time_ && now - last_ms_ > ContactSettings::max_gap_ms) missing();
    const uint64_t dt = have_time_ ? now - last_ms_ : 0;
    const double raw_step = std::abs(static_cast<double>(raw) - previous_raw_);
    previous_raw_ = raw;
    have_time_ = true;
    last_ms_ = now;
    if (!ready_) {
        if (!samples_) calibration_start_ms_ = now;
        ++samples_; sum_ += raw;
        minimum_ = std::min(minimum_, raw); maximum_ = std::max(maximum_, raw);
        if (samples_ < ContactSettings::calibration_samples ||
            now - calibration_start_ms_ < ContactSettings::calibration_ms) return ContactUpdate::none;
        if (minimum_ == 0 || maximum_ - minimum_ > ContactSettings::calibration_range) {
            missing();
            return ContactUpdate::calibration_rejected;
        }
        baseline_ = static_cast<double>(sum_) / samples_;
        ready_ = true;
        return ContactUpdate::calibrated;
    }
    // 無効値は基準値にも離脱判定にも使わない。
    if (raw == 0) { missing(); return ContactUpdate::none; }
    // 安定判定の参照値は基準値と独立。固定した起点との比較では、
    // 緩やかなドリフトでも待機時間が繰り返しリセットされてしまう。
    if (!baseline_stable_ || raw_step > ContactSettings::baseline_step_limit ||
        std::abs(static_cast<double>(raw) - stability_reference_) > ContactSettings::stability_tolerance) {
        baseline_stable_ = true;
        baseline_stable_since_ = now;
        stability_reference_ = raw;
    } else {
        stability_reference_ += (raw - stability_reference_) *
            -std::expm1(-static_cast<double>(dt) / ContactSettings::stability_tau_ms);
        const uint64_t stable_ms = now - baseline_stable_since_;
        if (stable_ms > ContactSettings::baseline_settle_ms) {
            // 待機境界をまたぐ場合も、10秒経過後の時間だけを積分する。
            const uint64_t follow_ms = std::min(dt, stable_ms - ContactSettings::baseline_settle_ms);
            baseline_ += delta(raw) *
                -std::expm1(-static_cast<double>(follow_ms) / ContactSettings::baseline_tau_ms);
        }
    }
    // 更新済み基準値に対して通常のヒステリシス・デバウンスを適用する。
    const bool crossing = touching_ ? delta(raw) <= ContactSettings::end_delta
                                    : delta(raw) >= ContactSettings::start_delta;
    if (!crossing) {
        candidate_ = false;
        return ContactUpdate::none;
    }
    if (!candidate_) { candidate_ = true; since_ms_ = now; }
    if (now - since_ms_ < ContactSettings::debounce_ms) return ContactUpdate::none;
    candidate_ = false;
    touching_ = !touching_;
    return touching_ ? ContactUpdate::touch_started : ContactUpdate::touch_ended;
}
}
