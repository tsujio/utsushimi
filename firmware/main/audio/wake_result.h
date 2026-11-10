#pragma once
namespace utsushimi {
// ESP-SR WakeNet state contract. Verified is a normal post-detection state,
// including mono operation; it must not reset PCM or model history.
enum class WakeResult { none, detected, verified, error };
constexpr WakeResult classify_wake_result(int result) {
    return result > 0 ? WakeResult::detected : result == 0 ? WakeResult::none :
           result == -1 ? WakeResult::verified : WakeResult::error;
}
}
