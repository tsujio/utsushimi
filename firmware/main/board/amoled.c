// 基板固有設定の根拠：Waveshare公式Rev2.0サンプル（詳細はREADME）。
#include "amoled.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_io_expander_tca9554.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t transfer_done;

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x20}, 1, 0},
    {0x26, (uint8_t []){0x0A}, 1, 0},
    {0x24, (uint8_t []){0x80}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xC2, (uint8_t []){0x00}, 1, 10},
    {0x35, (uint8_t []){0x00}, 0, 0},
    {0x51, (uint8_t []){0x00}, 1, 10},
    {0x11, (uint8_t []){0x00}, 0, 80},
    {0x2A, (uint8_t []){0x00,0x10,0x00,0xD1}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x00,0x57}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x51, (uint8_t []){0x00}, 1, 0},
};

static bool on_transfer_done(esp_lcd_panel_io_handle_t handle,
                             esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)handle; (void)event; (void)context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &wake);
    return wake == pdTRUE;
}

void amoled_init(void) {
    transfer_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(transfer_done ? ESP_OK : ESP_ERR_NO_MEM);
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t i2c = {
        .i2c_port = I2C_NUM_0, .sda_io_num = GPIO_NUM_47, .scl_io_num = GPIO_NUM_48,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c, &bus));
    esp_io_expander_handle_t expander;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(
        bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &expander));
    // EXIO0だけを操作。EXIO1（画面タッチ）などには触れない。
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT));
    const spi_bus_config_t spi = {
        .sclk_io_num = GPIO_NUM_10,
        .data0_io_num = GPIO_NUM_11, .data1_io_num = GPIO_NUM_12,
        .data2_io_num = GPIO_NUM_13, .data3_io_num = GPIO_NUM_14,
        .max_transfer_sz = AMOLED_MAX_TRANSFER_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = GPIO_NUM_9, .dc_gpio_num = -1, .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 1,
        .on_color_trans_done = on_transfer_done,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8, .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));
    sh8601_vendor_config_t vendor = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &config, &panel));
    // 起動時のハードウェアリセット待ち。通常動作のアニメーションとは別。
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 1));
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

void amoled_write_rect(int x, int y, int width, int height, const uint16_t *pixels) {
    ESP_ERROR_CHECK((pixels && x >= 0 && y >= 0 && width > 0 && height > 0 &&
                    width <= AMOLED_WIDTH && height <= AMOLED_HEIGHT &&
                    width * height * sizeof(uint16_t) <= AMOLED_MAX_TRANSFER_BYTES &&
                    x <= AMOLED_WIDTH - width && y <= AMOLED_HEIGHT - height &&
                    (x | y | width | height) % 2 == 0) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height, pixels));
    // DMAがバッファを参照している間は再利用しない。待つのは表示タスクのみ。
    ESP_ERROR_CHECK(xSemaphoreTake(transfer_done, pdMS_TO_TICKS(2000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
}

void amoled_show(void) {
    const uint8_t brightness = 0x80;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, (0x02u << 24) | (0x51u << 8), &brightness, 1));
}
