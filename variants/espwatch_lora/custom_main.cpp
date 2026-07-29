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

// Chat 前向声明
static bool _chat_visible = false;
static void add_chat_message(const char* from_name, const char* text, bool is_outgoing, bool is_channel, const char* target);
static void update_chat_list();

class SimpleUITask : public AbstractUITask {
public:
    SimpleUITask() : AbstractUITask(nullptr, nullptr) {}
    
    void msgRead(int msgcount) override {}
    void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override {
        _new_message = true;
        // Store in chat history - received message
        // Determine if it's a channel or direct message based on path_len
        bool is_channel = (path_len > 0);
        const char* target = is_channel ? "Broadcast" : from_name;
        add_chat_message(from_name, text, false, is_channel, target);
        
        // If chat overlay is visible for this conversation, refresh it
        if (_chat_visible) {
            update_chat_list();
        }
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

// Chat 页面管理
static lv_obj_t *chat_overlay = nullptr;
static lv_obj_t *lbl_chat_title_overlay = nullptr;
static lv_obj_t *lst_chat_overlay = nullptr;

// Chat message history
#define MAX_CHAT_MESSAGES 50
struct ChatMessage {
    char text[128];
    char from_name[32];
    char contact_or_channel[32];
    bool is_outgoing;
    bool is_channel;
};
static ChatMessage _chat_history[MAX_CHAT_MESSAGES];
static int _chat_history_count = 0;

static void add_chat_message(const char* from_name, const char* text, bool is_outgoing, bool is_channel, const char* target) {
    if (_chat_history_count >= MAX_CHAT_MESSAGES) {
        // Shift all messages up to make room
        for (int i = 0; i < MAX_CHAT_MESSAGES - 1; i++) {
            _chat_history[i] = _chat_history[i + 1];
        }
        _chat_history_count = MAX_CHAT_MESSAGES - 1;
    }
    ChatMessage& msg = _chat_history[_chat_history_count];
    strncpy(msg.from_name, from_name, sizeof(msg.from_name) - 1);
    msg.from_name[sizeof(msg.from_name) - 1] = '\0';
    strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    strncpy(msg.contact_or_channel, target, sizeof(msg.contact_or_channel) - 1);
    msg.contact_or_channel[sizeof(msg.contact_or_channel) - 1] = '\0';
    msg.is_outgoing = is_outgoing;
    msg.is_channel = is_channel;
    _chat_history_count++;
}

static void update_chat_list() {
    if (lst_chat_overlay == nullptr) return;
    lv_obj_clean(lst_chat_overlay);
    
    // Get current contact/channel name for filtering
    const char* current_name = nullptr;
    bool is_channel = false;
    if (_chat_parent == MenuScreen::CHANNELS) {
        // Get channel name
        static char ch_name[32];
        const char* channels[] = {"Broadcast", "Contacts", "Direct"};
        if (_channels_selected < 3) {
            strncpy(ch_name, channels[_channels_selected], sizeof(ch_name) - 1);
        } else {
            snprintf(ch_name, sizeof(ch_name), "Ch%d", _channels_selected);
        }
        ch_name[sizeof(ch_name) - 1] = '\0';
        current_name = ch_name;
        is_channel = true;
    } else {
        ContactInfo c;
        static char ct_name[32];
        if (the_mesh.getContactByIdx(_contacts_selected, c)) {
            strncpy(ct_name, c.name, sizeof(ct_name) - 1);
        } else {
            strncpy(ct_name, "Unknown", sizeof(ct_name) - 1);
        }
        ct_name[sizeof(ct_name) - 1] = '\0';
        current_name = ct_name;
        is_channel = false;
    }
    
    // Collect matching messages (newest first)
    int filtered_indices[MAX_CHAT_MESSAGES];
    int filtered_count = 0;
    for (int i = _chat_history_count - 1; i >= 0; i--) {
        ChatMessage& msg = _chat_history[i];
        if (msg.is_channel == is_channel && strcmp(msg.contact_or_channel, current_name) == 0) {
            filtered_indices[filtered_count++] = i;
        }
    }
    
    if (filtered_count == 0) {
        lv_obj_t* hint = lv_label_create(lst_chat_overlay);
        lv_label_set_text(hint, "No messages yet");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_set_width(hint, lv_pct(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(hint, 20, 0);
        return;
    }
    
    // Show up to 10 messages
    int show_count = filtered_count;
    if (show_count > 10) show_count = 10;
    
    for (int i = 0; i < show_count; i++) {
        ChatMessage& msg = _chat_history[filtered_indices[i]];
        
        // Container for each message
        lv_obj_t* msg_container = lv_obj_create(lst_chat_overlay);
        lv_obj_set_width(msg_container, lv_pct(90));
        lv_obj_set_height(msg_container, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(msg_container, 0, 0);
        lv_obj_set_style_pad_all(msg_container, 6, 0);
        lv_obj_set_style_radius(msg_container, 8, 0);
        
        if (msg.is_outgoing) {
            // Sent message - green background, right aligned
            lv_obj_set_style_bg_color(msg_container, lv_color_hex(0x00B050), 0);
            lv_obj_set_style_bg_opa(msg_container, LV_OPA_COVER, 0);
            lv_obj_set_align(msg_container, LV_ALIGN_RIGHT_MID);
            lv_obj_set_style_pad_bottom(msg_container, 4, 0);
        } else {
            // Received message - blue background, left aligned
            lv_obj_set_style_bg_color(msg_container, lv_color_hex(0x0096d8), 0);
            lv_obj_set_style_bg_opa(msg_container, LV_OPA_COVER, 0);
            lv_obj_set_align(msg_container, LV_ALIGN_LEFT_MID);
            lv_obj_set_style_pad_bottom(msg_container, 4, 0);
        }
        
        // Sender name (for received messages)
        if (!msg.is_outgoing) {
            lv_obj_t* name_label = lv_label_create(msg_container);
            lv_label_set_text(name_label, msg.from_name);
            lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(name_label, lv_color_hex(0xAADDFF), 0);
        }
        
        // Message text
        lv_obj_t* text_label = lv_label_create(msg_container);
        lv_label_set_text(text_label, msg.text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(text_label, lv_pct(100));
    }
}

// Home 页对象
static lv_obj_t *lbl_time = nullptr;
static lv_obj_t *lbl_date = nullptr;
static lv_obj_t *lbl_batt = nullptr;
static lv_obj_t *lbl_batt_icon = nullptr;
static lv_obj_t *home_top_row = nullptr;

// 点阵时钟对象
static lv_obj_t *digit_containers[5][7][3] = {nullptr}; // 5个数字(含冒号) x 7行 x 3列
static lv_obj_t *clock_container = nullptr; // 时钟容器
static bool time_initialized = false;
static int last_update_minute = -1;
static int current_combination_index = 0;
static bool srand_initialized = false;

// Contacts 页对象
static lv_obj_t *lst_contacts = nullptr;
static lv_obj_t *lbl_contact_count = nullptr;

// Channels 页对象
static lv_obj_t *lst_channels = nullptr;

// Settings 页对象
static lv_obj_t *lst_settings = nullptr;
static lv_obj_t *lbl_settings_detail = nullptr;

// ==================== 点阵时钟实现 ====================
// 点阵定义 (3x7)
static const uint16_t digit_matrix[11][7] = {
    {0x07,0x05,0x05,0x05,0x07,0x00,0x00}, // 0
    {0x02,0x02,0x02,0x02,0x02,0x00,0x00}, // 1
    {0x07,0x01,0x07,0x04,0x07,0x00,0x00}, // 2
    {0x07,0x01,0x07,0x01,0x07,0x00,0x00}, // 3
    {0x05,0x05,0x07,0x01,0x01,0x00,0x00}, // 4
    {0x07,0x04,0x07,0x01,0x07,0x00,0x00}, // 5
    {0x07,0x04,0x07,0x05,0x07,0x00,0x00}, // 6
    {0x07,0x01,0x01,0x01,0x01,0x00,0x00}, // 7
    {0x07,0x05,0x07,0x05,0x07,0x00,0x00}, // 8
    {0x07,0x05,0x07,0x01,0x07,0x00,0x00}, // 9
    {0x00,0x02,0x00,0x02,0x00,0x00,0x00}  // : (冒号)
};

// 预设色彩搭配
static const struct ColorPair {
    lv_color_t top_color;
    lv_color_t bottom_color;
} color_pairs[] = {
    {lv_color_hex(0x80D1C8), lv_color_hex(0x6bbdb4)}, // 蒂芙尼蓝
    {lv_color_hex(0xFFD4AA), lv_color_hex(0xf3c08f)}, // 浅驼色
    {lv_color_hex(0x012DA7), lv_color_hex(0x02278d)}, // 克莱茵蓝
    {lv_color_hex(0xFF7F00), lv_color_hex(0xe47201)}, // 赤橙
    {lv_color_hex(0x7A76C3), lv_color_hex(0x615cb1)}, // 风信子淡蓝
    {lv_color_hex(0xC7B3A2), lv_color_hex(0xb29984)}, // 灰驼色
    {lv_color_hex(0x8153FF), lv_color_hex(0x6b3ee6)}, // 藤紫
    {lv_color_hex(0x93Dc24), lv_color_hex(0x7fc514)},  // 钛啡绿
    {lv_color_hex(0xfd4569), lv_color_hex(0xf4355a)}, // 玫瑰红
    {lv_color_hex(0x57c2c0), lv_color_hex(0x3ca5a3)},  // 石绿
    {lv_color_hex(0x2082ff), lv_color_hex(0x1372ea)}, // 苏露青
    {lv_color_hex(0xffdc64), lv_color_hex(0xf7d251)}  // 佛手黄
};

static const struct {
    int hour_index;
    int minute_index;
} color_combinations[] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}
};

// 创建点阵时钟容器
void create_matrix_clock(lv_obj_t* parent) {
    const int screen_width = 240;
    const int block_size = screen_width / 18; // 每个小方格约13像素
    const int digit_width = 3 * block_size;
    const int digit_spacing = block_size;
    const int wide_spacing = 3 * block_size;
    
    const int total_width = 4 * digit_width + 2 * digit_spacing + wide_spacing;
    const int first_digit_left_edge = (screen_width - total_width) / 2;
    const int second_digit_left_edge = first_digit_left_edge + digit_width + block_size;
    const int colon_left_edge = second_digit_left_edge + digit_width;
    const int third_digit_left_edge = colon_left_edge + wide_spacing;
    const int fourth_digit_left_edge = third_digit_left_edge + digit_width + block_size;
    
    int digit_positions[] = {first_digit_left_edge, second_digit_left_edge, colon_left_edge, third_digit_left_edge, fourth_digit_left_edge};
    
    for(int d=0; d<5; d++){
        for(int row=0; row<7; row++){
            for(int col=0; col<3; col++){
                lv_obj_t* block = lv_obj_create(parent);
                if (block != nullptr) {
                    lv_obj_set_size(block, block_size - 1, block_size - 1);
                    lv_obj_set_pos(block, digit_positions[d] + col*block_size, row*block_size);
                    lv_obj_set_style_border_width(block, 0, 0);
                    lv_obj_set_style_radius(block, 1, 0);
                    lv_obj_set_scrollbar_mode(block, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, LV_PART_MAIN);
                    digit_containers[d][row][col] = block;
                }
            }
        }
    }
}

// 绘制单个数字
void draw_matrix_digit(int d_index, int num, lv_color_t top_color, lv_color_t bottom_color) {
    if (num < 0 || num >= 11) return;
    
    lv_color_t gradient_start = lv_color_lighten(top_color, 128);
    lv_color_t gradient_end = bottom_color;
    
    for(int row=0; row<7; row++){
        for(int col=0; col<3; col++){
            if (digit_containers[d_index][row][col] == nullptr) continue;
            
            if(digit_matrix[num][row] & (1 << (2-col))){
                int max_sum = 8;
                int current_sum = col + row;
                uint8_t gradient_factor = (uint8_t)((current_sum * 255) / max_sum);
                lv_color_t color = lv_color_mix(gradient_end, gradient_start, gradient_factor);
                lv_obj_set_style_bg_color(digit_containers[d_index][row][col], color, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(digit_containers[d_index][row][col], LV_OPA_COVER, LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_color(digit_containers[d_index][row][col], lv_color_black(), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(digit_containers[d_index][row][col], LV_OPA_TRANSP, LV_PART_MAIN);
            }
        }
    }
}

// 更新时间显示
void update_matrix_clock(int hours, int minutes) {
    if (!srand_initialized) {
        srand(millis());
        srand_initialized = true;
    }
    
    const int num_combinations = sizeof(color_combinations) / sizeof(color_combinations[0]);
    current_combination_index = rand() % num_combinations;
    
    int combination_index = current_combination_index;
    int hour_color_index = color_combinations[combination_index].hour_index;
    int minute_color_index = color_combinations[combination_index].minute_index;
    
    lv_color_t hour_top_color = color_pairs[hour_color_index].top_color;
    lv_color_t hour_bottom_color = color_pairs[hour_color_index].bottom_color;
    lv_color_t minute_top_color = color_pairs[minute_color_index].top_color;
    lv_color_t minute_bottom_color = color_pairs[minute_color_index].bottom_color;
    
    draw_matrix_digit(0, hours/10, hour_top_color, hour_bottom_color);
    draw_matrix_digit(1, hours%10, hour_top_color, hour_bottom_color);
    draw_matrix_digit(2, 10, hour_top_color, hour_bottom_color);
    draw_matrix_digit(3, minutes/10, minute_top_color, minute_bottom_color);
    draw_matrix_digit(4, minutes%10, minute_top_color, minute_bottom_color);
}

// 清理点阵时钟
void cleanup_matrix_clock() {
    for(int d=0; d<5; d++){
        for(int row=0; row<7; row++){
            for(int col=0; col<3; col++){
                if (digit_containers[d][row][col] != nullptr) {
                    lv_obj_del(digit_containers[d][row][col]);
                    digit_containers[d][row][col] = nullptr;
                }
            }
        }
    }
}
// ==================== 点阵时钟实现结束 ====================

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
    
    // 更新点阵时钟（每分钟更新一次）
    if (m != last_update_minute || !time_initialized) {
        update_matrix_clock(h, m);
        last_update_minute = m;
        time_initialized = true;
    }
    
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
    snprintf(batt_buf, sizeof(batt_buf), "%d", batt);
    lv_label_set_text(lbl_batt, batt_buf);
    
    // 更新电池图标
    const char* batt_icon;
    if (batt > 80) batt_icon = LV_SYMBOL_BATTERY_FULL;
    else if (batt > 60) batt_icon = LV_SYMBOL_BATTERY_3;
    else if (batt > 40) batt_icon = LV_SYMBOL_BATTERY_2;
    else if (batt > 20) batt_icon = LV_SYMBOL_BATTERY_1;
    else batt_icon = LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text(lbl_batt_icon, batt_icon);
    
    // 电量低于20%时图标变为红色，否则为绿色
    if (batt <= 20) {
        lv_obj_set_style_text_color(lbl_batt_icon, lv_color_hex(0xFF0000), 0);
    } else {
        lv_obj_set_style_text_color(lbl_batt_icon, lv_color_hex(0x00B050), 0);
    }
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

static void make_section_title(lv_obj_t *parent, const char* text) {
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00B050), 0);
    lv_obj_set_style_pad_top(title, 0, 0);
    lv_obj_set_style_pad_bottom(title, 0, 0);
    lv_obj_set_style_border_width(title, 1, 0);
    lv_obj_set_style_border_color(title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_width(title, lv_pct(100));
}

static void make_field_label(lv_obj_t *parent, const char* text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_top(lbl, 4, 0);
}

static void make_field_value(lv_obj_t *parent, const char* text) {
    lv_obj_t *val = lv_label_create(parent);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_label_set_text(val, text);
    lv_obj_set_style_pad_bottom(val, 4, 0);
}

static void make_hint(lv_obj_t *parent, const char* text) {
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, text);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_pad_top(hint, 12, 0);
}

void update_settings_list() {
    lv_obj_clean(lst_settings);
    lv_obj_set_flex_flow(lst_settings, LV_FLEX_FLOW_COLUMN);
    
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
    } else if (_in_timeout_select) {
        make_section_title(lst_settings, "Screen Timeout");
        
        for (int i = 0; i < _timeout_options_count; i++) {
            lv_obj_t *btn = lv_btn_create(lst_settings);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_style_bg_color(btn, (i == _timeout_selected_idx) ? lv_color_hex(0x0096d8) : lv_color_hex(0x333333), 0);
            
            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, _timeout_labels[i]);
            lv_obj_center(label);
        }
        
        make_hint(lst_settings, "G0:Select  G45:Scroll  Long:Save");
    } else if (_in_time_set) {
        make_section_title(lst_settings, "Set Time");
        
        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", _time_edit_hour, _time_edit_minute);
        lv_obj_t *time_label = lv_label_create(lst_settings);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
        lv_label_set_text(time_label, time_buf);
        lv_obj_set_style_pad_top(time_label, 20, 0);
        lv_obj_set_style_pad_bottom(time_label, 10, 0);
        
        lv_obj_t *field_hint = lv_label_create(lst_settings);
        char field_buf[32];
        if (_time_editing) {
            snprintf(field_buf, sizeof(field_buf), "Editing: %s", _time_edit_field == 0 ? "Hour" : "Minute");
        } else {
            snprintf(field_buf, sizeof(field_buf), "Selected: %s", _time_edit_field == 0 ? "Hour" : "Minute");
        }
        lv_label_set_text(field_hint, field_buf);
        lv_obj_set_style_text_color(field_hint, lv_color_hex(0x0096d8), 0);
        
        if (!_time_editing) {
            make_hint(lst_settings, "G0:Edit  G45:Switch  Long:Save");
        } else {
            make_hint(lst_settings, "G0:+1  G45:-1  Long:Save");
        }
    } else {
        switch (_settings_category) {
            case SettingsCategory::PUBLIC_INFO: {
                make_section_title(lst_settings, "Public Info");
                
                make_field_label(lst_settings, "Name");
                make_field_value(lst_settings, the_mesh.getNodeName());
                
                char buf[32];
                snprintf(buf, sizeof(buf), "%06lu", (unsigned long)the_mesh.getBLEPin());
                make_field_label(lst_settings, "BLE PIN");
                make_field_value(lst_settings, buf);
                
                make_hint(lst_settings, "Long G0: Back");
                break;
            }
            case SettingsCategory::RADIO_SETUP: {
                make_section_title(lst_settings, "Radio Setup");
                
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f MHz", LORA_FREQ);
                make_field_label(lst_settings, "Frequency");
                make_field_value(lst_settings, buf);
                
                snprintf(buf, sizeof(buf), "SF%d", LORA_SF);
                make_field_label(lst_settings, "Spreading Factor");
                make_field_value(lst_settings, buf);
                
                snprintf(buf, sizeof(buf), "%.1f kHz", LORA_BW);
                make_field_label(lst_settings, "Bandwidth");
                make_field_value(lst_settings, buf);
                
                snprintf(buf, sizeof(buf), "%d dBm", LORA_TX_POWER);
                make_field_label(lst_settings, "TX Power");
                make_field_value(lst_settings, buf);
                
                make_hint(lst_settings, "Long G0: Back");
                break;
            }
            case SettingsCategory::THEME: {
                make_section_title(lst_settings, "Theme");
                
                make_field_label(lst_settings, "Backlight");
                make_field_value(lst_settings, "5%");
                
                make_field_label(lst_settings, "Main Color");
                lv_obj_t *color_val = lv_label_create(lst_settings);
                lv_obj_set_style_text_font(color_val, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(color_val, lv_color_hex(0x0000FF), 0);
                lv_label_set_text(color_val, "BLUE");
                lv_obj_set_style_pad_bottom(color_val, 4, 0);
                
                make_field_label(lst_settings, "Screen Timeout");
                make_field_value(lst_settings, _timeout_labels[_timeout_selected_idx]);
                
                make_hint(lst_settings, "G0:Change  Long:Back");
                break;
            }
            case SettingsCategory::OTHER: {
                make_section_title(lst_settings, "Other");
                
                uint32_t now_secs = rtc_clock.getCurrentTime();
                int h = (now_secs / 3600) % 24;
                int m = (now_secs % 3600) / 60;
                char buf[32];
                snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
                make_field_label(lst_settings, "Set Time");
                make_field_value(lst_settings, buf);
                
                make_field_label(lst_settings, "Battery");
                make_field_value(lst_settings, "OK");
                
                lv_obj_t *reset_label = lv_label_create(lst_settings);
                lv_label_set_text(reset_label, "Factory Reset");
                lv_obj_set_style_text_color(reset_label, lv_color_hex(0x888888), 0);
                lv_obj_set_style_pad_top(reset_label, 4, 0);
                
                lv_obj_t *reset_val = lv_label_create(lst_settings);
                lv_obj_set_style_text_font(reset_val, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(reset_val, lv_color_hex(0xFF0000), 0);
                lv_label_set_text(reset_val, "Hold to reset");
                lv_obj_set_style_pad_bottom(reset_val, 4, 0);
                
                make_hint(lst_settings, "G0:Set Time  Long:Back");
                break;
            }
            case SettingsCategory::DEVICE_INFO: {
                make_section_title(lst_settings, "Device Info");
                
                make_field_label(lst_settings, "Node Name");
                make_field_value(lst_settings, the_mesh.getNodeName());
                
                make_field_label(lst_settings, "Firmware");
                make_field_value(lst_settings, "v1.0");
                
                char buf[32];
                snprintf(buf, sizeof(buf), "SX1268 %.1f MHz", LORA_FREQ);
                make_field_label(lst_settings, "Radio");
                make_field_value(lst_settings, buf);
                
                snprintf(buf, sizeof(buf), "%ldmin", rtc_clock.getCurrentTime() / 60);
                make_field_label(lst_settings, "Uptime");
                make_field_value(lst_settings, buf);
                
                make_hint(lst_settings, "Long G0: Back");
                break;
            }
            default:
                break;
        }
    }
}

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
        lv_obj_set_style_text_align(lbl_chat_title_overlay, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_chat_title_overlay, lv_pct(100));
        lv_obj_set_style_bg_color(lbl_chat_title_overlay, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(lbl_chat_title_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_top(lbl_chat_title_overlay, 5, 0);
        lv_obj_set_style_pad_bottom(lbl_chat_title_overlay, 5, 0);
        
        lst_chat_overlay = lv_list_create(chat_overlay);
        lv_obj_set_size(lst_chat_overlay, lv_pct(100), lv_pct(90));
        lv_obj_set_style_border_width(lst_chat_overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lst_chat_overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_scrollbar_mode(lst_chat_overlay, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_width(lst_chat_overlay, 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(lst_chat_overlay, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    }
    
    lv_label_set_text(lbl_chat_title_overlay, title);
    update_chat_list();
    
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
    lv_obj_set_flex_align(tab_home, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // 顶部行：右侧显示电量
    home_top_row = lv_obj_create(tab_home);
    lv_obj_set_size(home_top_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(home_top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_top_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(home_top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_top_row, 0, 0);
    lv_obj_set_style_pad_top(home_top_row, 5, 0);
    lv_obj_set_style_pad_bottom(home_top_row, 5, 0);
    lv_obj_set_style_pad_left(home_top_row, 5, 0);
    lv_obj_set_style_pad_right(home_top_row, 15, 0);
    lv_obj_set_style_pad_column(home_top_row, 5, 0);
    
    lbl_batt = lv_label_create(home_top_row);
    lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_batt, "100");
    
    lbl_batt_icon = lv_label_create(home_top_row);
    lv_obj_set_style_text_font(lbl_batt_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_batt_icon, lv_color_hex(0x00B050), 0);
    lv_label_set_text(lbl_batt_icon, LV_SYMBOL_BATTERY_FULL);
    
    // spacer
    lv_obj_t *spacer = lv_obj_create(tab_home);
    lv_obj_set_size(spacer, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    
    // 创建点阵时钟容器
    clock_container = lv_obj_create(tab_home);
    lv_obj_set_size(clock_container, lv_pct(100), 100); // 7行 * 13像素 ≈ 91像素
    lv_obj_set_style_bg_opa(clock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_container, 0, 0);
    lv_obj_set_style_pad_all(clock_container, 0, 0);
    create_matrix_clock(clock_container);
    
    lbl_date = lv_label_create(tab_home);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_24, 0);
    lv_label_set_text(lbl_date, "2024-01-01");
    
    // Contacts 页
    lv_obj_set_flex_flow(tab_contacts, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_contacts = lv_label_create(tab_contacts);
    lv_label_set_text(hdr_contacts, "Contacts");
    lv_obj_set_style_text_font(hdr_contacts, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_contacts, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_contacts, lv_color_hex(0x333333), 0);
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
    lv_obj_set_style_pad_top(lst_contacts, 0, 0);
    lv_obj_set_style_pad_bottom(lst_contacts, 5, 0);
    lv_obj_set_style_pad_left(lst_contacts, 5, 0);
    lv_obj_set_style_pad_right(lst_contacts, 5, 0);
    lv_obj_set_style_pad_row(lst_contacts, 5, 0);
    
    // Channels 页
    lv_obj_set_flex_flow(tab_channels, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_channels = lv_label_create(tab_channels);
    lv_label_set_text(hdr_channels, "Channels");
    lv_obj_set_style_text_font(hdr_channels, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_channels, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_channels, lv_color_hex(0x333333), 0);
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
    lv_obj_set_style_pad_top(lst_channels, 0, 0);
    lv_obj_set_style_pad_bottom(lst_channels, 5, 0);
    lv_obj_set_style_pad_left(lst_channels, 5, 0);
    lv_obj_set_style_pad_right(lst_channels, 5, 0);
    lv_obj_set_style_pad_row(lst_channels, 5, 0);
    
    // Settings 页
    lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t *hdr_settings = lv_label_create(tab_settings);
    lv_label_set_text(hdr_settings, "Settings");
    lv_obj_set_style_text_font(hdr_settings, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_settings, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(hdr_settings, lv_color_hex(0x333333), 0);
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
    lv_obj_set_style_pad_top(lst_settings, 0, 0);
    lv_obj_set_style_pad_bottom(lst_settings, 5, 0);
    lv_obj_set_style_pad_left(lst_settings, 5, 0);
    lv_obj_set_style_pad_right(lst_settings, 5, 0);
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
        lv_obj_scroll_by(lst_settings, 0, -30, LV_ANIM_ON);
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
        update_settings_list();
        return;
    }

    // 如果Chat页面可见，隐藏它
    if (_chat_visible) {
        hide_chat_overlay();
        return;
    }

    // 在Settings详情页时，G45滚动页面
    uint16_t current_tab = lv_tabview_get_tab_act(tabview);
    if (current_tab == 3 && _settings_selected) {
        // 滚动lst_settings列表
        lv_obj_scroll_by(lst_settings, 0, -30, LV_ANIM_ON);
        return;
    }

    // G45短按始终切换到下一个tab
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
        update_settings_list();
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
            _in_timeout_select = false;
            _in_time_set = false;
            _time_editing = false;
            update_settings_list();
            return;
        }
        // 主菜单，进入子菜单
        _settings_selected = true;
        switch (_settings_menu_idx) {
            case 0: _settings_category = SettingsCategory::PUBLIC_INFO; break;
            case 1: _settings_category = SettingsCategory::RADIO_SETUP; break;
            case 2: _settings_category = SettingsCategory::THEME; break;
            case 3: _settings_category = SettingsCategory::OTHER; break;
            case 4: _settings_category = SettingsCategory::DEVICE_INFO; break;
        }
        _in_timeout_select = false;
        _in_time_set = false;
        _time_editing = false;
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
        update_settings_list();
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
        } else {
            if (_settings_category == SettingsCategory::THEME) {
                _in_timeout_select = true;
                update_settings_list();
            } else if (_settings_category == SettingsCategory::OTHER) {
                _in_time_set = true;
                _time_edit_field = 0;
                _time_editing = false;
                uint32_t now_secs = rtc_clock.getCurrentTime();
                _time_edit_hour = (now_secs / 3600) % 24;
                _time_edit_minute = (now_secs % 3600) / 60;
                update_settings_list();
            }
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