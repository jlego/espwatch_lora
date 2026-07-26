#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
#include "target.h"
#include "lcd.h"
#include <lvgl.h>

StdRNG fast_rng;
SimpleMeshTables tables;

static unsigned long next_refresh = 0;
static volatile bool _new_message = false;

// 屏幕休眠状态
static unsigned long _last_activity = 0;
static bool _screen_off = false;
static bool _just_woken = false;
static volatile bool _g0_pressed = false;
static const uint16_t _timeout_options[] = {0, 10, 30, 60, 120, 300};
static const int _timeout_options_count = sizeof(_timeout_options) / sizeof(_timeout_options[0]);
static const char* _timeout_labels[] = {"Never", "10s", "30s", "1m", "2m", "5m"};
static int _timeout_selected_idx = 1;
static bool _in_timeout_select = false;

// 时间设置状态
static bool _in_time_set = false;
static int _time_edit_field = 0;
static bool _time_editing = false;
static int _time_edit_hour = 0;
static int _time_edit_minute = 0;

static unsigned long get_screen_timeout_ms() {
    uint16_t secs = _timeout_options[_timeout_selected_idx];
    if (secs == 0) return 0;
    return (unsigned long)secs * 1000UL;
}

#if defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef BLE_PIN_CODE
  #include <helpers/esp32/SerialBLEInterface.h>
  SerialBLEInterface serial_interface;
#else
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#endif

class SimpleUITask : public AbstractUITask {
public:
    SimpleUITask() : AbstractUITask(nullptr, nullptr) {}
    
    void msgRead(int msgcount) override {}
    void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override {
        _new_message = true;
    }
    void onDiscoveredContact(ContactInfo& ci, bool is_new, uint8_t path_len, const uint8_t* path) {
        _new_message = true;
    }
    void notify(UIEventType t = UIEventType::none) override {}
    void loop() override {}
};

static SimpleUITask ui_task;

MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
#ifdef DISPLAY_CLASS
   , &ui_task
#endif
);

void halt() {
  while (1) {
    delay(1000);
    Serial.println("System halted");
  }
}

// LVGL 对象指针
static lv_obj_t *scr_main = nullptr;
static lv_obj_t *tabview = nullptr;
static lv_obj_t *tab_home = nullptr;
static lv_obj_t *tab_contacts = nullptr;
static lv_obj_t *tab_channels = nullptr;
static lv_obj_t *tab_settings = nullptr;

// Home 页对象
static lv_obj_t *lbl_time = nullptr;
static lv_obj_t *lbl_date = nullptr;
static lv_obj_t *lbl_batt = nullptr;

// Contacts 页对象
static lv_obj_t *lst_contacts = nullptr;
static lv_obj_t *lbl_contact_count = nullptr;

// Channels 页对象
static lv_obj_t *lst_channels = nullptr;

// Settings 页对象
static lv_obj_t *lst_settings = nullptr;
static lv_obj_t *lbl_settings_detail = nullptr;

// 状态变量
enum class MenuScreen {
    HOME, CONTACTS, CHANNELS, CHAT, SETTINGS
};

enum class SettingsCategory {
    MAIN_MENU, PUBLIC_INFO, RADIO_SETUP, THEME, OTHER, DEVICE_INFO
};

static MenuScreen _menu_state = MenuScreen::HOME;
static MenuScreen _chat_parent = MenuScreen::CONTACTS;
static SettingsCategory _settings_category = SettingsCategory::MAIN_MENU;
static int _settings_menu_idx = 0;
static int _contacts_selected = 0;
static int _channels_selected = 0;
static bool _settings_selected = false;

// LVGL 显示刷新回调
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    int32_t w = (area->x2 - area->x1 + 1);
    int32_t h = (area->y2 - area->y1 + 1);
    
    LCD_SetWindows(area->x1, area->y1, area->x2, area->y2);
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Lcd_WriteData_16Bit(color_p->full);
            color_p++;
        }
    }
    
    lv_disp_flush_ready(disp);
}

// 自定义按键编码
#define MY_KEY_ENTER  1
#define MY_KEY_NEXT   2

