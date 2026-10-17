#include "behavior/blink.h"
#include <cassert>
using utsushimi::Blink;
int main() {
    Blink single(0, 0); // 2000ms後に開始
    assert(single.update(1999, 1) == 1);
    assert(single.update(2000, 1) == 1); // 1は単発を選択
    assert(single.update(2035, 1) > 0 && single.update(2035, 1) < 1);
    assert(single.update(2070, 1) == 0);
    assert(single.update(2109, 1) == 0);
    assert(single.update(2185, 1) > 0 && single.update(2185, 1) < 1);
    assert(single.update(2260, 4500) == 1); // 次は6500ms後
    assert(single.update(8759, 1) == 1);
    single.update(8760, 1);
    assert(single.update(8830, 1) == 0);

    Blink twice(0, 0);
    twice.update(2000, 0); // 0は二連を選択
    assert(twice.update(2070, 1) == 0);
    assert(twice.update(2260, 1) == 1);
    assert(twice.update(2359, 1) == 1);
    assert(twice.update(2430, 1) == 0);
    assert(twice.update(2620, 1) == 1);
    // 32bitミリ秒を越えても正常に動く。長時間スケジュール遅延後は開眼復帰。
    const uint64_t late = uint64_t{1} << 33;
    Blink long_running(late, 0);
    long_running.update(late + 2000, 1);
    assert(long_running.update(late + 2070, 1) == 0);
    assert(long_running.update(late + 20000, 1) == 1);
}
