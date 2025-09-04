/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include "esp_epd_gdey027t91.h"

static const char *TAG = "gdey027t91";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int busy_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    unsigned int bits_per_pixel;
    int busy_timeout_ms;
} gdey027t91_panel_t;

static uint8_t g_refresh_code = 0xFC;

static esp_err_t panel_gdey027t91_del(esp_lcd_panel_t *panel);
static esp_err_t panel_gdey027t91_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_gdey027t91_init(esp_lcd_panel_t *panel);
static esp_err_t panel_gdey027t91_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                              const void *color_data);
static void convert_8bit_to_1bit(const uint8_t *gray_buffer, uint8_t *mono_buffer, uint16_t width, uint16_t height,
                                 uint8_t threshold);
static void gdey027t91_set_window(gdey027t91_panel_t *panel, int x_start, int y_start, int x_end, int y_end);
static uint8_t *gdey027t91_prepare_mono_buffer(const uint8_t *color_data, int width, int height, uint8_t threshold);
static esp_err_t gdey027t91_trigger_refresh(gdey027t91_panel_t *panel, uint8_t mode);

esp_err_t esp_lcd_new_panel_gdey027t91(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_panel_dev_config_t *panel_dev_config,
                                       esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret                  = ESP_OK;
    gdey027t91_panel_t *gdey027t91 = NULL;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    gdey027t91 = calloc(1, sizeof(gdey027t91_panel_t));
    ESP_GOTO_ON_FALSE(gdey027t91, ESP_ERR_NO_MEM, err, TAG, "no mem for gdey027t91 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }
    gdey027t91->io               = io;
    gdey027t91->bits_per_pixel   = panel_dev_config->bits_per_pixel;
    gdey027t91->reset_gpio_num   = panel_dev_config->reset_gpio_num;
    gdey027t91->reset_level      = panel_dev_config->flags.reset_active_high;
    gdey027t91->busy_gpio_num    = GPIO_NUM_48;  // EINK_EPD_PIN_BUSY; // panel_dev_config->busy_gpio_num;
    gdey027t91->busy_timeout_ms  = 3000;         //
    gdey027t91->base.del         = panel_gdey027t91_del;
    gdey027t91->base.reset       = panel_gdey027t91_reset;
    gdey027t91->base.init        = panel_gdey027t91_init;
    gdey027t91->base.draw_bitmap = panel_gdey027t91_draw_bitmap;

    if (gdey027t91->busy_gpio_num >= 0) {
        gpio_reset_pin(gdey027t91->busy_gpio_num);
        gpio_set_direction(gdey027t91->busy_gpio_num, GPIO_MODE_INPUT);
    }

    *ret_panel = &(gdey027t91->base);
    ESP_LOGD(TAG, "new gdey027t91 panel @%p", gdey027t91);

    return ESP_OK;

err:
    if (gdey027t91) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(gdey027t91);
    }
    return ret;
}

