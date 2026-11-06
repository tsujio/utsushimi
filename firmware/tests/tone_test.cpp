#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include "audio/tone.h"

namespace {
void check_tone(float hz, uint32_t frames, float volume) {
    utsushimi::SineTone wave(hz, frames, volume);
    utsushimi::SineTone louder(hz, frames, volume * 2);
    utsushimi::SineTone silent(hz, frames, 0);
    unsigned crossings = 0;
    int32_t previous = 0;
    double sum = 0;
    const double amplitude = static_cast<double>(volume) * 2147483648.0;
    for (uint32_t i = 0; i < frames; ++i) {
        const auto sample = wave.next();
        assert(std::abs(static_cast<int64_t>(sample)) <= amplitude + 2);
        assert(silent.next() == 0);
        assert(std::abs(static_cast<int64_t>(louder.next()) - 2LL * sample) <= 1);
        const double envelope = std::min(1.0, std::min(i, frames - 1 - i) / 240.0);
        const double reference = std::sin(6.283185307179586 * hz * i / utsushimi::kAudioSampleRate)
                               * envelope * amplitude;
        // Lookup interpolation and accumulated DDS rounding over at most 2s.
        assert(std::abs(sample - reference) <= amplitude * 0.0003 + 4);
        if (i == 0 || i + 1 == frames) assert(sample == 0);
        if (sample > 0 && previous <= 0) ++crossings;
        previous = sample;
        sum += sample;
    }
    assert(wave.next() == 0);
    assert(wave.next() == 0);
    if (hz == 440 && frames == 9600) {
        assert(crossings == 88);
        assert(std::abs(sum / frames) < 1000);
    }
}
}
int main() {
    check_tone(440, 9600, .01f);
    for (const float hz : {20.0f, 440.0f, 1000.0f, 20000.0f})
        for (const uint32_t frames : {48u, 9600u, 96000u})
            check_tone(hz, frames, .08f);
    // Block boundaries never reset phase or the envelope.
    utsushimi::SineTone reference(440, 96000, .08f);
    utsushimi::SineTone blocked(440, 96000, .08f);
    for (unsigned base = 0; base < 96000; base += 240)
        for (unsigned i = 0; i < 240; ++i) assert(blocked.next() == reference.next());
    utsushimi::SineTone empty(440, 0, .01f);
    assert(empty.next() == 0);
}
