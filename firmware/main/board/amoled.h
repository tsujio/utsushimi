#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Rev2.0専用。ネイティブ座標は450×600。RGB565は上位バイトから送る。
enum { AMOLED_WIDTH = 450, AMOLED_HEIGHT = 600, AMOLED_STRIP_ROWS = 20, AMOLED_MAX_TRANSFER_BYTES = 186 * 476 * 2 };
void amoled_init(void);
// 表示タスク専用。偶数境界の矩形を転送し、DMA完了まで待機する。
// 最大転送量はAMOLED_MAX_TRANSFER_BYTES。入力処理側から直接呼ばない。
void amoled_write_rect(int x, int y, int width, int height, const uint16_t *pixels);
void amoled_show(void);
#ifdef __cplusplus
}
#endif
