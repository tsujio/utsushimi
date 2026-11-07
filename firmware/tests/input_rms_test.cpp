#include "audio/input_rms.h"
#include <cassert>
#include <cmath>
#include <cstdint>
using utsushimi::InputRms;
static int32_t pack(int32_t sample) {
    const uint32_t bits = (static_cast<uint32_t>(sample) << 8) | 0xabu;
    // Avoid implementation-defined conversion of out-of-range unsigned values.
    return bits <= INT32_MAX ? int32_t(bits) : -1 - int32_t(~bits);
}
int main() {
    assert(InputRms::decode(pack(-8388608)) == -8388608);
    assert(InputRms::decode(pack(8388607)) == 8388607);
    assert(InputRms::decode(pack(-1)) == -1);
    InputRms dc;
    assert(dc.rms() == 0);
    for (int i = 0; i < 4800; ++i) dc.add(pack(1234567));
    assert(dc.count() == 4800 && dc.rms() == 0);
    InputRms wave;
    for (int i = 0; i < 4800; ++i) {
        const int32_t sample = 2000000 + int32_t(1000000 * std::sin(i * 2 * 3.141592653589793 / 48));
        wave.add(pack(sample));
    }
    assert(std::abs(wave.rms() - 1000000 / 8388608.0 / std::sqrt(2.0)) < 1e-7);
    InputRms limits;
    for (int i = 0; i < 4800; ++i) limits.add(pack(i % 2 ? 8388607 : -8388608));
    assert(std::isfinite(limits.rms()) && limits.rms() > 0.999999);
}