static esp_err_t panel_gdey027t91_del(esp_lcd_panel_t *panel)
{
    gdey027t91_panel_t *gdey027t91 = __containerof(panel, gdey027t91_panel_t, base);

    if (gdey027t91->reset_gpio_num >= 0) {
        gpio_reset_pin(gdey027t91->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del gdey027t91 panel @%p", gdey027t91);
    free(gdey027t91);

    return ESP_OK;
}

static esp_err_t panel_gdey027t91_reset(esp_lcd_panel_t *panel)
{
    gdey027t91_panel_t *gdey027t91 = __containerof(panel, gdey027t91_panel_t, base);
    esp_lcd_panel_io_handle_t io   = gdey027t91->io;

    // perform hardware reset
    if (gdey027t91->reset_gpio_num >= 0) {
        gpio_set_level(gdey027t91->reset_gpio_num, gdey027t91->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(gdey027t91->reset_gpio_num, !gdey027t91->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {  // perform software reset
        esp_lcd_panel_io_tx_param(io, 0x12, (uint8_t[]){0}, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_lcd_panel_io_tx_param(io, 0x11, (uint8_t[]){0x03}, 1);
        esp_lcd_panel_io_tx_param(io, 0x3C, (uint8_t[]){0x05}, 1);
        esp_lcd_panel_io_tx_param(io, 0x18, (uint8_t[]){0x80}, 1);
        vTaskDelay(pdMS_TO_TICKS(10));  // spec, wait at least 5ms before sending new command
    }

    return ESP_OK;
}

static esp_err_t panel_gdey027t91_init(esp_lcd_panel_t *panel)
{
    gdey027t91_panel_t *gdey027t91 = __containerof(panel, gdey027t91_panel_t, base);
    esp_lcd_panel_io_handle_t io   = gdey027t91->io;

    (void)io;
    // TODO:

    return ESP_OK;
}

void panel_gdey027t91_refresh_code(uint8_t code)
{
    g_refresh_code = code;
    // if (full_refresh) {
    //     g_refresh_code = 0xD7; // 快速全局刷新 // 0xF7; // 全刷
    // } else {
    //     g_refresh_code = 0xFF; // 局刷
    // }
}

/**
 * 绘制灰度图像(局刷)
 *
 * 图像数据应为 8bpp 灰度格式（每字节一个像素，0=白，255=黑）。
 *
 * @param panel  指向 epd_panel_t 类型的面板控制结构体
 * @param x_start 起始 X 坐标（像素）
 * @param y_start 起始 Y 坐标（像素）
 * @param x_end   结束 X 坐标（像素）
 * @param y_end   结束 Y 坐标（像素）
 * @param color_bitmap 指向图像数据的指针（灰度，大小为 (x_end - x_start) * (y_end - y_start) 字节）
 */
static esp_err_t panel_gdey027t91_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    gdey027t91_panel_t *gdey027t91 = __containerof(panel, gdey027t91_panel_t, base);
    gdey027t91_set_window(gdey027t91, x_start, y_start, x_end, y_end);

    // 图像数据转换并发送
    int width = x_end - x_start;
    int height = y_end - y_start;
    uint8_t *mono_buffer = gdey027t91_prepare_mono_buffer(color_data, width, height, 0x7F);
    esp_lcd_panel_io_tx_color(gdey027t91->io, 0x24, mono_buffer, ((width + 7) / 8) * height);
    esp_err_t ret = gdey027t91_trigger_refresh(gdey027t91, g_refresh_code);
    g_refresh_code = 0xFC; // 刷新（局刷模式）
    free(mono_buffer);
    return ret;
}

/**
 * 绘制灰度图像(全刷)
 *
 * 图像数据应为 8bpp 灰度格式（每字节一个像素，0=白，255=黑）。
 *
 * @param panel  指向 epd_panel_t 类型的面板控制结构体
 * @param x_start 起始 X 坐标（像素）
 * @param y_start 起始 Y 坐标（像素）
 * @param x_end   结束 X 坐标（像素）
 * @param y_end   结束 Y 坐标（像素）
 * @param color_bitmap 指向图像数据的指针（灰度，大小为 (x_end - x_start) * (y_end - y_start) 字节）
 */
esp_err_t panel_gdey027t91_draw_bitmap_full(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    gdey027t91_panel_t *gdey027t91 = __containerof(panel, gdey027t91_panel_t, base);
    gdey027t91_set_window(gdey027t91, x_start, y_start, x_end, y_end);

    int width = x_end - x_start;
    int height = y_end - y_start;
    uint8_t *mono_buffer = gdey027t91_prepare_mono_buffer(color_data, width, height, 0x7F);
    esp_lcd_panel_io_tx_color(gdey027t91->io, 0x24, mono_buffer, ((width + 7) / 8) * height);
    esp_err_t ret = gdey027t91_trigger_refresh(gdey027t91, 0xF7);  // 全刷
    g_refresh_code = 0xF7; 
    free(mono_buffer);

    return ret;
}

/**
 * @brief 8位灰度转1位单色（核心转换）
 * @param gray_buffer 输入灰度数组（每个像素0-255）
 * @param mono_buffer 输出单色数组（每字节存8像素，MSB优先）
 * @param width      图像宽度（像素数）
 * @param height     图像高度（像素数）
 * @param threshold  二值化阈值（0-255）
 */
void convert_8bit_to_1bit(const uint8_t *gray_buffer, uint8_t *mono_buffer, uint16_t width, uint16_t height,
                          uint8_t threshold)
{
    const uint16_t bytes_per_row = (width + 7) / 8;

    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const uint32_t byte_idx = y * bytes_per_row + (x / 8);
            const uint8_t bit_pos   = 7 - (x % 8);
            const bool bit          = gray_buffer[y * width + x] >= threshold;
            if (bit) {
                mono_buffer[byte_idx] |= (1 << bit_pos);
            } else {
                mono_buffer[byte_idx] &= ~(1 << bit_pos);
            }
        }
    }
}

static void gdey027t91_set_window(gdey027t91_panel_t *panel, int x_start, int y_start, int x_end, int y_end)
{
    esp_lcd_panel_io_handle_t io = panel->io;

    x_start += panel->x_gap;
    x_end += panel->x_gap;
    y_start += panel->y_gap;
    y_end += panel->y_gap;

    esp_lcd_panel_io_tx_param(io, 0x11, (uint8_t[]){0x03}, 1);  // x/y递增模式

    uint8_t x_start_byte = x_start / 8;
    uint8_t x_end_byte   = (x_end - 1) / 8;
    esp_lcd_panel_io_tx_param(io, 0x44, (uint8_t[]){x_start_byte, x_end_byte}, 2);

    uint16_t y_start_line = y_start;
    uint16_t y_end_line   = y_end - 1;
    esp_lcd_panel_io_tx_param(io, 0x45,
                              (uint8_t[]){(uint8_t)(y_start_line & 0xFF), (uint8_t)(y_start_line >> 8),
                                          (uint8_t)(y_end_line & 0xFF), (uint8_t)(y_end_line >> 8)},
                              4);

    uint8_t current_x = x_start / 8;
    esp_lcd_panel_io_tx_param(io, 0x4E, (uint8_t[]){current_x}, 1);
    esp_lcd_panel_io_tx_param(io, 0x4F, (uint8_t[]){(uint8_t)(y_start & 0xFF), (uint8_t)(y_start >> 8)}, 2);
}

static uint8_t *gdey027t91_prepare_mono_buffer(const uint8_t *color_data, int width, int height, uint8_t threshold)
{
    int mono_bytes_per_row = (width + 7) / 8;
    int buffer_size        = mono_bytes_per_row * height;
    uint8_t *buffer        = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    assert(buffer != NULL);
    convert_8bit_to_1bit(color_data, buffer, width, height, threshold);
    return buffer;
}

static esp_err_t gdey027t91_wait_busy(gdey027t91_panel_t *panel)
{
    if (panel->busy_gpio_num >= 0) {
        int64_t timeout_us = panel->busy_timeout_ms * 1000;
        int64_t start = esp_timer_get_time();

        while (gpio_get_level(panel->busy_gpio_num)) {
            if (esp_timer_get_time() - start > timeout_us) {
                ESP_LOGE(TAG, "Refresh timeout after %dms", panel->busy_timeout_ms);
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS(1500));  // fallback delay
    }

    return ESP_OK;
}

static esp_err_t gdey027t91_trigger_refresh(gdey027t91_panel_t *panel, uint8_t mode)
{
    esp_lcd_panel_io_tx_param(panel->io, 0x22, &mode, 1);
    esp_lcd_panel_io_tx_param(panel->io, 0x20, NULL, 0);
    return gdey027t91_wait_busy(panel);
}