// LVGL 输入设备回调
void my_input_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    static bool last_state = false;
    static uint32_t last_key = 0;
    
    if (digitalRead(PIN_USER_BTN) == LOW) {
        data->state = LV_INDEV_STATE_PR;
        data->key = MY_KEY_ENTER;
        last_key = MY_KEY_ENTER;
        last_state = true;
    } else if (digitalRead(PIN_BTN_2) == LOW) {
        data->state = LV_INDEV_STATE_PR;
        data->key = MY_KEY_NEXT;
        last_key = MY_KEY_NEXT;
        last_state = true;
    } else {
        data->state = LV_INDEV_STATE_REL;
        data->key = last_key;
        last_state = false;
    }
}

// LVGL 缓冲区
#define LVGL_BUF_SIZE (240 * 20)
static lv_color_t lvgl_buf1[LVGL_BUF_SIZE];
static lv_color_t lvgl_buf2[LVGL_BUF_SIZE];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

void init_lvgl() {
    lv_init();
    
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, lvgl_buf2, LVGL_BUF_SIZE);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 285;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = my_input_read;
    lv_indev_drv_register(&indev_drv);
}

// 更新时间显示
void update_time_display(lv_timer_t *timer) {
    uint32_t now_secs = rtc_clock.getCurrentTime();
    int h = (now_secs / 3600) % 24;
    int m = (now_secs % 3600) / 60;
    
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, m);
    lv_label_set_text(lbl_time, time_buf);
    
    // 计算日期
    int days = now_secs / 86400;
    int y = 1970, mon = 1, d = 1;
    int remaining_days = days;
    while (1) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        int ydays = leap ? 366 : 365;
        if (remaining_days < ydays) break;
        remaining_days -= ydays;
        y++;
    }
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int feb = leap ? 29 : 28;
    for (mon = 0; mon < 12; mon++) {
        int md = (mon == 1) ? feb : mdays[mon];
        if (remaining_days < md) break;
        remaining_days -= md;
    }
    mon++;
    d = remaining_days + 1;
    
    char date_buf[16];
    snprintf(date_buf, sizeof(date_buf), "%04d-%d-%d", y, mon, d);
    lv_label_set_text(lbl_date, date_buf);
    
    // 更新电量
    uint8_t batt = board.getBattPercent();
    char batt_buf[16];
    snprintf(batt_buf, sizeof(batt_buf), "Battery: %d%%", batt);
    lv_label_set_text(lbl_batt, batt_buf);
}

// 前向声明按钮回调函数
void contact_btn_event_cb(lv_event_t *e);
void channel_btn_event_cb(lv_event_t *e);
void settings_btn_event_cb(lv_event_t *e);

// 更新联系人列表
void update_contacts_list() {
    lv_obj_clean(lst_contacts);
    
    int num_contacts = the_mesh.getNumContacts();
    
    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%d nodes", num_contacts);
    lv_label_set_text(lbl_contact_count, count_buf);
    
    for (int i = 0; i < num_contacts; i++) {
        ContactInfo contact;
        if (the_mesh.getContactByIdx(i, contact)) {
            lv_obj_t *btn = lv_btn_create(lst_contacts);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_style_bg_color(btn, (i == _contacts_selected) ? lv_color_hex(0x0096d8) : lv_color_hex(0x333333), 0);
            
            lv_obj_t *label = lv_label_create(btn);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%dhops)", contact.name, contact.out_path_len);
            lv_label_set_text(label, buf);
            lv_obj_center(label);
            
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, contact_btn_event_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(btn, contact_btn_event_cb, LV_EVENT_KEY, NULL);
        }
    }
}

// 更新频道列表
void update_channels_list() {
    lv_obj_clean(lst_channels);
    
    const char* channels[] = {"Broadcast", "Contacts", "Direct"};
    int num_channels = 3;
    
    for (int i = 0; i < num_channels; i++) {
        lv_obj_t *btn = lv_btn_create(lst_channels);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_bg_color(btn, (i == _channels_selected) ? lv_color_hex(0x0096d8) : lv_color_hex(0x333333), 0);
        
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, channels[i]);
        lv_obj_center(label);
        
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, channel_btn_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(btn, channel_btn_event_cb, LV_EVENT_KEY, NULL);
    }
}

