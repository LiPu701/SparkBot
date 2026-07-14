/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
 
#include <stdio.h>

#include "freertos/idf_additions.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_sys.h"
#include "audio_thread.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "periph_spiffs.h"
#include "periph_sdcard.h"
#include "audio_mem.h"
#include "board.h"

#include "volc_rtc.h"
#include "config.h"

#include "lv_demos.h"
#include "bsp/esp-bsp.h"

static char *TAG = "main";

static void monitor_task(void *arg)
{
    while (1) {
        audio_sys_get_real_time_stats();
        AUDIO_MEM_SHOW(TAG);
        vTaskDelay(10000 / portTICK_RATE_MS);
    }
}

extern const lv_img_dsc_t ai_voice;
extern const lv_img_dsc_t doubao;
extern const lv_img_dsc_t doubao_man;
extern const lv_img_dsc_t doubao_head;

static lv_obj_t *ui_voice;

void enable_broadcast(bool enable)
{
    bsp_display_lock(0);

    if(enable){
        lv_obj_clear_flag(ui_voice, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_voice, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

esp_err_t app_ui_start(void)
{
    /* Initialize display and LVGL */
    bsp_display_cfg_t custom_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .trans_size = BSP_LCD_H_RES * 10, // in SRAM, DMA-capable
        .double_buffer = 0,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        }
    };
    custom_cfg.lvgl_port_cfg.task_stack = 1024 * 6;
    custom_cfg.lvgl_port_cfg.task_affinity = 1;
    bsp_display_start_with_config(&custom_cfg);

    /* Turn on display backlight */
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display initialized");
    bsp_display_lock(0);

#if 0
    lv_demo_widgets();
#else
    lv_obj_t *obj_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj_bg, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_align(obj_bg, LV_ALIGN_CENTER);
    lv_obj_clear_flag(obj_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj_bg, 0, 0);

    lv_obj_t *ui_emoji = lv_img_create(obj_bg);
    lv_obj_set_width(ui_emoji, 240);
    lv_obj_set_height(ui_emoji, 240);
    lv_obj_set_align(ui_emoji, LV_ALIGN_CENTER);
#if 1
    lv_img_set_src(ui_emoji, &doubao_head);
#else
    lv_img_set_src(ui_emoji, &doubao_man);
#endif

#endif
    bsp_display_unlock();


    return ESP_OK;
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Initialize board peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral
    // audio_board_sdcard_init(set, SD_MODE_1_LINE);

    app_ui_start();

    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = true};
    esp_periph_handle_t spiffs_handle = periph_spiffs_init(&spiffs_cfg);
    esp_periph_start(set, spiffs_handle);

    // Wait until spiffs is mounted
    while (!periph_spiffs_is_mounted(spiffs_handle)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 90);

    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    // audio_thread_create(NULL, "monitor_task", monitor_task, NULL, 5 * 1024, 13, true, 0);

    // init byte rtc engine
    volc_rtc_init();
}
