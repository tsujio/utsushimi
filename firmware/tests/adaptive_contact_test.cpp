#include "touch/adaptive_contact.h"
#include <cassert>
#include <iostream>
using namespace utsushimi;
int main() {
    for (int sign : {-1, 1}) {
        AdaptiveContact trial; ContactDetector old;
        uint64_t t = 0;
        auto sample = [&](uint32_t raw) { trial.update(raw, t); old.update(raw, t); t += 50; };
        for (int i=0; i<100; ++i) sample(45000);
        // 合成データ：45秒で±900のドリフト。実測データではない。
        for (int i=0; i<900; ++i) {
            sample(45000 + sign*i);
            assert(!trial.touching() && !trial.fault());
        }
        const uint32_t idle = 45000 + sign*900;
        std::cout << "synthetic drift " << sign*900 << ": old residual=" << old.delta(idle)
                  << " trial residual=" << idle-trial.baseline() << '\n';
        assert(std::abs(idle-trial.baseline()) < 100);
        auto start = t;
        while (!trial.touching() && t-start<500) sample(idle+1600);
        assert(trial.touching() && t-start<=250);
        std::cout << "start delay <= " << t-start << "ms\n";
        double frozen = trial.baseline();
        for (int i=0; i<600; ++i) { sample(idle+1600); assert(trial.touching()); }
        assert(trial.baseline() == frozen);
        start=t;
        while (trial.touching() && t-start<500) sample(idle);
        assert(!trial.touching() && t-start<=250);
    }
    AdaptiveContact d;
    uint64_t t=0;
    auto feed=[&](uint32_t raw, int n) { for(int i=0;i<n;++i,t+=50) d.update(raw,t); };
    feed(45000,100);
    feed(47000,1); feed(45000,20); assert(!d.touching()); // 単発外れ値
    feed(43000,10); assert(d.touching() && !d.fault()); // 負極性の接触候補
    feed(45000,10); assert(!d.touching() && !d.fault());
    feed(50000,10); assert(d.fault() && !d.touching()); // 大きすぎる段差は異常
    feed(45000,60); assert(!d.fault() && !d.touching());
    feed(46600,10); assert(d.touching());
    d.missing(); feed(0,10); assert(d.touching()); // 欠測を離脱にしない
    feed(45000,10); assert(!d.touching());
    feed(46600,2); t+=500; feed(46600,1); assert(!d.touching());
    feed(46600,10); assert(d.touching());
    AdaptiveContact noisy;
    for (uint64_t ms=0; ms<120000; ms+=50) {
        const int jitter = static_cast<int>((ms / 50 * 37) % 101) - 50;
        noisy.update(45000 + jitter, ms);
        assert(!noisy.touching() && !noisy.fault());
    }
    assert(noisy.ready());
    // 1秒かけた接触の立ち上がりも、追従で吸収しない。
    for (uint64_t ms=120000; ms<122000; ms+=50)
        noisy.update(45000 - std::min<uint64_t>(1600, (ms-120000)*1600/1000), ms);
    assert(noisy.touching());
    std::cout << "adaptive contact tests passed (synthetic only)\n";
}
