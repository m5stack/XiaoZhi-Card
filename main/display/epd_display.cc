#include "epd_display.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"

#include "dual_network_board.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
// #include "lv_port_fs.h"

static const char *TAG = "EpdDisplay";


QueueHandle_t upgrade_queue;

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR       lv_color_hex(0x121212)     // Dark background
#define DARK_TEXT_COLOR             lv_color_white()           // White text
#define DARK_CHAT_BACKGROUND_COLOR  lv_color_hex(0x1E1E1E)     // Slightly lighter than background
#define DARK_USER_BUBBLE_COLOR      lv_color_hex(0x1A6C37)     // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)     // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)     // Medium gray
#define DARK_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)     // Light gray text
#define DARK_BORDER_COLOR           lv_color_hex(0x333333)     // Dark gray border
#define DARK_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)     // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR       lv_color_white()           // White background
#define LIGHT_TEXT_COLOR             lv_color_black()           // Black text
#define LIGHT_CHAT_BACKGROUND_COLOR  lv_color_hex(0xE0E0E0)     // Light gray background
#define LIGHT_USER_BUBBLE_COLOR      lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()           // White
#define LIGHT_SYSTEM_BUBBLE_COLOR    lv_color_hex(0xE0E0E0)     // Light gray
#define LIGHT_SYSTEM_TEXT_COLOR      lv_color_hex(0x666666)     // Dark gray text
#define LIGHT_BORDER_COLOR           lv_color_hex(0xE0E0E0)     // Light gray border
#define LIGHT_LOW_BATTERY_COLOR      lv_color_black()           // Black for light mode

// Add for XiaoZhi-Card Board.
// assert image 
LV_IMG_DECLARE(ui_img_minus_png);         
LV_IMG_DECLARE(ui_img_plus_png);     
LV_IMG_DECLARE(ui_img_sleep_png);         
LV_IMG_DECLARE(ui_img_shutdown_png);    
LV_IMG_DECLARE(ui_img_eye_png);      
LV_IMG_DECLARE(ui_img_tip_png);    
LV_IMG_DECLARE(ui_img_psleep_png);  
LV_IMG_DECLARE(ui_img_line_png); 
LV_IMG_DECLARE(ui_img_assistant_png);  
LV_IMG_DECLARE(ui_img_page2_png);  
LV_IMG_DECLARE(ui_img_arrow_png);  
LV_IMG_DECLARE(ui_img_mbox_png);  
LV_IMG_DECLARE(ui_img_box_png);  
// font
LV_FONT_DECLARE(font_wly_18);
LV_FONT_DECLARE(font_wly_22);
LV_FONT_DECLARE(font_wly_26);
LV_FONT_DECLARE(font_sfy_34);
LV_FONT_DECLARE(font_simple_48);


// Define dark theme colors
const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR
};

// Define light theme colors
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR
};

LV_FONT_DECLARE(font_awesome_30_1);

EpdDisplay::EpdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch, int width, int height,
                       int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : panel_io_(panel_io), panel_(panel), touch_(touch), fonts_(fonts) {
    width_ = width;
    height_ = height;

    // Load theme from settings
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // Update the theme
    if (current_theme_name_ == "dark") {
        current_theme_ = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme_ = LIGHT_THEME;
    }

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16*1024;
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    port_cfg.task_affinity = 1; // NOTE: 
    lvgl_port_init(&port_cfg);

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_L8, 
        .flags = {
            .buff_dma = 0,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 1,
            .direct_mode = 0,  
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }
    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    /* Add touch input */
    ESP_LOGI(TAG, "Adding Touch Indev");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display_,
        .handle = touch_,
    };
    display_indev_ = lvgl_port_add_touch(&touch_cfg);

    GuidePageUI();
    SetupUI();
}

EpdDisplay::EpdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
                       int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : panel_io_(panel_io), panel_(panel), fonts_(fonts) {
    width_ = width;
    height_ = height;

    // Load theme from settings
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // Update the theme
    if (current_theme_name_ == "dark") {
        current_theme_ = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme_ = LIGHT_THEME;
    }

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16*1024;
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    port_cfg.task_affinity = 1; 
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding Display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_L8,  // LV_COLOR_FORMAT_RGB565,//
        .flags = {
            .buff_dma = 0,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 1,
            .direct_mode = 0,  //
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    //lv_port_fs_init();
    SetupUI();
}
 
EpdDisplay::~EpdDisplay() {
    // 然后再清理 LVGL 对象
    if (content_ != nullptr) {
        lv_obj_delete(content_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_delete(status_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_delete(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_delete(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
    }
}

bool EpdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void EpdDisplay::Unlock() {
    lvgl_port_unlock();
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void EpdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto scr_main_ = lv_screen_active();
    lv_obj_set_style_text_font(scr_main_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(scr_main_, current_theme_.text, 0);
    lv_obj_set_style_bg_color(scr_main_, current_theme_.background, 0);

    /* Container */
    container_ = lv_obj_create(scr_main_);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    
    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 10, 0);
    lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0); // Background for chat area
    lv_obj_set_style_border_color(content_, current_theme_.border, 0); // Border color for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    
    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, 10, 0); // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 10, 0);
    lv_obj_set_style_pad_right(status_bar_, 10, 0);
    lv_obj_set_style_pad_top(status_bar_, 2, 0);
    lv_obj_set_style_pad_bottom(status_bar_, 2, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    // 设置状态栏的内容垂直居中
    lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 创建emotion_label_在状态栏最左侧
    emotion_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_style_margin_right(emotion_label_, 5, 0); // 添加右边距，与后面的元素分隔

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
    lv_obj_set_style_margin_left(network_label_, 5, 0); // 添加左边距，与前面的元素分隔

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
    lv_obj_set_style_margin_left(battery_label_, 5, 0); // 添加左边距，与前面的元素分隔

    low_battery_popup_ = lv_obj_create(scr_main_);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}
#if CONFIG_IDF_TARGET_ESP32P4
#define  MAX_MESSAGES 40
#else
#define  MAX_MESSAGES 20
#endif
void EpdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }
    
    //避免出现空的消息框
    if(strlen(content) == 0) return;
    
    // 检查消息数量是否超过限制
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // 删除最早的消息（第一个子对象）
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
        if (first_child != nullptr) {
            lv_obj_delete(first_child);
        }
        // Scroll to the last message immediately
        if (last_child != nullptr) {
            lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
        }
    }
    
    // 折叠系统消息（如果是系统消息，检查最后一个消息是否也是系统消息）
    if (strcmp(role, "system") == 0 && child_count > 0) {
        // 获取最后一个消息容器
        lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
        if (last_container != nullptr && lv_obj_get_child_cnt(last_container) > 0) {
            // 获取容器内的气泡
            lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
            if (last_bubble != nullptr) {
                // 检查气泡类型是否为系统消息
                void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                if (bubble_type_ptr != nullptr && strcmp((const char*)bubble_type_ptr, "system") == 0) {
                    // 如果最后一个消息也是系统消息，则删除它
                    lv_obj_delete(last_container);
                }
            }
        }
    }
    
    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 1, 0);
    lv_obj_set_style_border_color(msg_bubble, current_theme_.border, 0);
    lv_obj_set_style_pad_all(msg_bubble, 8, 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // 计算文本实际宽度
    lv_coord_t text_width = lv_txt_get_width(content, strlen(content), fonts_.text_font, 0);

    // 计算气泡宽度
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 屏幕宽度的85%
    lv_coord_t min_width = 20;  
    lv_coord_t bubble_width;
    
    // 确保文本宽度不小于最小宽度
    if (text_width < min_width) {
        text_width = min_width;
    }

    // 如果文本宽度小于最大宽度，使用文本宽度
    if (text_width < max_width) {
        bubble_width = text_width; 
    } else {
        bubble_width = max_width;
    }
    
    // 设置消息文本的宽度
    lv_obj_set_width(msg_text, bubble_width);  // 减去padding
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(msg_text, fonts_.text_font, 0);

    // 设置气泡宽度
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.user_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme_.text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.assistant_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme_.text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.system_bubble, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, current_theme_.system_text, 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"system");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }
    
    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);
        
        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);
        
        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // 为系统消息创建全宽容器以确保居中对齐
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // 使容器透明且无边框
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // 将消息气泡移入此容器
        lv_obj_set_parent(msg_bubble, container);
        
        // 将气泡居中对齐在容器中
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        
        // 自动滚动底部
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }
    
    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}