// 更新设置列表
void update_settings_list() {
    lv_obj_clean(lst_settings);
    
    if (!_settings_selected) {
        const char* categories[] = {"Public Info", "Radio Setup", "Theme", "Other", "Device Info"};
        int num_cats = 5;
        
        for (int i = 0; i < num_cats; i++) {
            lv_obj_t *btn = lv_btn_create(lst_settings);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_style_bg_color(btn, (i == _settings_menu_idx) ? lv_color_hex(0x0096d8) : lv_color_hex(0x333333), 0);
            
            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, categories[i]);
            lv_obj_center(label);
            
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(btn, settings_btn_event_cb, LV_EVENT_KEY, NULL);
        }
    } else {
        switch (_settings_category) {
            case SettingsCategory::PUBLIC_INFO: {
                lv_obj_t *label = lv_label_create(lst_settings);
                char buf[64];
                snprintf(buf, sizeof(buf), "Name: %s\nBLE PIN: %06lu", 
                         the_mesh.getNodeName(), (unsigned long)the_mesh.getBLEPin());
                lv_label_set_text(label, buf);
                break;
            }
            case SettingsCategory::RADIO_SETUP: {
                lv_obj_t *label = lv_label_create(lst_settings);
                char buf[64];
                snprintf(buf, sizeof(buf), "Frequency: %.1f MHz\nSF: %d\nBW: %.1f kHz\nTX Power: %d dBm",
                         LORA_FREQ, LORA_SF, LORA_BW, LORA_TX_POWER);
                lv_label_set_text(label, buf);
                break;
            }
            case SettingsCategory::THEME: {
                lv_obj_t *label = lv_label_create(lst_settings);
                char buf[64];
                snprintf(buf, sizeof(buf), "Screen Timeout: %s", _timeout_labels[_timeout_selected_idx]);
                lv_label_set_text(label, buf);
                break;
            }
            case SettingsCategory::OTHER: {
                lv_obj_t *label = lv_label_create(lst_settings);
                uint32_t now_secs = rtc_clock.getCurrentTime();
                int h = (now_secs / 3600) % 24;
                int m = (now_secs % 3600) / 60;
                char buf[64];
                snprintf(buf, sizeof(buf), "Time: %02d:%02d\nFactory Reset: Hold to reset", h, m);
                lv_label_set_text(label, buf);
                break;
            }
            case SettingsCategory::DEVICE_INFO: {
                lv_obj_t *label = lv_label_create(lst_settings);
                char buf[64];
                snprintf(buf, sizeof(buf), "Node: %s\nRadio: SX1268 %.1f MHz\nUptime: %ldmin",
                         the_mesh.getNodeName(), LORA_FREQ, rtc_clock.getCurrentTime() / 60);
                lv_label_set_text(label, buf);
                break;
            }
            default:
                break;
        }
    }
}

// Chat 页面管理
static bool _chat_visible = false;
static lv_obj_t *chat_overlay = nullptr;
static lv_obj_t *lbl_chat_title_overlay = nullptr;
static lv_obj_t *lst_chat_overlay = nullptr;

