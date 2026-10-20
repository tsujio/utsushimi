#pragma once

namespace utsushimi {
// 起動時に1回呼ぶ。電極Aの診断専用タスクを作成する。
// 失敗はログに記録し、表示・身体反応は継続する。
void start_touch_diagnostics();
}