#else

/**
 * 引导页面
 */
void scr_guide_event_cb(lv_event_t * e) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);  

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& app = Application::GetInstance();
   
    app.PlaySound(Lang::Sounds::P3_CLICK);
  
    if (btn == display->btn_startup_intro_) {
        lv_screen_load(display->scr_page1_);
    } else if (btn == display->btn_startup_return_) {
        lv_screen_load(display->scr_main_);
        auto async_del = [](void * obj) {
            lv_obj_delete(static_cast<lv_obj_t*>(obj));
        };
        lv_async_call(async_del, display->scr_startup_);
        lv_async_call(async_del, display->scr_page1_);
        lv_async_call(async_del, display->scr_page2_);
        lv_async_call(async_del, display->scr_page3_);
        lv_async_call(async_del, display->scr_page4_);
        lv_async_call(async_del, display->scr_page5_);
        display->FullRefresh();
        if (display->on_click_dont_reming_) { // 不再提示  
            display->on_click_dont_reming_();
        }
    } else if (btn == display->btn_page1_next_) {
        lv_screen_load(display->scr_page2_);
    } else if (btn == display->btn_page2_next_) {
        lv_screen_load(display->scr_page3_);
    } else if (btn == display->btn_page3_next_) {
        lv_screen_load(display->scr_page4_);
    } else if (btn == display->btn_page4_next_) {
        lv_screen_load(display->scr_page5_);
    } else if (btn == display->btn_page5_next_) {
        lv_screen_load(display->scr_main_);
        auto async_del = [](void * obj) {
            lv_obj_delete(static_cast<lv_obj_t*>(obj));
        };
        lv_async_call(async_del, display->scr_startup_);
        lv_async_call(async_del, display->scr_page1_);
        lv_async_call(async_del, display->scr_page2_);
        lv_async_call(async_del, display->scr_page3_);
        lv_async_call(async_del, display->scr_page4_);
        lv_async_call(async_del, display->scr_page5_);
        display->FullRefresh();
    } 
}

/** 
 * 设置页面 
 */
