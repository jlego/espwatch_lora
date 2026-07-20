#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
#include "target.h"

StdRNG fast_rng;
SimpleMeshTables tables;

static unsigned long next_refresh = 0;

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

MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
#ifdef DISPLAY_CLASS
   , nullptr
#endif
);

void halt() {
  while (1) {
    delay(1000);
    Serial.println("System halted");
  }
}

void draw_startup_screen() {
#ifdef DISPLAY_CLASS
  if (!display.begin()) return;

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::GREEN);
  display.setTextSize(2);
  display.drawTextCentered(120, 30, "MeshCore");
  display.setTextSize(1);
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(120, 70, "ESPWatch LoRa");
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(120, 100, "Initializing...");
  display.endFrame();
#endif
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef CUSTOM_BOARD
  draw_startup_screen();
  user_btn.begin();
  user_btn2.begin();
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

  Serial.println("Boot complete");
}

// ===================== 主界面：cardputer 风格 =====================

#ifdef CUSTOM_BOARD

// ========= 状态变量（模仿 cardputer 的 MenuScreen + SettingsCategory） =========
enum class MenuScreen {
    CONTACTS,
    CHANNELS,
    CHAT,
    SETTINGS
};

enum class SettingsCategory {
    MAIN_MENU,
    PUBLIC_INFO,
    RADIO_SETUP,
    THEME,
    OTHER,
    DEVICE_INFO
};

static MenuScreen _menu_state = MenuScreen::CONTACTS;
static MenuScreen _chat_parent = MenuScreen::CONTACTS;  // chat 页的来源（用于返回）
static SettingsCategory _settings_category = SettingsCategory::MAIN_MENU;
static int _settings_menu_idx = 0;       // 设置项索引（滚动位置）
static int _contacts_scroll = 0;          // 联系人滚动（窗口起点）
static int _contacts_selected = 0;        // 选中的联系人索引
static int _channels_scroll = 0;          // 频道滚动（窗口起点）
static int _channels_selected = 0;        // 选中的频道索引
static int _chat_scroll = 0;              // 聊天消息滚动
static bool _settings_selected = false;   // 设置中：是否在子分类里

// ========= 通用 UI 组件 =========
void draw_header(const char* title) {
    // 顶部标题栏 (0, 0, 240, 28)
    display.setColor(DisplayDriver::BLUE);
    display.fillRect(0, 0, 240, 28);
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.setCursor(14, 7);
    display.print("<");

    // 标题居中
    int title_len = strlen(title);
    int title_x = 120 - (title_len * 6);
    if (title_x < 25) title_x = 25;
    display.setCursor(title_x, 7);
    display.print(title);
}

void draw_tab_bar() {
    // Chat 页：不显示 tab bar，显示返回提示
    if (_menu_state == MenuScreen::CHAT) {
        int bar_y = 258;
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(0, bar_y, 240, 27);
        display.setTextSize(2);
        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(40, bar_y + 7);
        display.print("G0:Back  G45:Scroll");
        return;
    }

    // 底部 tab bar (0, 258, 240, 27) - 3个tab，不含 Chat
    int bar_y = 258;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, bar_y, 240, 27);

    display.setTextSize(1);

    // 3个tab，每个80像素宽 — Chat 是联系人/频道内的子功能，不是顶级 tab
    const int TAB_W = 80;
    const char* tab_names[] = {"Contacts", "Channels", "Settings"};
    MenuScreen tab_states[] = {MenuScreen::CONTACTS, MenuScreen::CHANNELS, MenuScreen::SETTINGS};

    for (int i = 0; i < 3; i++) {
        int tab_x = i * TAB_W;
        if (_menu_state == tab_states[i]) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(tab_x, bar_y, TAB_W, 27);
            display.setColor(DisplayDriver::DARK);
        } else {
            display.setColor(DisplayDriver::LIGHT);
        }
        // 居中文字
        int text_len = strlen(tab_names[i]);
        int cursor_x = tab_x + (TAB_W - text_len * 6) / 2;
        display.setCursor(cursor_x, bar_y + 9);
        display.print(tab_names[i]);
    }
}

