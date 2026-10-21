#pragma once
#include <cstdint>

namespace utsushimi {
// 電極Aの実測から選んだ暫定値。容量値ではなくrawカウントの差。
struct ContactSettings {
    static constexpr unsigned calibration_samples = 40;
    static constexpr uint64_t calibration_ms = 1950;
    static constexpr uint32_t calibration_range = 500;
    static constexpr double start_delta = 1000;
    static constexpr double end_delta = 600;
    static constexpr uint64_t debounce_ms = 50;
    static constexpr uint64_t max_gap_ms = 150;
    // 接触状態とは独立した安定判定。ノイズ許容幅は実機調整用の暫定値。
    static constexpr uint64_t baseline_settle_ms = 10000;
    static constexpr double stability_tau_ms = 1000;
    static constexpr double stability_tolerance = 150;
    static constexpr uint32_t baseline_step_limit = 100;
    static constexpr double baseline_tau_ms = 40000;
};
enum class ContactUpdate { none, calibrated, calibration_rejected, touch_started, touch_ended };
// ハードウェア非依存。単調な実測時刻で更新し、測定失敗はmissing()へ渡す。
class ContactDetector {
public:
    ContactUpdate update(uint32_t raw, uint64_t now_ms);
    void missing();
    bool ready() const { return ready_; }
    bool touching() const { return touching_; }
    double baseline() const { return baseline_; }
    double delta(uint32_t raw) const { return static_cast<double>(raw) - baseline_; }
private:
    bool ready_ = false, touching_ = false, candidate_ = false, have_time_ = false;
    uint64_t last_ms_ = 0, since_ms_ = 0, calibration_start_ms_ = 0, sum_ = 0;
    unsigned samples_ = 0;
    uint32_t minimum_ = UINT32_MAX, maximum_ = 0;
    double baseline_ = 0;
    bool baseline_stable_ = false;
    uint64_t baseline_stable_since_ = 0;
    uint32_t previous_raw_ = 0;
    double stability_reference_ = 0;
};
}