static void scr_setup_event_cb(lv_event_t * e) {
    auto& app = Application::GetInstance();
    //app.Schedule([&app]() {
        app.PlaySound(Lang::Sounds::P3_CLICK);
    //});
    
     lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);  
    Board& board = Board::GetInstance();
    auto display = board.GetDisplay(); 
    auto codec = board.GetAudioCodec();
    if (btn == display->setup_btn_clear_net_) { // 重置 Wi-Fi
        lv_obj_add_flag(display->setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(display->setup_btn_cn_confirm_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(display->setup_btn_cn_cancel_, LV_OBJ_FLAG_HIDDEN);
    } else if (btn == display->setup_btn_sw_net_) { // 切换网络 
        lv_obj_add_flag(display->setup_btn_sw_net_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(display->setup_btn_confirm_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(display->setup_btn_cancel_, LV_OBJ_FLAG_HIDDEN);
    } else if (btn == display->setup_btn_minus_) { // 减小音量
        int vol = codec->output_volume();
        if (vol > 10) { vol -= 10; } else { vol = 0; }
        codec->SetOutputVolume(vol);
        lv_label_set_text_fmt(display->label_volume_, "%d", vol);
    } else if (btn == display->setup_btn_plus_) { // 增大音量 
        int vol = codec->output_volume();
        if (vol <= 90) { vol += 10;} else { vol = 100; }
        codec->SetOutputVolume(vol);
        lv_label_set_text_fmt(display->label_volume_, "%d", vol);
    } else if (btn == display->setup_btn_sleep_) { // 休眠 
        if (display->on_manual_sleep_) {
            display->on_manual_sleep_();
        }
    } else if (btn == display->setup_btn_shutdown_) { // 关机 
        if (display->on_shutdown_) {
            display->on_shutdown_();
        }
    } else if (btn == display->setup_btn_return_) { // 返回主页面  
        if (display->setup_btn_clear_net_) {
            if (lv_obj_has_flag(display->setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(display->setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(display->setup_btn_cn_confirm_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(display->setup_btn_cn_cancel_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (lv_obj_has_flag(display->setup_btn_sw_net_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(display->setup_btn_sw_net_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(display->setup_btn_confirm_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(display->setup_btn_cancel_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_screen_load(display->scr_main_);
        display->FullRefresh();
    } else if (btn == display->setup_btn_auto_sleep_) { // 自动休眠开/关 
        if (display->on_auto_sleep_changed_) {
            display->on_auto_sleep_changed_();
        }
    } 
} 

/**
 * 主页面   
 */

static void scr_main_event_cb(lv_event_t * e) {
    lv_event_code_t event = lv_event_get_code(e);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& app = Application::GetInstance();
    static bool pause = false;

    if (event == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
        auto& app = Application::GetInstance();
        if (dir == LV_DIR_BOTTOM) { // 下滑
            ESP_LOGI("Gesture", "Swipe down detected");
            DeviceState state = app.GetDeviceState();
            if (state == kDeviceStateUpgrading) {
                return;
            }

            //app.Schedule([&app]() {
            app.PlaySound(Lang::Sounds::P3_CLICK);
            //});
            
            // 音量
            auto codec = board.GetAudioCodec();
            int vol = codec->output_volume();
            lv_label_set_text_fmt(display->label_volume_, "%d", vol);
            // 网络类型
            std::string board_type = board.GetBoardType();
            if (board_type == "ml307") {
                lv_label_set_text(display->setup_label_net_, "切换网络为 Wi-Fi");
                if (display->setup_btn_clear_net_) {
                    lv_obj_delete(display->setup_btn_clear_net_);
                    lv_obj_delete(display->setup_btn_cn_confirm_);
                    lv_obj_delete(display->setup_btn_cn_cancel_);
                    display->setup_btn_clear_net_ = nullptr;
                    display->setup_btn_cn_confirm_ = nullptr;
                    display->setup_btn_cn_cancel_ = nullptr;
                }
            } else if (board_type == "wifi") {
                lv_label_set_text(display->setup_label_net_, "切换网络为 4G");
                lv_obj_clear_flag(display->setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN);
            }  
            // 电量 
            int level = 0;
            bool charge, discahrge;
            board.GetBatteryLevel(level, charge, discahrge);
            lv_label_set_text_fmt(display->setup_label_battery_, "%d", level);
            // 自动休眠 
            if (board.GetPowerSaveMode()) {
                lv_label_set_text(display->setup_label_auto_sleep_, "关闭自动休眠");
            } else {
                lv_label_set_text(display->setup_label_auto_sleep_, "开启自动休眠");
            }
            // 
            lv_screen_load(display->scr_setup_);
        } else if (dir == LV_DIR_LEFT) { // 左滑
            display->FullRefresh();
        } else if (dir == LV_DIR_RIGHT) { // 右滑
            display->FullRefresh();
        }
    } else if (event == LV_EVENT_CLICKED) {  
        app.PlaySound(Lang::Sounds::P3_CLICK);
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);  
        auto codec = Board::GetInstance().GetAudioCodec();
        if (btn == display->main_btn_chat_) { // 对话，退出，暂停，继续 
            DeviceState state = app.GetDeviceState();
            if (state == kDeviceStateSpeaking) {  
                pause = !pause; // 暂停/继续 
                if (pause) {
                    codec->EnableOutput(false);  
                    app.PausePlay(true);
                    display->ShowNotification("已暂停");
                    lv_label_set_text(display->main_btn_chat_label_, "继续");
                    lv_obj_add_flag(display->content_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(display->main_btn_new_chat_, LV_OBJ_FLAG_HIDDEN);
                } else {
                    codec->EnableOutput(true);  
                    app.PausePlay(false);
                    display->ShowNotification("说话中..."); 
                    lv_label_set_text(display->main_btn_chat_label_, "暂停");
                    lv_obj_remove_flag(display->content_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(display->main_btn_new_chat_, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                codec->EnableOutput(true);  
                lv_obj_remove_flag(display->content_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(display->main_btn_new_chat_, LV_OBJ_FLAG_HIDDEN);
                app.ToggleChatState(); // 切换对话状态 
                app.PausePlay(false);
            }
        } else if (btn == display->main_btn_new_chat_) { // 新对话 
            codec->EnableOutput(true);  
            lv_obj_add_flag(display->main_btn_new_chat_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(display->chat_message_label_, ""); // 清空对话信息 
            lv_obj_remove_flag(display->content_, LV_OBJ_FLAG_HIDDEN);
            app.ToggleChatState(); 
            pause = false;
        } 
    }
}

/**
 * 开机引导页 
 */
void EpdDisplay::GuidePageUI()
{
    DisplayLockGuard lock(this);

    lv_obj_t *label = nullptr;
    lv_obj_t *img = nullptr;

    font_18_ = &font_wly_18; // lv_binfont_create("P:/sdcard/wly_18.bin");
    ESP_LOGI(TAG, "%s", font_18_ ? "wly_18 loaded" : "load wly_18 failed!");

    font_22_ = &font_wly_22; // lv_binfont_create("P:/sdcard/wly_22.bin");
    ESP_LOGI(TAG, "%s", font_22_ ? "wly_22 loaded" : "load wly_22 failed!");

    font_26_ = &font_wly_26; // lv_binfont_create("P:/sdcard/wly_26.bin");
    ESP_LOGI(TAG, "%s", font_26_ ? "wly_26 loaded" : "load wly_26 failed!");

    font_34_ = &font_sfy_34; // lv_binfont_create("P:/sdcard/jfy_34.bin");
    ESP_LOGI(TAG, "%s", font_34_ ? "jfy_34 loaded" : "load jfy_34 failed!");

    font_48_ = &font_simple_48; //lv_binfont_create("P:/sdcard/simple_48.bin");
    ESP_LOGI(TAG, "%s", font_48_ ? "simple_48 loaded" : "load simple_48 failed!");

    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP);     
    lv_style_set_border_color(&style_btn, lv_color_black());
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_radius(&style_btn, 10);                
    lv_style_set_pad_all(&style_btn, 10);   
    
    static lv_style_t style_label;
    lv_style_init(&style_label);
    lv_style_set_text_line_space(&style_label, 5);
 
//================================================================
// 开机引导页 
//================================================================
    scr_startup_ = lv_obj_create(NULL);

    lv_obj_t *img_eye_l = lv_img_create(scr_startup_);
    lv_img_set_src(img_eye_l, &ui_img_eye_png);
    lv_obj_set_size(img_eye_l, 13, 21); 
    lv_obj_align(img_eye_l, LV_ALIGN_TOP_MID, -13, 26); 

    lv_obj_t *img_eye_r = lv_img_create(scr_startup_);
    lv_img_set_src(img_eye_r, &ui_img_eye_png);
    lv_obj_set_size(img_eye_r, 13, 21); 
    lv_obj_align(img_eye_r, LV_ALIGN_TOP_MID, 13, 26); 

    label = lv_label_create(scr_startup_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 68); 
    lv_label_set_text(label, "欢迎使用");

    label = lv_label_create(scr_startup_);
    lv_obj_set_style_text_font(label, font_34_, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 105); 
    lv_label_set_text(label, "小智墨伴");

    btn_startup_intro_ = lv_button_create(scr_startup_);
    lv_obj_set_size(btn_startup_intro_, 108, 40);
    lv_obj_align(btn_startup_intro_, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_clear_flag(btn_startup_intro_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);; 
    lv_obj_add_style(btn_startup_intro_, &style_btn, 0);
    lv_obj_add_event_cb(btn_startup_intro_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_startup_intro_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "进入向导");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

    btn_startup_return_ = lv_button_create(scr_startup_);
    lv_obj_set_size(btn_startup_return_, 108, 40);
    lv_obj_align(btn_startup_return_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_startup_return_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_startup_return_, &style_btn, 0);
    lv_obj_add_event_cb(btn_startup_return_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_startup_return_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "不再提示");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

//================================================================
    scr_page1_ = lv_obj_create(NULL); 

    label = lv_label_create(scr_page1_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "信号");
    lv_obj_set_pos(label, 2, 25);
    label = lv_label_create(scr_page1_);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_set_style_text_font(label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(label, current_theme_.text, 0);
    lv_label_set_text(label, FONT_AWESOME_SIGNAL_FULL);

    label = lv_label_create(scr_page1_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "12:34");

    label = lv_label_create(scr_page1_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "-------------------------");

    label = lv_label_create(scr_page1_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "电量");
    lv_obj_set_pos(label, 140, 25);
    
    label = lv_label_create(scr_page1_);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -10, 4);
    lv_obj_set_style_text_font(label, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(label, current_theme_.text, 0);
    lv_label_set_text(label, FONT_AWESOME_BATTERY_FULL);

    img = lv_img_create(scr_page1_);
    lv_img_set_src(img, &ui_img_line_png);
    lv_obj_set_size(img, 36, 90); 
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 30); 

    label = lv_label_create(scr_page1_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "状态栏");
    lv_obj_set_pos(label, 100, 108);

    label = lv_label_create(scr_page1_);
    lv_obj_set_style_text_font(label, font_48_, 0);
    lv_label_set_text(label, "1");
    lv_obj_set_pos(label, 130, 138);

    btn_page1_next_ = lv_button_create(scr_page1_);
    lv_obj_set_size(btn_page1_next_, 108, 40);
    lv_obj_align(btn_page1_next_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_page1_next_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_page1_next_, &style_btn, 0);
    lv_obj_add_event_cb(btn_page1_next_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_page1_next_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "下一页");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);
        
//================================================================
    scr_page2_ = lv_obj_create(NULL); 

    img = lv_img_create(scr_page2_);
    lv_img_set_src(img, &ui_img_page2_png);
    lv_obj_set_size(img, 176, 114); 
    lv_obj_set_pos(img, 0, 0);

    label = lv_label_create(scr_page2_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "系统设置");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 28);

    label = lv_label_create(scr_page2_);
    lv_obj_set_style_text_font(label, font_48_, 0);
    lv_label_set_text(label, "2");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 65);

    img = lv_img_create(scr_page2_);
    lv_img_set_src(img, &ui_img_arrow_png);
    lv_obj_set_size(img, 90, 49); 
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 118);
        
    label = lv_label_create(img);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "下滑");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 5);

    btn_page2_next_ = lv_button_create(scr_page2_);
    lv_obj_set_size(btn_page2_next_, 108, 40);
    lv_obj_align(btn_page2_next_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_page2_next_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_page2_next_, &style_btn, 0);
    lv_obj_add_event_cb(btn_page2_next_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_page2_next_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "下一页");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

//================================================================
    scr_page3_ = lv_obj_create(NULL); 

    lv_obj_t *img_mbox = lv_img_create(scr_page3_);
    lv_img_set_src(img_mbox, &ui_img_mbox_png);
    lv_obj_set_size(img_mbox, 156, 59); 
    lv_obj_set_pos(img_mbox, 10, 12);

    label = lv_label_create(scr_page3_);
    lv_obj_set_style_text_font(label, font_26_, 0);
    lv_label_set_text(label, "\“你好小智\”");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 22);

    label = lv_label_create(scr_page3_);
    lv_obj_set_style_text_font(label, font_18_, 0);
    lv_label_set_text(label, "请对我说");
    lv_obj_set_pos(label, 10, 75);

    label = lv_label_create(scr_page3_);
    lv_obj_set_style_text_font(label, font_18_, 0);
    lv_label_set_text(label, "或点击");
    lv_obj_set_pos(label, 99, 98);
    
    img = lv_img_create(scr_page3_);
    lv_img_set_src(img, &ui_img_box_png);
    lv_obj_set_size(img, 68, 38); 
    lv_obj_set_pos(img, 95, 119);
    label = lv_label_create(img);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "唤醒");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -2);

    label = lv_label_create(scr_page3_);
    lv_obj_set_style_text_font(label, font_48_, 0);
    lv_label_set_text(label, "3");
    lv_obj_set_pos(label, 20, 107);

    label = lv_label_create(scr_page3_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "即可唤醒我！");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 5, 175);

    btn_page3_next_ = lv_button_create(scr_page3_);
    lv_obj_set_size(btn_page3_next_, 108, 40);
    lv_obj_align(btn_page3_next_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_page3_next_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_page3_next_, &style_btn, 0);
    lv_obj_add_event_cb(btn_page3_next_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_page3_next_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "下一页");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

//================================================================
    scr_page4_ = lv_obj_create(NULL); 
    
    img = lv_img_create(scr_page4_);
    lv_img_set_src(img, &ui_img_tip_png);
    lv_obj_set_size(img, 28, 28); 
    lv_obj_set_pos(img, 8, 27);

    label = lv_label_create(scr_page4_);
    lv_obj_set_style_text_font(label, font_22_, 0); 
    lv_obj_set_pos(label, 40, 29);
    lv_label_set_text(label, "提醒");

    label = lv_label_create(scr_page4_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_add_style(label, &style_label, 0);      
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 73);
    lv_label_set_text(label, "如果一段时间\n不使用我\n我会自动\n进入休眠哦");

    btn_page4_next_ = lv_button_create(scr_page4_);
    lv_obj_set_size(btn_page4_next_, 108, 40);
    lv_obj_align(btn_page4_next_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_page4_next_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_page4_next_, &style_btn, 0);
    lv_obj_add_event_cb(btn_page4_next_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_page4_next_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "知道了");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);
        
//================================================================
    scr_page5_ = lv_obj_create(NULL); 

    lv_obj_t *img_eye = lv_img_create(scr_page5_);
    lv_img_set_src(img_eye, &ui_img_eye_png);
    lv_obj_align(img_eye, LV_ALIGN_TOP_MID, -13, 45); 

    img_eye = lv_img_create(scr_page5_);
    lv_img_set_src(img_eye, &ui_img_eye_png);
    lv_obj_align(img_eye, LV_ALIGN_TOP_MID, 13, 45); 

    label = lv_label_create(scr_page5_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_add_style(label, &style_label, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_size(label, 100, 30);
    lv_label_set_text(label, "马上体验");
    label = lv_label_create(scr_page5_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_add_style(label, &style_label, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 6, 132);
    lv_label_set_text(label, "奇妙旅程吧！");

    btn_page5_next_ = lv_button_create(scr_page5_);
    lv_obj_set_size(btn_page5_next_, 108, 40);
    lv_obj_align(btn_page5_next_, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_clear_flag(btn_page5_next_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_style(btn_page5_next_, &style_btn, 0);
    lv_obj_add_event_cb(btn_page5_next_, scr_guide_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_page5_next_);
    lv_obj_set_style_text_font(label, font_22_, 0);
    lv_label_set_text(label, "开始");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);  
}
 
void EpdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    lv_obj_t *label = nullptr;

    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP);     
    lv_style_set_border_color(&style_btn, lv_color_black());
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_radius(&style_btn, 10);                
    lv_style_set_pad_all(&style_btn, 10);   
 
//================================================================
// 提示页  
//================================================================
    scr_tip_ = lv_obj_create(NULL); 

    scr_tip_label_title_ = lv_label_create(scr_tip_);
    lv_obj_set_style_text_font(scr_tip_label_title_, fonts_.text_font, 0);
    lv_label_set_text(scr_tip_label_title_, "充电中不能关机");
    lv_obj_align(scr_tip_label_title_, LV_ALIGN_TOP_MID, 0, 5);

    scr_tip_label_ = lv_label_create(scr_tip_);
    lv_obj_set_style_text_font(scr_tip_label_, fonts_.text_font, 0);
    lv_label_set_text(scr_tip_label_, "充电中不能关机");
    lv_obj_align(scr_tip_label_, LV_ALIGN_TOP_MID, 0, 35);

//================================================================
// 主页面 
//================================================================
    scr_main_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(scr_main_, fonts_.text_font, 0);
    lv_obj_set_style_text_color(scr_main_, current_theme_.text, 0);
    lv_obj_set_style_bg_color(scr_main_, current_theme_.background, 0);

    /* Container */
    container_ = lv_obj_create(scr_main_);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);  
 
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    // lv_obj_set_height(content_, LV_VER_RES * 0.8);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 5, 0);
    lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
    lv_obj_set_style_border_color(content_, current_theme_.border, 0); // Border color for content

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    // lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_style_pad_left(emotion_label_, 2, 0);

    preview_image_ = lv_image_create(content_);
    lv_obj_set_size(preview_image_, width_ * 0.5, height_ * 0.5);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9); // 限制宽度为屏幕宽度的 90%
    lv_obj_set_height(chat_message_label_, LV_VER_RES * 0.4); // 限制高度，防止与按钮区域重叠 -- 文字被遮挡，待处理  
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);         
    
    /* 是否升级选择 */
    main_btn_confirm_upgrade_ = lv_btn_create(scr_main_);
    lv_obj_remove_style_all(main_btn_confirm_upgrade_);  
    lv_obj_set_size(main_btn_confirm_upgrade_, 108, 40);
    lv_obj_align(main_btn_confirm_upgrade_, LV_ALIGN_CENTER, 0, -10);
    lv_obj_clear_flag(main_btn_confirm_upgrade_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_event_cb(main_btn_confirm_upgrade_, [](lv_event_t* e) {
        auto& app = Application::GetInstance();
        app.PlaySound(Lang::Sounds::P3_CLICK);
        int upgrade = 1;
        if (upgrade_queue) xQueueSend(upgrade_queue, &upgrade, 1);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_style(main_btn_confirm_upgrade_, &style_btn, 0);
    lv_obj_add_flag(main_btn_confirm_upgrade_, LV_OBJ_FLAG_HIDDEN);
    label = lv_label_create(main_btn_confirm_upgrade_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "确认升级");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    main_btn_skip_upgrade_ = lv_btn_create(scr_main_);
    lv_obj_remove_style_all(main_btn_skip_upgrade_); 
    lv_obj_set_size(main_btn_skip_upgrade_, 108, 40);
    lv_obj_align(main_btn_skip_upgrade_, LV_ALIGN_CENTER, 0, 50);
    lv_obj_clear_flag(main_btn_skip_upgrade_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_event_cb(main_btn_skip_upgrade_, [](lv_event_t* e) {
        auto& app = Application::GetInstance();
        app.PlaySound(Lang::Sounds::P3_CLICK);
        int upgrade = 0;
        if (upgrade_queue) xQueueSend(upgrade_queue, &upgrade, 1);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_style(main_btn_skip_upgrade_, &style_btn, 0);
    lv_obj_add_flag(main_btn_skip_upgrade_, LV_OBJ_FLAG_HIDDEN);
    label = lv_label_create(main_btn_skip_upgrade_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "暂不升级");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);

    /* 切换对话状态 */
    main_btn_chat_ = lv_btn_create(scr_main_);
    lv_obj_remove_style_all(main_btn_chat_);  
    lv_obj_align(main_btn_chat_, LV_ALIGN_TOP_MID, 0, 215);
    lv_obj_set_size(main_btn_chat_, 108, 40);
    lv_obj_clear_flag(main_btn_chat_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_event_cb(main_btn_chat_, scr_main_event_cb, LV_EVENT_CLICKED, NULL); 
    lv_obj_add_style(main_btn_chat_, &style_btn, 0);
    main_btn_chat_label_ = lv_label_create(main_btn_chat_);
    lv_label_set_text(main_btn_chat_label_, "对话");
    lv_obj_center(main_btn_chat_label_);
    lv_obj_set_style_text_color(main_btn_chat_label_, lv_color_black(), 0);
    lv_obj_add_flag(main_btn_chat_, LV_OBJ_FLAG_HIDDEN);

    main_btn_new_chat_ = lv_btn_create(scr_main_);
    lv_obj_remove_style_all(main_btn_new_chat_); 
    lv_obj_align(main_btn_new_chat_, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_size(main_btn_new_chat_, 108, 40);
    lv_obj_clear_flag(main_btn_new_chat_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); 
    lv_obj_add_event_cb(main_btn_new_chat_, scr_main_event_cb, LV_EVENT_CLICKED, NULL); 
    lv_obj_add_style(main_btn_new_chat_, &style_btn, 0);
    label = lv_label_create(main_btn_new_chat_);
    lv_label_set_text(label, "新对话");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_add_flag(main_btn_new_chat_, LV_OBJ_FLAG_HIDDEN);

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
    lv_obj_set_style_pad_left(network_label_, 2, 0); // 内移两个像素 

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    //lv_obj_set_style_pad_left(notification_label_, 3, 0); 

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    //lv_obj_set_style_pad_left(status_label_, 3, 0); 

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
    lv_obj_set_style_pad_top(battery_label_, 2, 0);  
    lv_obj_set_style_pad_right(battery_label_, 2, 0); // 内移两个像素 

    low_battery_popup_ = lv_obj_create(scr_main_);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // 添加手势触发回调 
    lv_obj_add_event_cb(scr_main_, scr_main_event_cb, LV_EVENT_GESTURE, NULL);
 
//================================================================
// 设置页面  
//================================================================
    scr_setup_ = lv_obj_create(NULL); 

    setup_btn_clear_net_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_clear_net_);
    lv_obj_set_size(setup_btn_clear_net_, 160, 30);
    lv_obj_align(setup_btn_clear_net_, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_add_event_cb(setup_btn_clear_net_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_clear_net_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "重新配置 Wi-Fi");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);  
    lv_obj_add_flag(setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN); 

    setup_btn_cn_confirm_ = lv_button_create(scr_setup_); // 清除网络配置 
    lv_obj_remove_style_all(setup_btn_cn_confirm_);
    lv_obj_set_size(setup_btn_cn_confirm_, 72, 34);
    lv_obj_align(setup_btn_cn_confirm_, LV_ALIGN_TOP_MID, -44, 1); 
    lv_obj_add_event_cb(setup_btn_cn_confirm_, [](lv_event_t* e) {
        Board& board = Board::GetInstance();
        auto display = board.GetDisplay();
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            app.PlaySound(Lang::Sounds::P3_CLICK);
        });
        if (display->on_clear_network_) {
            display->on_clear_network_();
        }
    }, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_cn_confirm_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "确认");
    lv_obj_center(label);
    lv_obj_add_flag(setup_btn_cn_confirm_, LV_OBJ_FLAG_HIDDEN); 

    setup_btn_cn_cancel_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_cn_cancel_);
    lv_obj_set_size(setup_btn_cn_cancel_, 72, 34);
    lv_obj_align(setup_btn_cn_cancel_, LV_ALIGN_TOP_MID, 44, 1);
    lv_obj_add_event_cb(setup_btn_cn_cancel_, [](lv_event_t* e) {
        Board& board = Board::GetInstance();
        auto display = board.GetDisplay();
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            app.PlaySound(Lang::Sounds::P3_CLICK);
        });
        lv_obj_clear_flag(display->setup_btn_clear_net_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display->setup_btn_cn_confirm_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display->setup_btn_cn_cancel_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_cn_cancel_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "取消");
    lv_obj_center(label);
    lv_obj_add_flag(setup_btn_cn_cancel_, LV_OBJ_FLAG_HIDDEN); 

    setup_btn_sw_net_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_sw_net_);
    lv_obj_set_size(setup_btn_sw_net_, 160, 34);
    lv_obj_align(setup_btn_sw_net_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_event_cb(setup_btn_sw_net_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    setup_label_net_ = lv_label_create(setup_btn_sw_net_);
    lv_obj_set_style_text_font(setup_label_net_, fonts_.text_font, 0);
    lv_label_set_text(setup_label_net_, "");
    lv_obj_set_style_text_color(setup_label_net_, lv_color_black(), 0);
    lv_obj_center(setup_label_net_);  

    setup_btn_confirm_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_confirm_);
    lv_obj_set_size(setup_btn_confirm_, 72, 34);
    lv_obj_align(setup_btn_confirm_, LV_ALIGN_TOP_MID, -44, 30); 
    lv_obj_add_event_cb(setup_btn_confirm_, [](lv_event_t* e) {
        Board& board = Board::GetInstance();
        auto display = board.GetDisplay();
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            app.PlaySound(Lang::Sounds::P3_CLICK);
        });
        if (display->on_switch_network_) {
            display->on_switch_network_();
        }
    }, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_confirm_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "确认");
    lv_obj_center(label);
    lv_obj_add_flag(setup_btn_confirm_, LV_OBJ_FLAG_HIDDEN); 

    setup_btn_cancel_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_cancel_);
    lv_obj_set_size(setup_btn_cancel_, 72, 34);
    lv_obj_align(setup_btn_cancel_, LV_ALIGN_TOP_MID, 44, 30);
    lv_obj_add_event_cb(setup_btn_cancel_, [](lv_event_t* e) {
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            app.PlaySound(Lang::Sounds::P3_CLICK);
        });
        Board& board = Board::GetInstance();
        auto display = board.GetDisplay();
        lv_obj_clear_flag(display->setup_btn_sw_net_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display->setup_btn_confirm_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display->setup_btn_cancel_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_cancel_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "取消");
    lv_obj_center(label);
    lv_obj_add_flag(setup_btn_cancel_, LV_OBJ_FLAG_HIDDEN); 

    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "------------------------");

    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "------------------------");
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "音量");
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "------------------------");

    label_volume_ = lv_label_create(scr_setup_);
    lv_obj_align(label_volume_, LV_ALIGN_TOP_MID, 0, 87);
    lv_obj_set_style_text_font(label_volume_, fonts_.text_font, 0);
    lv_label_set_text(label_volume_, "");

    setup_btn_minus_ = lv_imagebutton_create(scr_setup_);
    lv_imagebutton_set_src(setup_btn_minus_, LV_IMAGEBUTTON_STATE_RELEASED, NULL, &ui_img_minus_png, NULL);
    lv_obj_set_size(setup_btn_minus_, 46, 46);
    lv_obj_align(setup_btn_minus_, LV_ALIGN_TOP_MID, -55, 66);
    lv_obj_add_event_cb(setup_btn_minus_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);

    setup_btn_plus_ = lv_imagebutton_create(scr_setup_);
    lv_imagebutton_set_src(setup_btn_plus_, LV_IMAGEBUTTON_STATE_RELEASED, NULL, &ui_img_plus_png, NULL);
    lv_obj_set_size(setup_btn_plus_, 46, 46);
    lv_obj_align(setup_btn_plus_, LV_ALIGN_TOP_MID, 55, 66);
    lv_obj_add_event_cb(setup_btn_plus_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);

    setup_btn_auto_sleep_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_auto_sleep_);
    lv_obj_set_size(setup_btn_auto_sleep_, 160, 32);
    lv_obj_align(setup_btn_auto_sleep_, LV_ALIGN_TOP_MID, 0, 111);
    lv_obj_add_event_cb(setup_btn_auto_sleep_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    setup_label_auto_sleep_ = lv_label_create(setup_btn_auto_sleep_);
    lv_obj_set_style_text_font(setup_label_auto_sleep_, fonts_.text_font, 0);
    lv_label_set_text(setup_label_auto_sleep_, "关闭自动休眠");
    lv_obj_set_style_text_color(setup_label_auto_sleep_, lv_color_black(), 0);
    lv_obj_center(setup_label_auto_sleep_);  
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 135);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "------------------------");

    setup_btn_sleep_ = lv_imagebutton_create(scr_setup_);
    lv_imagebutton_set_src(setup_btn_sleep_, LV_IMAGEBUTTON_STATE_RELEASED, NULL, &ui_img_sleep_png, NULL);
    lv_obj_set_size(setup_btn_sleep_, 40, 40);
    lv_obj_align(setup_btn_sleep_, LV_ALIGN_TOP_MID, -50, 156);
    lv_obj_add_event_cb(setup_btn_sleep_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, -50, 198);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "休眠");

    setup_btn_shutdown_ = lv_imagebutton_create(scr_setup_);
    lv_imagebutton_set_src(setup_btn_shutdown_, LV_IMAGEBUTTON_STATE_RELEASED, NULL, &ui_img_shutdown_png, NULL);
    lv_obj_set_size(setup_btn_shutdown_, 40, 40);
    lv_obj_align(setup_btn_shutdown_, LV_ALIGN_TOP_MID, 50, 156);
    lv_obj_add_event_cb(setup_btn_shutdown_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 50, 198);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "关机");
        
    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "------------------------");
    setup_btn_return_ = lv_button_create(scr_setup_);
    lv_obj_remove_style_all(setup_btn_return_);
    lv_obj_set_size(setup_btn_return_, 108, 40);
    lv_obj_align(setup_btn_return_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(setup_btn_return_, scr_setup_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(setup_btn_return_);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "> 返回 <");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);  

    label = lv_label_create(scr_setup_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_set_style_text_font(label, fonts_.text_font, 0);
    lv_label_set_text(label, "电量");
    setup_label_battery_ = lv_label_create(scr_setup_);
    lv_obj_align(setup_label_battery_, LV_ALIGN_TOP_MID, 0, 188);
    lv_obj_set_style_text_font(setup_label_battery_, fonts_.text_font, 0);
    lv_label_set_text(setup_label_battery_, "");
    
//================================================================
// 关机页面 
//================================================================
    scr_shutdown_ = lv_obj_create(NULL); 
    lv_obj_set_style_text_font(scr_shutdown_, fonts_.text_font, 0);
    label = lv_label_create(scr_shutdown_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);
    lv_label_set_text(label, "已关机");

    lv_obj_t *img_assistant = lv_img_create(scr_shutdown_);
    lv_img_set_src(img_assistant, &ui_img_assistant_png);
    lv_obj_set_size(img_assistant, 170, 195); 
    lv_obj_align(img_assistant, LV_ALIGN_TOP_MID, 0, 40); 

//================================================================
// 休眠页面 
//================================================================
    scr_sleep_ = lv_obj_create(NULL); 
    lv_obj_set_style_text_font(scr_sleep_, fonts_.text_font, 0);
    label = lv_label_create(scr_sleep_);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);
    lv_label_set_text(label, "休眠中");
    
    lv_obj_t *img_psleep = lv_img_create(scr_sleep_);
    lv_img_set_src(img_psleep, &ui_img_psleep_png);
    lv_obj_set_size(img_psleep, 156, 118); 
    lv_obj_align(img_psleep, LV_ALIGN_TOP_MID, 0, 70); 
}