void draw_settings_bottom_menu(bool in_sub) {
    // 设置底部：Save / Back
    int bar_y = 258;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, bar_y, 240, 27);

    display.setTextSize(2);

    // 左：Back (120宽)
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(13, bar_y + 7);
    display.print("Back");

    // 右：Next hint
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(133, bar_y + 7);
    display.print("Next");
}

// ========= PAGE 1: CONTACTS =========
void render_contacts() {
    char buf[64];
    int num_contacts = the_mesh.getNumContacts();

    // 标题栏
    draw_header("Contacts");

    // 列表区 (y: 30 ~ 256)，每一行高 28，可显示 7 行
    const int MAX_VISIBLE = 7;
    const int ITEM_H = 28;
    int y_start = 32;

    // 选中项边界检查
    if (_contacts_selected >= num_contacts) _contacts_selected = max(0, num_contacts - 1);
    if (_contacts_selected < 0) _contacts_selected = 0;

    // 让选中项保持在可见区域内
    if (_contacts_selected >= _contacts_scroll + MAX_VISIBLE)
        _contacts_scroll = _contacts_selected - MAX_VISIBLE + 1;
    if (_contacts_selected < _contacts_scroll)
        _contacts_scroll = _contacts_selected;
    if (_contacts_scroll < 0) _contacts_scroll = 0;

    // 显示计数 + 选中提示
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "%d nodes  [select:%d]", num_contacts, _contacts_selected + 1);
    display.setCursor(4, y_start);
    display.print(buf);

    // 显示节点列表
    for (int i = 0; i < MAX_VISIBLE; i++) {
        int idx = _contacts_scroll + i;
        if (idx >= num_contacts) break;

        int y = y_start + 18 + i * ITEM_H;
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(idx, contact)) continue;

        bool is_selected = (idx == _contacts_selected);
        if (is_selected) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(2, y + 2, 236, ITEM_H - 4);
            display.setColor(DisplayDriver::DARK);
        } else {
            display.setColor(DisplayDriver::LIGHT);
        }

        display.setTextSize(1);

        // 节点名
        char filtered[32];
        strncpy(filtered, contact.name, sizeof(filtered));
        filtered[sizeof(filtered) - 1] = '\0';
        display.setCursor(10, y + 7);
        display.print(filtered);

        // 路径跳数
        snprintf(buf, sizeof(buf), "%d hops", contact.out_path_len);
        if (is_selected) {
            display.setColor(DisplayDriver::BLUE);
        } else {
            display.setColor(DisplayDriver::YELLOW);
        }
        display.setCursor(200, y + 7);
        display.print(buf);

        // 最后见时间
        if (is_selected) {
            display.setColor(DisplayDriver::BLUE);
        } else {
            display.setColor(DisplayDriver::GREEN);
        }
        display.setCursor(10, y + 18);
        snprintf(buf, sizeof(buf), "Last: %us ago",
                 (unsigned)(rtc_clock.getCurrentTime() - contact.lastmod));
        display.print(buf);
    }

    // 空状态
    if (num_contacts == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(2);
        display.setCursor(50, y_start + 50);
        display.print("No nodes yet");
        display.setTextSize(1);
        display.setCursor(55, y_start + 80);
        display.print("Advertise to find peers");
    }
}

// ========= PAGE 2: CHANNELS =========
void render_channels() {
    char buf[64];

    draw_header("Channels");

    // 频道列表（简化版 — 这3个频道是系统预设的）
    const char* channels[] = {"Broadcast", "Contacts", "Direct"};
    int num_channels = 3;

    const int ITEM_H = 28;
    int y_start = 40;

    // 选中项边界检查
    if (_channels_selected >= num_channels) _channels_selected = 0;
    if (_channels_selected < 0) _channels_selected = num_channels - 1;

    // 让选中项保持在可见区域内
    if (_channels_selected >= _channels_scroll + 7)
        _channels_scroll = _channels_selected - 6;
    if (_channels_selected < _channels_scroll)
        _channels_scroll = _channels_selected;
    if (_channels_scroll < 0) _channels_scroll = 0;

    for (int i = 0; i < 7; i++) {
        int idx = _channels_scroll + i;
        if (idx >= num_channels) break;

        int y = y_start + i * ITEM_H;
        bool is_selected = (idx == _channels_selected);

        // 选中 = 白色反色
        if (is_selected) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(4, y + 2, 232, ITEM_H - 4);
            display.setColor(DisplayDriver::DARK);
        } else {
            display.setColor(DisplayDriver::LIGHT);
        }

        display.setTextSize(2);
        display.setCursor(10, y + 7);
        display.print(channels[idx]);

        // 指示点
        display.setColor(is_selected ? DisplayDriver::BLUE : DisplayDriver::YELLOW);
        display.fillRect(220, y + 11, 6, 6);

        // 白色下边线
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(4, y + ITEM_H - 2, 232, 1);
    }

    // 频道说明
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.setCursor(4, y_start + 7 * ITEM_H);
    snprintf(buf, sizeof(buf), "Channels: %d", num_channels);
    display.print(buf);
}

