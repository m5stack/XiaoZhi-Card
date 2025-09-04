/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create LCD panel for model ILI9341
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *          - ESP_ERR_INVALID_ARG   if parameter is invalid
 *          - ESP_ERR_NO_MEM        if out of memory
 *          - ESP_OK                on success
 */
esp_err_t esp_lcd_new_panel_gdey027t91(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_panel_dev_config_t *panel_dev_config,
                                       esp_lcd_panel_handle_t *ret_panel);

void panel_gdey027t91_refresh_code(uint8_t code);

esp_err_t panel_gdey027t91_draw_bitmap_full(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                            const void *color_data);

#ifdef __cplusplus
}
#endif