#endif

void EpdDisplay::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {"😶", "neutral"},
        {"🙂", "happy"},
        {"😆", "laughing"},
        {"😂", "funny"},
        {"😔", "sad"},
        {"😠", "angry"},
        {"😭", "crying"},
        {"😍", "loving"},
        {"😳", "embarrassed"},
        {"😯", "surprised"},
        {"😱", "shocked"},
        {"🤔", "thinking"},
        {"😉", "winking"},
        {"😎", "cool"},
        {"😌", "relaxed"},
        {"🤤", "delicious"},
        {"😘", "kissy"},
        {"😏", "confident"},
        {"😴", "sleepy"},
        {"😜", "silly"},
        {"🙄", "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });

    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    lv_obj_set_style_text_font(emotion_label_, fonts_.emoji_font, 0);
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, "😶");
    }
    
    // 显示emotion_label_，隐藏preview_image_
    lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EpdDisplay::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    lv_label_set_text(emotion_label_, icon);
    
    // 显示emotion_label_，隐藏preview_image_
    lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EpdDisplay::SetPreviewImage(const lv_img_dsc_t* img_dsc) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        return;
    }
    
    if (img_dsc != nullptr) {
        // zoom factor 0.5
        lv_img_set_zoom(preview_image_, 128 * width_ / img_dsc->header.w);
        // 设置图片源并显示预览图片
        lv_img_set_src(preview_image_, img_dsc);
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        // 隐藏emotion_label_
        if (emotion_label_ != nullptr) {
            lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // 隐藏预览图片并显示emotion_label_
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        if (emotion_label_ != nullptr) {
            lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void EpdDisplay::SetTheme(const std::string& theme_name) {
    return; // 禁用深色模式

    DisplayLockGuard lock(this);
    
    if (theme_name == "dark" || theme_name == "DARK") {
        current_theme_ = DARK_THEME;
    } else if (theme_name == "light" || theme_name == "LIGHT") {
        current_theme_ = LIGHT_THEME;
    } else {
        // Invalid theme name, return false
        ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str());
        return;
    }
    
    // Get the active scr_main_
    lv_obj_t* scr_main_ = lv_screen_active();
    
    // Update the scr_main_ colors
    lv_obj_set_style_bg_color(scr_main_, current_theme_.background, 0);
    lv_obj_set_style_text_color(scr_main_, current_theme_.text, 0);
    
    // Update container colors
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
        lv_obj_set_style_border_color(container_, current_theme_.border, 0);
    }
    
    // Update status bar colors
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
        
        // Update status bar elements
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
        }
        if (emotion_label_ != nullptr) {
            lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
        }
    }
    
    // Update content area colors
    if (content_ != nullptr) {
        lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
        lv_obj_set_style_border_color(content_, current_theme_.border, 0);
        
        // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // Iterate through all children of content (message containers or bubbles)
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* obj = lv_obj_get_child(content_, i);
            if (obj == nullptr) continue;
            
            lv_obj_t* bubble = nullptr;
            
            // 检查这个对象是容器还是气泡
            // 如果是容器（用户或系统消息），则获取其子对象作为气泡
            // 如果是气泡（助手消息），则直接使用
            if (lv_obj_get_child_cnt(obj) > 0) {
                // 可能是容器，检查它是否为用户或系统消息容器
                // 用户和系统消息容器是透明的
                lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
                if (bg_opa == LV_OPA_TRANSP) {
                    // 这是用户或系统消息的容器
                    bubble = lv_obj_get_child(obj, 0);
                } else {
                    // 这可能是助手消息的气泡自身
                    bubble = obj;
                }
            } else {
                // 没有子元素，可能是其他UI元素，跳过
                continue;
            }
            
            if (bubble == nullptr) continue;
            
            // 使用保存的用户数据来识别气泡类型
            void* bubble_type_ptr = lv_obj_get_user_data(bubble);
            if (bubble_type_ptr != nullptr) {
                const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
                
                // 根据气泡类型应用正确的颜色
                if (strcmp(bubble_type, "user") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.user_bubble, 0);
                } else if (strcmp(bubble_type, "assistant") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.assistant_bubble, 0); 
                } else if (strcmp(bubble_type, "system") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }
                
                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme_.border, 0);
                
                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0) {
                    lv_obj_t* text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr) {
                        // 根据气泡类型设置文本颜色
                        if (strcmp(bubble_type, "system") == 0) {
                            lv_obj_set_style_text_color(text, current_theme_.system_text, 0);
                        } else {
                            lv_obj_set_style_text_color(text, current_theme_.text, 0);
                        }
                    }
                }
            } else {
                // 如果没有标记，回退到之前的逻辑（颜色比较）
                // ...保留原有的回退逻辑...
                lv_color_t bg_color = lv_obj_get_style_bg_color(bubble, 0);
            
                // 改进bubble类型检测逻辑，不仅使用颜色比较
                bool is_user_bubble = false;
                bool is_assistant_bubble = false;
                bool is_system_bubble = false;
            
                // 检查用户bubble
                if (lv_color_eq(bg_color, DARK_USER_BUBBLE_COLOR) || 
                    lv_color_eq(bg_color, LIGHT_USER_BUBBLE_COLOR) ||
                    lv_color_eq(bg_color, current_theme_.user_bubble)) {
                    is_user_bubble = true;
                }
                // 检查系统bubble
                else if (lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) || 
                         lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR) ||
                         lv_color_eq(bg_color, current_theme_.system_bubble)) {
                    is_system_bubble = true;
                }
                // 剩余的都当作助手bubble处理
                else {
                    is_assistant_bubble = true;
                }
            
                // 根据bubble类型应用正确的颜色
                if (is_user_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.user_bubble, 0);
                } else if (is_assistant_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.assistant_bubble, 0);
                } else if (is_system_bubble) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }
                
                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme_.border, 0);
                
                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0) {
                    lv_obj_t* text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr) {
                        // 回退到颜色检测逻辑
                        if (lv_color_eq(bg_color, current_theme_.system_bubble) ||
                            lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) || 
                            lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR)) {
                            lv_obj_set_style_text_color(text, current_theme_.system_text, 0);
                        } else {
                            lv_obj_set_style_text_color(text, current_theme_.text, 0);
                        }
                    }
                }
            }
        }
#else
        // Simple UI mode - just update the main chat message
        if (chat_message_label_ != nullptr) {
            lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
        }
        
        if (emotion_label_ != nullptr) {
            lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
        }
#endif
    }
    
    // Update low battery popup
    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    }

    // No errors occurred. Save theme to settings
    Display::SetTheme(theme_name);
}

 