// ========= PAGE 3: CHAT（聊天，只读气泡） =========
void render_chat() {
    char buf[64];
    char title_buf[48];

    // 根据来源确定标题
    if (_chat_parent == MenuScreen::CHANNELS) {
        const char* channels[] = {"Broadcast", "Contacts", "Direct"};
        snprintf(title_buf, sizeof(title_buf), "%s", channels[_channels_selected]);
    } else {
        ContactInfo c;
        if (the_mesh.getContactByIdx(_contacts_selected, c)) {
            strncpy(title_buf, c.name, sizeof(title_buf) - 1);
            title_buf[sizeof(title_buf) - 1] = '\0';
        } else {
            snprintf(title_buf, sizeof(title_buf), "Chat");
        }
    }

    // 标题栏
    draw_header(title_buf);

    // 聊天消息区
    const int MAX_MSGS = 6;
    const int BUBBLE_H = 32;
    int y_start = 35;

    int num_contacts = the_mesh.getNumContacts();
    long uptime = rtc_clock.getCurrentTime();

    display.setTextSize(1);

    // 气泡 1：系统欢迎
    int y = y_start;
    display.setColor(DisplayDriver::BLUE);
    display.fillRect(8, y, 224, BUBBLE_H);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(14, y + 6);
    display.print("System: Welcome to MeshCore");
    display.setCursor(14, y + 18);
    snprintf(buf, sizeof(buf), "Node: %s", the_mesh.getNodeName());
    display.print(buf);
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(8, y + BUBBLE_H, 224, 1);

    // 气泡 2：当前聊天对象信息
    if (_chat_parent == MenuScreen::CONTACTS && num_contacts > 0) {
        ContactInfo c;
        if (the_mesh.getContactByIdx(_contacts_selected, c)) {
            y += BUBBLE_H + 3;
            display.setColor(DisplayDriver::GREEN);
            display.fillRect(8, y, 224, BUBBLE_H);
            display.setColor(DisplayDriver::DARK);
            display.setCursor(14, y + 6);
            snprintf(buf, sizeof(buf), "Peer: %s", c.name);
            display.print(buf);
            display.setCursor(14, y + 18);
            snprintf(buf, sizeof(buf), "%d hops · %us ago", c.out_path_len,
                     (unsigned)(rtc_clock.getCurrentTime() - c.lastmod));
            display.print(buf);
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(8, y + BUBBLE_H, 224, 1);
        }
    } else {
        y += BUBBLE_H + 3;
        display.setColor(DisplayDriver::GREEN);
        display.fillRect(8, y, 224, BUBBLE_H);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(14, y + 6);
        snprintf(buf, sizeof(buf), "Channel: %s", title_buf);
        display.print(buf);
        display.setCursor(14, y + 18);
        snprintf(buf, sizeof(buf), "%d peers in network", num_contacts);
        display.print(buf);
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(8, y + BUBBLE_H, 224, 1);
    }

    // 气泡 3：运行时间
    y += BUBBLE_H + 3;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(8, y, 224, BUBBLE_H);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(14, y + 6);
    snprintf(buf, sizeof(buf), "Uptime: %ldmin", uptime / 60);
    display.print(buf);
    display.setCursor(14, y + 18);
    snprintf(buf, sizeof(buf), "Msg queue: %d pending", (int)the_mesh.getNumContacts());
    display.print(buf);
    display.fillRect(8, y + BUBBLE_H, 224, 1);

    // 气泡 4：最新节点（如果有联系人）
    if (num_contacts > 0) {
        y += BUBBLE_H + 3;
        display.setColor(DisplayDriver::GREEN);
        display.fillRect(8, y, 224, BUBBLE_H);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(14, y + 6);

        ContactInfo c;
        char filtered[32];
        if (the_mesh.getContactByIdx(0, c)) {
            strncpy(filtered, c.name, sizeof(filtered));
            filtered[sizeof(filtered) - 1] = '\0';
            snprintf(buf, sizeof(buf), "Latest: %s (%d hops)", filtered, c.out_path_len);
            display.print(buf);
            display.setCursor(14, y + 18);
            snprintf(buf, sizeof(buf), "Seen: %us ago", (unsigned)(rtc_clock.getCurrentTime() - c.lastmod));
            display.print(buf);
        }
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(8, y + BUBBLE_H, 224, 1);
    }
}

