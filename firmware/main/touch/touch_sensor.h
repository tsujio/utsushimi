#pragma once
#include <cstdint>
#include "petting_detector.h"

namespace utsushimi {
// 起動時に1回呼ぶ。電極A/B/Cの入力・診断タスクを作成する。
// 失敗はログに記録し、表示・身体反応は継続する。
void start_touch_diagnostics();
// 最新の確定接触状態。未校正・未取得・500ms超の測定欠落ではfalse。
// 身体タスクから非ブロッキングで読む。生の測定値は公開しない。
bool touch_is_held();
struct PettingEvent { PettingDirection direction; int64_t measured_us; };
// 身体タスク専用。500ms以内の未処理イベントを非ブロッキングで1件取得。
bool take_petting_event(PettingEvent &event);
}
