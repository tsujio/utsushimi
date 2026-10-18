#include "behavior/blink.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
using namespace utsushimi;
int main() {
    Blink blink;
    assert(!blink.active(0) && blink.openness(0) == 1);
    const uint64_t start = 9000000000000ULL; // 長時間稼働後の64bit時刻。
    for (bool twice : {false, true}) {
        blink.start(start, 150000, twice);
        assert(blink.active(start) && blink.openness(start) == 1);
        assert(blink.openness(start + 20000) < 1);
        assert(blink.openness(start + 45000) == 0);
        assert(blink.openness(start + 74000) == 0);
        assert(blink.openness(start + 100000) > 0);
        assert(blink.openness(start + 150000) == 1);
        assert(blink.end_us() == start + (twice ? 400000 : 150000));
        if (twice) {
            assert(blink.active(start + 249999));
            assert(blink.openness(start + 249999) == 1);
            assert(blink.openness(start + 270000) < 1);
            assert(blink.openness(start + 295000) == 0);
        }
        assert(!blink.active(blink.end_us()));
        assert(blink.openness(blink.end_us()) == 1);
        for (uint64_t t = start; t <= blink.end_us(); t += 1000)
            assert(blink.openness(t) >= 0 && blink.openness(t) <= 1);
    }
}