// ========= PAGE 4: SETTINGS =========
void render_settings_main_menu() {
    char buf[32];

    // 顶部：SETTINGS
    draw_header("Settings");

    // 子分类列表（5 项）
    const char* categories[] = {"Public Info", "Radio Setup", "Theme", "Other", "Device Info"};
    int num_cats = 5;

    const int ITEM_H = 28;
    int y_start = 40;

    // 限制滚动
    if (_settings_menu_idx >= num_cats)
        _settings_menu_idx = num_cats - 1;
    if (_settings_menu_idx < 0) _settings_menu_idx = 0;

    for (int i = 0; i < num_cats; i++) {
        int y = y_start + i * ITEM_H;

        // 选中框（当前选中的高亮）
        if (i == _settings_menu_idx) {
            display.setColor(DisplayDriver::BLUE);
            display.fillRect(4, y, 232, ITEM_H - 2);
            display.setColor(DisplayDriver::LIGHT);
        }

        display.setTextSize(2);
        display.setCursor(10, y + 7);
        display.print(categories[i]);

        // 白色下边线
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(4, y + ITEM_H - 2, 232, 1);
    }
}

void render_settings_public_info() {
    char buf[64];
    draw_header("Public Info");

    int y = 40;

    // 用户名
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Name");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print(the_mesh.getNodeName());

    // BLE PIN
    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("BLE PIN");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%d", (int)BLE_PIN_CODE);
    display.print(buf);

    // 白色下边线
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(4, y + 28, 232, 1);
}

void render_settings_radio_setup() {
    char buf[64];
    draw_header("Radio Setup");

    int y = 40;

    // Frequency
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Frequency");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%.1f MHz", LORA_FREQ);
    display.print(buf);

    // Spreading Factor
    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Spreading Factor");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "SF%d", LORA_SF);
    display.print(buf);

    // Bandwidth
    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Bandwidth");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%.1f kHz", LORA_BW);
    display.print(buf);

    // TX Power
    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("TX Power");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%d dBm", LORA_TX_POWER);
    display.print(buf);
}

void render_settings_theme() {
    draw_header("Theme");

    int y = 40;

    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Backlight");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("5%");

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Main Color");
    display.setTextSize(2);
    display.setColor(DisplayDriver::BLUE);
    display.setCursor(10, y + 18);
    display.print("BLUE");

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Screen Timeout");
    display.setTextSize(2);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(10, y + 18);
    display.print("Never");

    // 白色下边线
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(4, y + 28, 232, 1);
}

void render_settings_other() {
    draw_header("Other");

    int y = 40;

    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Screen Timeout");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("Never");

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Battery");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("OK");

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Factory Reset");
    display.setTextSize(2);
    display.setColor(DisplayDriver::RED);
    display.setCursor(10, y + 18);
    display.print("Hold to reset");

    // 白色下边线
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(4, y + 28, 232, 1);
}

void render_settings_device_info() {
    char buf[64];
    draw_header("Device Info");

    int y = 40;

    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Node Name");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print(the_mesh.getNodeName());

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Firmware");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "v1.0");
    display.print(buf);

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Radio");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "SX1268 %.1f MHz", LORA_FREQ);
    display.print(buf);

    y += 34;
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Uptime");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%ldmin", rtc_clock.getCurrentTime() / 60);
    display.print(buf);

    // 白色下边线
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(4, y + 28, 232, 1);
}

