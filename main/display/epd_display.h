#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <font_emoji.h>
#include "application.h"

#include <atomic>

// Theme color structure
struct ThemeColors {
    lv_color_t background;
    lv_color_t text;
    lv_color_t chat_background;
    lv_color_t user_bubble;
    lv_color_t assistant_bubble;
    lv_color_t system_bubble;
    lv_color_t system_text;
    lv_color_t border;
    lv_color_t low_battery;
};


class EpdDisplay : public Display {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;


    //const lv_font_t *font_16_ = nullptr;
    const lv_font_t *font_18_ = nullptr;
    const lv_font_t *font_20_ = nullptr;
    const lv_font_t *font_22_ = nullptr;
    const lv_font_t *font_24_ = nullptr;
    const lv_font_t *font_26_ = nullptr;
    const lv_font_t *font_32_ = nullptr;
    const lv_font_t *font_34_ = nullptr;
    //const lv_font_t *font_36_ = nullptr;
    //const lv_font_t *font_40_ = nullptr;
    const lv_font_t *font_48_ = nullptr;

    DisplayFonts fonts_;
    ThemeColors current_theme_;

    void GuidePageUI();
    void SetupUI();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    EpdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch, int width, int height,
                       int offset_x, int offset_y, bool mirror_x, bool mirror_y,  bool swap_xy, DisplayFonts fonts);
    EpdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
                       int offset_x, int offset_y, bool mirror_x, bool mirror_y,  bool swap_xy, DisplayFonts fonts);
    ~EpdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetIcon(const char* icon) override;
    virtual void SetPreviewImage(const lv_img_dsc_t* img_dsc) override;
    virtual void UpdateLiveTime(float vol, int cur, float time_s) override;
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    virtual void SetChatMessage(const char* role, const char* content) override; 
#endif  
    
    // Add theme switching function
    virtual void SetTheme(const std::string& theme_name) override;
    virtual void SetContentVisable(bool en) override;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    void SetScrMainGestureEvent(bool enable);
};

#endif // LCD_DISPLAY_H
