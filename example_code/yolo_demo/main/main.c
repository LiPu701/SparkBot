/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

#include "app_camera.h"
#include "app_ai_detect.h"
#include "app_mode_manager.h"

static char *TAG = "app_main";

#define LOG_MEM_INFO    (1)

// Auto mode switch timing configuration - REMOVED, now manual only
// #define CAMERA_DISPLAY_TIME_MS  10000    // Show camera for 5 seconds
// #define DETECTION_WAIT_TIME_MS  10000   // Maximum wait time for detection completion

// Manual mode control variables - REMOVED, now always manual
// static bool manual_mode_enabled = false;   // Flag to enable/disable manual mode
// static bool auto_mode_task_running = true; // Flag to control auto mode task

#define LOG_MEMORY_SYSTEM_INFO         (0)
#define LOG_TASK_SYSTEM_INFO           (0)
#define LOG_TIME_INTERVAL_MS           (2000)
#define SYS_TASKS_ELAPSED_TIME_MS      (2000)   // Period of stats measurement

esp_lcd_panel_handle_t lcd_panel;
esp_lcd_panel_io_handle_t lcd_io;

esp_err_t print_real_time_mem_stats(void);

// Function to handle automatic mode switching - REMOVED
// static void auto_mode_switch_task(void *arg)

static void button_handler(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
{
    (void) out_handle; // Unused
    int button = (int)arg;
    
    // Only enable button 1 for mode switching, other buttons are disabled
    if (button != 1) {
        ESP_LOGW(TAG, "Button[%d] is disabled - only Button[1] is enabled for mode control", button);
        return;
    }
    
    if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
        ESP_LOGI(TAG, "Button[%d] Press", button);
        
        // Button press: Toggle between camera and detection modes
        app_display_mode_t current_mode = app_mode_manager_get_mode();
        app_detection_state_t detection_state = app_mode_manager_get_detection_state();
        
        if (current_mode == MODE_CAMERA_DISPLAY) {
            // Switch from camera to detection mode
            if (app_mode_manager_can_switch_mode()) {
                ESP_LOGI(TAG, "Manual switch: Camera -> Detection");
                app_mode_manager_trigger_detection();
            } else {
                ESP_LOGW(TAG, "Cannot switch to detection - system busy");
            }
        } else if (current_mode == MODE_AI_DETECTION && detection_state != DETECTION_PROCESSING) {
            // Switch from detection back to camera mode (only if detection is not processing)
            ESP_LOGI(TAG, "Manual switch: Detection -> Camera");
            app_mode_manager_switch_to_camera();
        } else {
            ESP_LOGW(TAG, "Detection in progress - please wait for completion");
        }
        
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
        ESP_LOGI(TAG, "Button[%d] Release", button);
        
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
        ESP_LOGI(TAG, "Button[%d] LongPress", button);
        
        // Long press: Force return to camera mode (emergency reset)
        ESP_LOGI(TAG, "Long press detected - forcing return to camera mode");
        if (!app_mode_manager_can_switch_mode()) {
            ESP_LOGI(TAG, "Force switching to camera mode");
            app_mode_manager_force_camera_mode();
        } else {
            app_mode_manager_switch_to_camera();
        }
    }
}

void app_main(void)
{
    /* Initialize display and LVGL */
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t),
    };
    bsp_display_new(&bsp_disp_cfg, &lcd_panel, &lcd_io);
    esp_lcd_panel_disp_on_off(lcd_panel, true);
    bsp_display_backlight_on();

    // Initialize AI detection
    app_ai_detect_init();
    
    // Initialize mode manager
    app_mode_manager_init(lcd_panel);
    
    // Register LCD transfer callback for mutual exclusion
    esp_err_t callback_ret = app_mode_manager_register_lcd_callback(lcd_io);
    if (callback_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LCD callback: %s", esp_err_to_name(callback_ret));
    } else {
        ESP_LOGI(TAG, "LCD transfer synchronization enabled");
    }

    // Initialize and start camera
    app_camera_init(lcd_panel);
    app_camera_begin();
    app_camera_start();

    /* Create touch button */
    bsp_touch_button_create(button_handler);

    ESP_LOGI(TAG, "System initialized. Manual button controls available:");
    ESP_LOGI(TAG, "  - Button[1] Short press: Toggle between camera and AI detection");
    ESP_LOGI(TAG, "  - Button[1] Long press: Force return to camera mode (emergency reset)");
    ESP_LOGI(TAG, "  - Other buttons: Disabled for mode control");
    ESP_LOGI(TAG, "Starting in camera display mode...");