void show_chat_overlay(const char* title) {
    if (chat_overlay == nullptr) {
        chat_overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(chat_overlay, lv_pct(100), lv_pct(100));
        lv_obj_set_style_border_width(chat_overlay, 0, 0);
        lv_obj_set_style_outline_width(chat_overlay, 0, 0);
        lv_obj_set_style_pad_all(chat_overlay, 0, 0);
        lv_obj_set_style_bg_color(chat_overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_flex_flow(chat_overlay, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(chat_overlay, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_width(chat_overlay, 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(chat_overlay, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
        
        lbl_chat_title_overlay = lv_label_create(chat_overlay);
        lv_obj_set_style_text_font(lbl_chat_title_overlay, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl_chat_title_overlay, lv_color_hex(0xFFFFFF), 0);
        
        lst_chat_overlay = lv_list_create(chat_overlay);
        lv_obj_set_size(lst_chat_overlay, lv_pct(100), lv_pct(90));
        lv_obj_set_scrollbar_mode(lst_chat_overlay, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_width(lst_chat_overlay, 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(lst_chat_overlay, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    }
    
    lv_label_set_text(lbl_chat_title_overlay, title);
    lv_obj_clean(lst_chat_overlay);
    
    lv_obj_clear_flag(chat_overlay, LV_OBJ_FLAG_HIDDEN);
    _chat_visible = true;
}

void hide_chat_overlay() {
    if (chat_overlay != nullptr) {
        lv_obj_add_flag(chat_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    _chat_visible = false;
}

// 按钮事件回调 - 处理CLICKED和KEY事件
// KEY事件中：MY_KEY_ENTER触发点击，MY_KEY_NEXT切换选中项
void contact_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (code == LV_EVENT_CLICKED) {
        _contacts_selected = idx;
        update_contacts_list();
        _chat_parent = MenuScreen::CONTACTS;
        _menu_state = MenuScreen::CHAT;
        show_chat_overlay("Contact Chat");
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == MY_KEY_ENTER) {
            _contacts_selected = idx;
            update_contacts_list();
            _chat_parent = MenuScreen::CONTACTS;
            _menu_state = MenuScreen::CHAT;
            show_chat_overlay("Contact Chat");
        } else if (key == MY_KEY_NEXT) {
            _contacts_selected = idx;
            update_contacts_list();
        }
    }
}

void channel_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (code == LV_EVENT_CLICKED) {
        _channels_selected = idx;
        update_channels_list();
        _chat_parent = MenuScreen::CHANNELS;
        _menu_state = MenuScreen::CHAT;
        show_chat_overlay("Channel Chat");
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == MY_KEY_ENTER) {
            _channels_selected = idx;
            update_channels_list();
            _chat_parent = MenuScreen::CHANNELS;
            _menu_state = MenuScreen::CHAT;
            show_chat_overlay("Channel Chat");
        } else if (key == MY_KEY_NEXT) {
            _channels_selected = idx;
            update_channels_list();
        }
    }
}

void settings_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (code == LV_EVENT_CLICKED) {
        if (!_settings_selected) {
            _settings_menu_idx = idx;
            update_settings_list();
            _settings_selected = true;
            switch (idx) {
                case 0: _settings_category = SettingsCategory::PUBLIC_INFO; break;
                case 1: _settings_category = SettingsCategory::RADIO_SETUP; break;
                case 2: _settings_category = SettingsCategory::THEME; break;
                case 3: _settings_category = SettingsCategory::OTHER; break;
                case 4: _settings_category = SettingsCategory::DEVICE_INFO; break;
            }
            update_settings_list();
        } else {
            _settings_selected = false;
            _settings_menu_idx = 0;
            update_settings_list();
        }
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == MY_KEY_ENTER) {
            if (!_settings_selected) {
                _settings_menu_idx = idx;
                update_settings_list();
                _settings_selected = true;
                switch (idx) {
                    case 0: _settings_category = SettingsCategory::PUBLIC_INFO; break;
                    case 1: _settings_category = SettingsCategory::RADIO_SETUP; break;
                    case 2: _settings_category = SettingsCategory::THEME; break;
                    case 3: _settings_category = SettingsCategory::OTHER; break;
                    case 4: _settings_category = SettingsCategory::DEVICE_INFO; break;
                }
                update_settings_list();
            } else {
                _settings_selected = false;
                _settings_menu_idx = 0;
                update_settings_list();
            }
        } else if (key == MY_KEY_NEXT) {
            _settings_menu_idx = idx;
            update_settings_list();
        }
    }
}

// 创建UI
void create_ui() {
    // 设置黑底白字全局样式
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
    
    // 创建Tab视图（隐藏底部导航栏）
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 0);
    
    // 去掉tabview边框
    lv_obj_set_style_border_width(tabview, 0, 0);
    lv_obj_set_style_outline_width(tabview, 0, 0);
    lv_obj_set_style_pad_all(tabview, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tabview, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(tabview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tabview, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    // 创建标签页（只保留4个一级页面）
    tab_home = lv_tabview_add_tab(tabview, "Home");
    tab_contacts = lv_tabview_add_tab(tabview, "Contacts");
    tab_channels = lv_tabview_add_tab(tabview, "Channels");
    tab_settings = lv_tabview_add_tab(tabview, "Settings");
    
    // 隐藏tabview内容区域的滚动条
    lv_obj_t *tv_content = lv_tabview_get_content(tabview);
    lv_obj_set_scrollbar_mode(tv_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tv_content, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tv_content, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    // 去掉所有tab页面的边框，设置黑底白字
    lv_obj_set_style_border_width(tab_home, 0, 0);
    lv_obj_set_style_outline_width(tab_home, 0, 0);
    lv_obj_set_style_pad_all(tab_home, 0, 0);
    lv_obj_set_style_bg_color(tab_home, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tab_home, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(tab_home, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tab_home, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tab_home, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    lv_obj_set_style_border_width(tab_contacts, 0, 0);
    lv_obj_set_style_outline_width(tab_contacts, 0, 0);
    lv_obj_set_style_pad_all(tab_contacts, 0, 0);
    lv_obj_set_style_bg_color(tab_contacts, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tab_contacts, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(tab_contacts, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tab_contacts, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tab_contacts, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    lv_obj_set_style_border_width(tab_channels, 0, 0);
    lv_obj_set_style_outline_width(tab_channels, 0, 0);
    lv_obj_set_style_pad_all(tab_channels, 0, 0);
    lv_obj_set_style_bg_color(tab_channels, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tab_channels, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(tab_channels, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tab_channels, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tab_channels, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    lv_obj_set_style_border_width(tab_settings, 0, 0);
    lv_obj_set_style_outline_width(tab_settings, 0, 0);
    lv_obj_set_style_pad_all(tab_settings, 0, 0);
    lv_obj_set_style_bg_color(tab_settings, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tab_settings, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(tab_settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(tab_settings, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(tab_settings, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    
    // Home 页
    lv_obj_set_flex_flow(tab_home, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_home, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lbl_time = lv_label_create(tab_home);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl_time, "00:00");
    
    lbl_date = lv_label_create(tab_home);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
    lv_label_set_text(lbl_date, "2024-01-01");
    
    lbl_batt = lv_label_create(tab_home);
    lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_batt, "Battery: 100%");
    
    // Contacts 页
    lv_obj_set_flex_flow(tab_contacts, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_contacts = lv_label_create(tab_contacts);
    lv_label_set_text(hdr_contacts, "Contacts");
    lv_obj_set_style_text_font(hdr_contacts, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_contacts, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_contacts, lv_color_hex(0x00B050), 0);
    lv_obj_set_style_bg_opa(hdr_contacts, LV_OPA_COVER, 0);
    lv_obj_set_width(hdr_contacts, lv_pct(100));
    lv_obj_set_style_pad_top(hdr_contacts, 5, 0);
    lv_obj_set_style_pad_bottom(hdr_contacts, 5, 0);
    lv_obj_set_style_text_align(hdr_contacts, LV_TEXT_ALIGN_CENTER, 0);
    
    lbl_contact_count = lv_label_create(tab_contacts);
    lv_label_set_text(lbl_contact_count, "0 nodes");
    
    lst_contacts = lv_list_create(tab_contacts);
    lv_obj_set_size(lst_contacts, lv_pct(100), lv_pct(80));
    lv_obj_set_style_border_width(lst_contacts, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(lst_contacts, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_outline_width(lst_contacts, 0, 0);
    lv_obj_set_style_bg_color(lst_contacts, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(lst_contacts, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(lst_contacts, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(lst_contacts, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(lst_contacts, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_row(lst_contacts, 5, 0);
    
    // Channels 页
    lv_obj_set_flex_flow(tab_channels, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_channels = lv_label_create(tab_channels);
    lv_label_set_text(hdr_channels, "Channels");
    lv_obj_set_style_text_font(hdr_channels, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_channels, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_channels, lv_color_hex(0x00B050), 0);
    lv_obj_set_style_bg_opa(hdr_channels, LV_OPA_COVER, 0);
    lv_obj_set_width(hdr_channels, lv_pct(100));
    lv_obj_set_style_pad_top(hdr_channels, 5, 0);
    lv_obj_set_style_pad_bottom(hdr_channels, 5, 0);
    lv_obj_set_style_text_align(hdr_channels, LV_TEXT_ALIGN_CENTER, 0);
    
    lst_channels = lv_list_create(tab_channels);
    lv_obj_set_size(lst_channels, lv_pct(100), lv_pct(90));
    lv_obj_set_style_border_width(lst_channels, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(lst_channels, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_outline_width(lst_channels, 0, 0);
    lv_obj_set_style_bg_color(lst_channels, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(lst_channels, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(lst_channels, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(lst_channels, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(lst_channels, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_row(lst_channels, 5, 0);
    
    // Settings 页
    lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_settings = lv_label_create(tab_settings);
    lv_label_set_text(hdr_settings, "Settings");
    lv_obj_set_style_text_font(hdr_settings, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_settings, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_settings, lv_color_hex(0x00B050), 0);
    lv_obj_set_style_bg_opa(hdr_settings, LV_OPA_COVER, 0);
    lv_obj_set_width(hdr_settings, lv_pct(100));
    lv_obj_set_style_pad_top(hdr_settings, 5, 0);
    lv_obj_set_style_pad_bottom(hdr_settings, 5, 0);
    lv_obj_set_style_text_align(hdr_settings, LV_TEXT_ALIGN_CENTER, 0);
    
    lst_settings = lv_list_create(tab_settings);
    lv_obj_set_size(lst_settings, lv_pct(100), lv_pct(90));
    lv_obj_set_style_border_width(lst_settings, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(lst_settings, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_outline_width(lst_settings, 0, 0);
    lv_obj_set_style_bg_color(lst_settings, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(lst_settings, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(lst_settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(lst_settings, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(lst_settings, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_row(lst_settings, 5, 0);
    
    // 更新列表
    update_contacts_list();
    update_channels_list();
    update_settings_list();
    
    // 创建定时器更新时间
    lv_timer_create(update_time_display, 1000, NULL);
}

void draw_startup_screen() {
#ifdef DISPLAY_CLASS
  if (!display.begin()) return;
  
  init_lvgl();
  
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
  
  lv_obj_t *lbl1 = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(lbl1, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl1, lv_color_hex(0x00FF00), 0);
  lv_label_set_text(lbl1, "MeshCore");
  lv_obj_align(lbl1, LV_ALIGN_CENTER, 0, -40);
  
  lv_obj_t *lbl2 = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl2, lv_color_hex(0xFFFF00), 0);
  lv_label_set_text(lbl2, "ESPWatch LoRa");
  lv_obj_align(lbl2, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_t *lbl3 = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(lbl3, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl3, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(lbl3, "Initializing...");
  lv_obj_align(lbl3, LV_ALIGN_CENTER, 0, 40);
  
  lv_refr_now(NULL);
#endif
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef CUSTOM_BOARD
  draw_startup_screen();
  user_btn.begin();
  user_btn2.begin();
  _last_activity = millis();

  uint16_t batt_mv = board.getBattMilliVolts();
  uint8_t batt_pct = board.getBattPercent();
  uint8_t cw2015_soc = board.getCW2015SoC();
  Serial.printf("[BATT] CW2015: Voltage=%dmV, Estimated=%d%%, CW2015_SoC=%d%%\n",
                batt_mv, batt_pct, cw2015_soc);
#endif

  if (!radio_init()) {
    Serial.println("radio init failed");
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  rtc_clock.begin(Wire);

#if defined(ESP32)
  SPIFFS.begin(true);
#endif
  store.begin();

  the_mesh.begin(
#ifdef DISPLAY_CLASS
      true
#else
      false
#endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(the_mesh.getNodeName(), the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);

  NodePrefs* prefs = the_mesh.getNodePrefs();
  uint16_t saved_timeout = prefs->screen_timeout_seconds;
  for (int i = 0; i < _timeout_options_count; i++) {
    if (_timeout_options[i] == saved_timeout) {
      _timeout_selected_idx = i;
      break;
    }
  }

  // 创建主UI
  create_ui();

  Serial.println("Boot complete");
}

void loop() {
#ifdef CUSTOM_BOARD
  if (_screen_off) {
    the_mesh.loop();

    pinMode(0, INPUT_PULLUP);
    gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(100000ULL);
    esp_light_sleep_start();
    
    if (digitalRead(0) == LOW) {
      _screen_off = false;
      _just_woken = true;
      _last_activity = millis();
      Serial.println("[UI] Screen wake by G0");
      display.turnOn();
    }
    return;
  }
#endif

  the_mesh.loop();
  lv_timer_handler();

  if (_new_message) {
    _new_message = false;
    next_refresh = 0;
    update_contacts_list();
  }

#ifdef BLE_PIN_CODE
  if (serial_interface.checkAndRequestTimeSync()) {
    Serial.println("[BLE] Time sync request sent");
  }
#endif

#ifdef CUSTOM_BOARD
  unsigned long now = millis();

  if (now - _last_activity >= get_screen_timeout_ms() && get_screen_timeout_ms() > 0 && !_screen_off) {
    display.turnOff();
    _screen_off = true;
    Serial.println("[UI] Screen timeout -> off");
    return;
  }

  int btn_g0 = user_btn.check();
  int btn_g45 = user_btn2.check();

  if (_screen_off && (btn_g0 != BUTTON_EVENT_NONE || btn_g45 != BUTTON_EVENT_NONE)) {
    display.turnOn();
    _screen_off = false;
    _last_activity = now;
    _just_woken = true;
    Serial.println("[UI] Screen wake up");
    next_refresh = now + 60000;
    return;
  }

  if (btn_g0 != BUTTON_EVENT_NONE || btn_g45 != BUTTON_EVENT_NONE) {
    _last_activity = now;
    if(_just_woken) {
      _just_woken = false;
    }
  }

  // G45 按钮处理
  if (btn_g45 == BUTTON_EVENT_CLICK) {
    board.beep(150, 1500);
    Serial.println("[UI] G45: Next");

    if (_in_timeout_select) {
        _timeout_selected_idx = (_timeout_selected_idx + 1) % _timeout_options_count;
        update_settings_list();
        return;
    }

    if (_in_time_set) {
        if (!_time_editing) {
            _time_edit_field = (_time_edit_field + 1) % 2;
        } else {
            if (_time_edit_field == 0) {
                _time_edit_hour = (_time_edit_hour + 23) % 24;
            } else {
                _time_edit_minute = (_time_edit_minute + 59) % 60;
            }
        }
        return;
    }

    // 如果Chat页面可见，隐藏它
    if (_chat_visible) {
        hide_chat_overlay();
        return;
    }

    // G45短按始终切换到下一个tab
    uint16_t current_tab = lv_tabview_get_tab_act(tabview);
    if (current_tab < 3) {
        lv_tabview_set_act(tabview, current_tab + 1, LV_ANIM_OFF);
    } else {
        lv_tabview_set_act(tabview, 0, LV_ANIM_OFF);
    }
    
    next_refresh = now + 60000;
  }

  // G0 长按 - 确认/进入列表项
  if (btn_g0 == BUTTON_EVENT_LONG_PRESS) {
    board.beep(200, 1000);
    user_btn.cancelClick();

    if (_in_time_set) {
        uint32_t now_secs = rtc_clock.getCurrentTime();
        int cur_h = (now_secs / 3600) % 24;
        int cur_m = (now_secs % 3600) / 60;
        int cur_s = now_secs % 60;
        int days = now_secs / 86400;
        uint32_t new_secs = (uint32_t)days * 86400 + (uint32_t)_time_edit_hour * 3600 + (uint32_t)_time_edit_minute * 60 + cur_s;
        rtc_clock.setCurrentTime(new_secs);
        _in_time_set = false;
        _time_editing = false;
        Serial.printf("[UI] G0 long: Time set to %02d:%02d (epoch=%lu)\n", _time_edit_hour, _time_edit_minute, new_secs);
        return;
    }

    if (_in_timeout_select) {
        _in_timeout_select = false;
        NodePrefs* prefs = the_mesh.getNodePrefs();
        prefs->screen_timeout_seconds = _timeout_options[_timeout_selected_idx];
        the_mesh.savePrefs();
        Serial.printf("[UI] G0 long: Timeout set to %s (%ds)\n", _timeout_labels[_timeout_selected_idx], _timeout_options[_timeout_selected_idx]);
        update_settings_list();
        return;
    }

    // 如果Chat页面可见，隐藏它
    if (_chat_visible) {
        hide_chat_overlay();
        return;
    }

    // G0长按：如果已在子菜单中则返回上一级，否则进入列表项
    uint16_t current_tab = lv_tabview_get_tab_act(tabview);
    if (current_tab == 3) { // Settings tab
        if (_settings_selected) { // 已在子菜单，返回主菜单
            _settings_selected = false;
            update_settings_list();
            return;
        }
        // 主菜单，进入子菜单
        _settings_selected = true;
        update_settings_list();
    } else if (current_tab == 1) { // Contacts tab - 进入联系人Chat
        int num_contacts = the_mesh.getNumContacts();
        if (num_contacts > 0 && _contacts_selected < num_contacts) {
            _chat_parent = MenuScreen::CONTACTS;
            show_chat_overlay("Contact Chat");
        }
    } else if (current_tab == 2) { // Channels tab - 进入频道Chat
        _chat_parent = MenuScreen::CHANNELS;
        show_chat_overlay("Channel Chat");
    } else if (current_tab == 0) { // Home tab - 跳转Settings
        lv_tabview_set_act(tabview, 3, LV_ANIM_OFF);
    }

    next_refresh = now + 60000;
  }

  // G0 短按 - 切换列表选中项
  if (btn_g0 == BUTTON_EVENT_CLICK) {
    board.beep(100, 2000);

    if (_in_time_set) {
        if (!_time_editing) {
            _time_editing = true;
            uint32_t now_secs = rtc_clock.getCurrentTime();
            _time_edit_hour = (now_secs / 3600) % 24;
            _time_edit_minute = (now_secs % 3600) / 60;
        } else {
            if (_time_edit_field == 0) {
                _time_edit_hour = (_time_edit_hour + 1) % 24;
            } else {
                _time_edit_minute = (_time_edit_minute + 1) % 60;
            }
        }
        return;
    }

    if (_in_timeout_select) {
        _timeout_selected_idx = (_timeout_selected_idx + 1) % _timeout_options_count;
        update_settings_list();
        return;
    }

    uint16_t current_tab = lv_tabview_get_tab_act(tabview);
    
    if (current_tab == 3) { // Settings tab - 切换设置选中项
        if (!_settings_selected) {
            _settings_menu_idx = (_settings_menu_idx + 1) % 5;
            update_settings_list();
        }
    } else if (current_tab == 0) { // Home tab - 无操作或跳转
    } else if (current_tab == 1) { // Contacts tab - 切换联系人选中项
        int num_contacts = the_mesh.getNumContacts();
        if (num_contacts > 0) {
            _contacts_selected = (_contacts_selected + 1) % num_contacts;
            update_contacts_list();
        }
    } else if (current_tab == 2) { // Channels tab - 切换频道选中项
        _channels_selected = (_channels_selected + 1) % 3;
        update_channels_list();
    }
    
    next_refresh = now + 60000;
  }

  // 定期刷新
  if (millis() > next_refresh && next_refresh != 0) {
    update_contacts_list();
    update_channels_list();
    update_settings_list();
    next_refresh = millis() + 60000;
  }
#endif
}