void render_settings() {
    if (!_settings_selected) {
        render_settings_main_menu();
    } else {
        switch (_settings_category) {
            case SettingsCategory::MAIN_MENU:
                render_settings_main_menu();
                break;
            case SettingsCategory::PUBLIC_INFO:
                render_settings_public_info();
                break;
            case SettingsCategory::RADIO_SETUP:
                render_settings_radio_setup();
                break;
            case SettingsCategory::THEME:
                render_settings_theme();
                break;
            case SettingsCategory::OTHER:
                render_settings_other();
                break;
            case SettingsCategory::DEVICE_INFO:
                render_settings_device_info();
                break;
        }
    }
}

// ========= 主渲染函数 =========
void draw_status_screen() {
    unsigned long t0 = millis();
    Serial.printf("[UI] Render start: page=%d\n", (int)_menu_state);

    display.startFrame(DisplayDriver::DARK);

    // === 画正式页面内容 ===
    switch (_menu_state) {
        case MenuScreen::CONTACTS:
            render_contacts();
            break;
        case MenuScreen::CHANNELS:
            render_channels();
            break;
        case MenuScreen::CHAT:
            render_chat();
            break;
        case MenuScreen::SETTINGS:
            render_settings();
            break;
    }

    // 底部栏 - 所有页面都显示tab bar
    draw_tab_bar();

    display.endFrame();
    Serial.printf("[UI] Render total: %lums\n", millis() - t0);
}

#endif  // CUSTOM_BOARD

// ===================== LOOP =====================