#if LOG_MEMORY_SYSTEM_INFO
    static char buffer[2048];
    while (1) {
        sprintf(buffer, "\t  Biggest /     Free /    Total\n"
                " SRAM : [%8d / %8d / %8d]\n"
                "PSRAM : [%8d / %8d / %8d]\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        printf("------------ Memory ------------\n");
        printf("%s\n", buffer);

        ESP_ERROR_CHECK(print_real_time_mem_stats());
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(LOG_TIME_INTERVAL_MS));
    }
#endif
}

#if LOG_TASK_SYSTEM_INFO
#define ARRAY_SIZE_OFFSET                   8   // Increase this if audio_sys_get_real_time_stats returns ESP_ERR_INVALID_SIZE

#define audio_malloc    malloc
#define audio_calloc    calloc
#define audio_free      free
#define AUDIO_MEM_CHECK(tag, x, action) if (x == NULL) { \
        ESP_LOGE(tag, "Memory exhausted (%s:%d)", __FILE__, __LINE__); \
        action; \
    }

const char *task_state[] = {
    "Running",
    "Ready",
    "Blocked",
    "Suspended",
    "Deleted"
};

/** @brief
 * "Extr": Allocated task stack from psram, "Intr": Allocated task stack from internel
 */
const char *task_stack[] = {"Extr", "Intr"};

esp_err_t print_real_time_mem_stats(void)
{
#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    uint32_t total_elapsed_time;
    uint32_t task_elapsed_time, percentage_time;
    esp_err_t ret;

    // Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * start_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });
    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    vTaskDelay(pdMS_TO_TICKS(SYS_TASKS_ELAPSED_TIME_MS));

    // Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t *)audio_malloc(sizeof(TaskStatus_t) * end_array_size);
    AUDIO_MEM_CHECK(TAG, start_array, {
        ret = ESP_FAIL;
        goto exit;
    });

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        goto exit;
    }

    // Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ESP_LOGE(TAG, "Delay duration too short. Trying increasing SYS_TASKS_ELAPSED_TIME_MS");
        ret = ESP_FAIL;
        goto exit;
    }

    ESP_LOGI(TAG, "| Task              | Run Time    | Per | Prio | HWM       | State   | CoreId   | Stack ");

    // Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {

                task_elapsed_time = end_array[j].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
                percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
                ESP_LOGI(TAG, "| %-17s | %-11d |%2d%%  | %-4u | %-9u | %-7s | %-8x | %s",
                                start_array[i].pcTaskName, (int)task_elapsed_time, (int)percentage_time, start_array[i].uxCurrentPriority,
                                (int)start_array[i].usStackHighWaterMark, task_state[(start_array[i].eCurrentState)],
                                start_array[i].xCoreID, task_stack[esp_ptr_internal(pxTaskGetStackStart(start_array[i].xHandle))]);

                // Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
    }

    // Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Deleted", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "| %s | Created", end_array[i].pcTaskName);
        }
    }
    printf("\n");
    ret = ESP_OK;

exit:    // Common return path
    if (start_array) {
        audio_free(start_array);
        start_array = NULL;
    }
    if (end_array) {
        audio_free(end_array);
        end_array = NULL;
    }
    return ret;
#else
    ESP_LOGW(TAG, "Please enbale `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` in menuconfig");
    return ESP_FAIL;
#endif
}
#endif
