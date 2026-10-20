#pragma once
#include <cstdint>
namespace utsushimi {
enum class PettingDirection { none, a_to_c, c_to_a };
struct PettingSettings {
    static constexpr uint64_t release_ms = 100;
    static constexpr uint64_t min_step_ms = 100;
    static constexpr uint64_t max_step_ms = 1000;
    static constexpr uint64_t max_total_ms = 2000;
    static constexpr uint64_t max_sample_gap_ms = 150;
};
// 校正済みの確定接触マスク（A=1 B=2 C=4）を走査ごとに渡す。
class PettingDetector {
public:
    PettingDirection update(uint8_t mask, uint64_t now_ms);
    void missing();
private:
    bool have_time_ = false, releasing_ = false, armed_ = false;
    uint64_t previous_ms_ = 0, release_since_ = 0, start_ms_ = 0, step_ms_ = 0;
    uint8_t previous_mask_ = 0, origin_ = 0;
    unsigned stage_ = 0;
};
}