void loop() {
  the_mesh.loop();

#ifdef CUSTOM_BOARD
  unsigned long now = millis();

  // 按钮输入（与 cardputer 的键盘处理类似，但简化为 2 个按钮）
  int btn_g0 = user_btn.check();    // GPIO0: 动作键（Enter/Advert/Scroll）
  int btn_g45 = user_btn2.check();  // GPIO45: 切页键（Next）——注意长按交给硬件关机

  // --- G45 (Next / Scroll down) ---
  if (btn_g45 == BUTTON_EVENT_CLICK) {
    board.beep(150, 1500);
    Serial.println("[UI] G45: Next");

    if (_menu_state == MenuScreen::SETTINGS && !_settings_selected) {
        // 设置主菜单：向下滚动选中项
        const int num_cats = 5;
        _settings_menu_idx = (_settings_menu_idx + 1) % num_cats;
        Serial.printf("[UI] G45: Settings menu idx=%d\n", _settings_menu_idx);
    } else if (_menu_state == MenuScreen::SETTINGS && _settings_selected) {
        // 设置子分类：返回 Contacts
        _settings_selected = false;
        _menu_state = MenuScreen::CONTACTS;
        Serial.println("[UI] G45: Back to Contacts from settings");
    } else if (_menu_state == MenuScreen::CONTACTS) {
        // Contacts：向下移动选中项；已在最后一项则切到下一个 tab
        int num_contacts = the_mesh.getNumContacts();
        if (num_contacts > 0) {
            if (_contacts_selected >= num_contacts - 1) {
                _menu_state = MenuScreen::CHANNELS;
                Serial.println("[UI] G45: Contacts -> Channels");
            } else {
                _contacts_selected++;
                Serial.printf("[UI] G45: Contact select=%d\n", _contacts_selected);
            }
        } else {
            _menu_state = MenuScreen::CHANNELS;
            Serial.println("[UI] G45: (no contacts) Contacts -> Channels");
        }
    } else if (_menu_state == MenuScreen::CHANNELS) {
        // Channels：向下移动选中项；已在最后一项则切到下一个 tab
        const int num_channels = 3;
        if (_channels_selected >= num_channels - 1) {
            _menu_state = MenuScreen::SETTINGS;
            Serial.println("[UI] G45: Channels -> Settings");
        } else {
            _channels_selected++;
            Serial.printf("[UI] G45: Channel select=%d\n", _channels_selected);
        }
    } else if (_menu_state == MenuScreen::CHAT) {
        // Chat：滚动消息（当前简化为滚动聊天内容的滚动位）
        _chat_scroll++;
        Serial.println("[UI] G45: Chat scroll");
    }

    draw_status_screen();
    next_refresh = now + 60000;
  }

  // --- G0 长按（返回上一级）- 必须在 CLICK 之前处理 ---
  if (btn_g0 == BUTTON_EVENT_LONG_PRESS) {
    board.beep(200, 1000);
    user_btn.cancelClick();  // 阻止松开时触发 CLICK

    if (_menu_state == MenuScreen::SETTINGS && _settings_selected) {
        // 设置子分类：返回设置主菜单
        _settings_selected = false;
        Serial.println("[UI] G0 long: Back to settings main menu");
    } else if (_menu_state == MenuScreen::CHAT) {
        // Chat：返回进入前的来源页面（Contacts 或 Channels）
        _menu_state = _chat_parent;
        Serial.printf("[UI] G0 long: Chat -> back to %s\n",
                      _chat_parent == MenuScreen::CONTACTS ? "Contacts" : "Channels");
    } else if (_menu_state == MenuScreen::SETTINGS) {
        // Settings：返回 Channels
        _menu_state = MenuScreen::CHANNELS;
        Serial.println("[UI] G0 long: Settings -> Channels");
    } else if (_menu_state == MenuScreen::CHANNELS) {
        // Channels：返回 Contacts
        _menu_state = MenuScreen::CONTACTS;
        Serial.println("[UI] G0 long: Channels -> Contacts");
    } else if (_menu_state == MenuScreen::CONTACTS) {
        // Contacts：返回 Settings（循环）
        _menu_state = MenuScreen::SETTINGS;
        Serial.println("[UI] G0 long: Contacts -> Settings");
    }

    draw_status_screen();
    next_refresh = now + 60000;
  }

  // --- G0 (Enter / Select) ---
  if (btn_g0 == BUTTON_EVENT_CLICK) {
    board.beep(100, 2000);

    if (_menu_state == MenuScreen::SETTINGS) {
        if (!_settings_selected) {
            // 进入选中的子分类
            _settings_selected = true;
            switch (_settings_menu_idx) {
                case 0: _settings_category = SettingsCategory::PUBLIC_INFO; break;
                case 1: _settings_category = SettingsCategory::RADIO_SETUP; break;
                case 2: _settings_category = SettingsCategory::THEME; break;
                case 3: _settings_category = SettingsCategory::OTHER; break;
                case 4: _settings_category = SettingsCategory::DEVICE_INFO; break;
                default: _settings_category = SettingsCategory::MAIN_MENU; break;
            }
            Serial.println("[UI] G0: Enter settings category");
        } else {
            // 在子分类中：返回主菜单
            _settings_selected = false;
            Serial.println("[UI] G0: Back to main settings");
        }
    } else if (_menu_state == MenuScreen::CONTACTS) {
        // Contacts 页：进入选中联系人的聊天
        int num_contacts = the_mesh.getNumContacts();
        if (num_contacts > 0 && _contacts_selected < num_contacts) {
            _chat_parent = MenuScreen::CONTACTS;
            _menu_state = MenuScreen::CHAT;
            ContactInfo c;
            if (the_mesh.getContactByIdx(_contacts_selected, c)) {
                Serial.printf("[UI] G0: Enter chat with %s\n", c.name);
            } else {
                Serial.println("[UI] G0: Enter chat with contact");
            }
        } else {
            // 没有联系人时，发送广播发现节点
            Serial.println("[UI] G0: No contacts — sending advert");
            the_mesh.advert();
        }
    } else if (_menu_state == MenuScreen::CHANNELS) {
        // Channels 页：进入选中频道的聊天
        _chat_parent = MenuScreen::CHANNELS;
        _menu_state = MenuScreen::CHAT;
        const char* channels[] = {"Broadcast", "Contacts", "Direct"};
        Serial.printf("[UI] G0: Enter channel chat: %s\n", channels[_channels_selected]);
    } else if (_menu_state == MenuScreen::CHAT) {
        // Chat 页：发送广播（当前简化为 advert）
        Serial.println("[UI] G0: Chat — sending advert");
        the_mesh.advert();
    }

    draw_status_screen();
    next_refresh = now + 60000;
  }

  // --- 定期刷新屏幕（60秒一次） ---
  if (now >= next_refresh) {    
    draw_status_screen();
    next_refresh = now + 60000;
  }
#endif  // CUSTOM_BOARD
}