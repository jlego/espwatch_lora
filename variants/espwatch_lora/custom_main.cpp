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
static SettingsCategory _settings_category = SettingsCategory::MAIN_MENU;
static int _settings_menu_idx = 0;       // 设置项索引（滚动位置）
static int _contacts_scroll = 0;          // 联系人滚动
static int _channels_scroll = 0;          // 频道滚动
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
    // 底部 tab bar (0, 258, 240, 27) - 仅非设置页显示
    int bar_y = 258;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, bar_y, 240, 27);

    display.setTextSize(2);

    // Contacts tab (0-120)
    if (_menu_state == MenuScreen::CONTACTS) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, bar_y, 120, 27);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(13, bar_y + 7);
        display.print("Contacts");
    } else {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(13, bar_y + 7);
        display.print("Contacts");
    }

    // Channels tab (120-240)
    if (_menu_state == MenuScreen::CHANNELS) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(120, bar_y, 120, 27);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(133, bar_y + 7);
        display.print("Channels");
    } else {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(133, bar_y + 7);
        display.print("Channels");
    }
}

void draw_chat_back_hint() {
    // (help hint removed)
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

    // 列表区 (y: 30 ~ 256)，每一行高 28，可显示 8 行
    const int MAX_VISIBLE = 8;
    const int ITEM_H = 28;
    int y_start = 32;

    // 计算滚动
    if (_contacts_scroll > max(0, num_contacts - MAX_VISIBLE))
        _contacts_scroll = max(0, num_contacts - MAX_VISIBLE);
    if (_contacts_scroll < 0) _contacts_scroll = 0;

    // 显示计数
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "%d nodes", num_contacts);
    display.setCursor(4, y_start);
    display.print(buf);

    // 显示节点列表
    for (int i = 0; i < MAX_VISIBLE; i++) {
        int idx = _contacts_scroll + i;
        if (idx >= num_contacts) break;

        int y = y_start + 18 + i * ITEM_H;
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(idx, contact)) continue;

        // 选中/未选中框
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(4, y, 232, ITEM_H - 2);

        display.setTextSize(1);
        display.setColor(DisplayDriver::LIGHT);

        // 节点名
        char filtered[32];
        strncpy(filtered, contact.name, sizeof(filtered));
        filtered[sizeof(filtered) - 1] = '\0';
        display.setCursor(10, y + 7);
        display.print(filtered);

        // 路径跳数
        snprintf(buf, sizeof(buf), "%d hops", contact.out_path_len);
        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(200, y + 7);
        display.print(buf);

        // 最后见时间
        display.setColor(DisplayDriver::GREEN);
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

    // ESPWatch 只显示广播频道（简化版 cardputer 的 channel 列表）
    const char* channels[] = {"Broadcast", "Contacts", "Direct"};
    int num_channels = 3;

    const int ITEM_H = 28;
    int y_start = 40;

    if (_channels_scroll > max(0, num_channels - 7))
        _channels_scroll = max(0, num_channels - 7);

    for (int i = 0; i < 7; i++) {
        int idx = _channels_scroll + i;
        if (idx >= num_channels) break;

        int y = y_start + i * ITEM_H;

        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(4, y, 232, ITEM_H - 2);

        display.setTextSize(2);
        display.setCursor(10, y + 7);
        display.print(channels[idx]);

        // 指示点（黄色）
        display.setColor(DisplayDriver::YELLOW);
        display.fillRect(220, y + 11, 6, 6);
    }

    // 频道说明
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.setCursor(4, y_start + 7 * ITEM_H);
    snprintf(buf, sizeof(buf), "Channel count: %d", num_channels);
    display.print(buf);
}

// ========= PAGE 3: CHAT（聊天，只读气泡） =========
void render_chat() {
    char buf[64];

    // 标题栏（显示频道名）
    draw_header("Broadcast");

    // 聊天消息区
    const int MAX_MSGS = 6;
    const int BUBBLE_H = 32;
    int y_start = 35;

    // 显示最近的一些消息（如果 Mesh 有接口可用）
    // 简化版：显示一些状态信息作为"消息"
    int num_contacts = the_mesh.getNumContacts();
    long uptime = rtc_clock.getCurrentTime();

    // 消息气泡（模拟）
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

    // 气泡 2：在线节点
    if (num_contacts > 0) {
        y += BUBBLE_H + 3;
        display.setColor(DisplayDriver::GREEN);
        display.fillRect(8, y, 224, BUBBLE_H);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(14, y + 6);
        snprintf(buf, sizeof(buf), "Network: %d peers online", num_contacts);
        display.print(buf);
    } else {
        y += BUBBLE_H + 3;
        display.setColor(DisplayDriver::YELLOW);
        display.fillRect(8, y, 224, BUBBLE_H);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(14, y + 6);
        display.print("No contacts yet");
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

    // 气泡 4：节点列表（如果有）
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
    if (_settings_menu_idx > max(0, num_cats - 7))
        _settings_menu_idx = max(0, num_cats - 7);
    if (_settings_menu_idx < 0) _settings_menu_idx = 0;

    for (int i = 0; i < 7; i++) {
        int idx = _settings_menu_idx + i;
        if (idx >= num_cats) break;

        int y = y_start + i * ITEM_H;

        // 选中框（当前选中的高亮）
        if (idx == _settings_menu_idx && _settings_selected) {
            display.setColor(DisplayDriver::BLUE);
            display.fillRect(4, y, 232, ITEM_H - 2);
            display.setColor(DisplayDriver::LIGHT);
        } else {
            display.setColor(DisplayDriver::LIGHT);
            display.drawRect(4, y, 232, ITEM_H - 2);
        }

        display.setTextSize(2);
        display.setCursor(10, y + 7);
        display.print(categories[idx]);
    }
}

void render_settings_public_info() {
    char buf[64];
    draw_header("Public Info");

    int y = 40;

    // 用户名
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Name");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print(the_mesh.getNodeName());

    // BLE PIN
    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("BLE PIN");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%d", (int)BLE_PIN_CODE);
    display.print(buf);
}

void render_settings_radio_setup() {
    char buf[64];
    draw_header("Radio Setup");

    int y = 40;

    // Frequency
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Frequency");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%.1f MHz", LORA_FREQ);
    display.print(buf);

    // Spreading Factor
    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Spreading Factor");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "SF%d", LORA_SF);
    display.print(buf);

    // Bandwidth
    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Bandwidth");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%.1f kHz", LORA_BW);
    display.print(buf);

    // TX Power
    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
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

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Backlight");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("5%");

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Main Color");
    display.setTextSize(2);
    display.setColor(DisplayDriver::BLUE);
    display.setCursor(10, y + 18);
    display.print("BLUE");

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Screen Timeout");
    display.setTextSize(2);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(10, y + 18);
    display.print("Never");
}

void render_settings_other() {
    draw_header("Other");

    int y = 40;

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Screen Timeout");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("Never");

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Battery");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print("OK");

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Factory Reset");
    display.setTextSize(2);
    display.setColor(DisplayDriver::RED);
    display.setCursor(10, y + 18);
    display.print("Hold to reset");
}

void render_settings_device_info() {
    char buf[64];
    draw_header("Device Info");

    int y = 40;

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Node Name");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    display.print(the_mesh.getNodeName());

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Firmware");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "v1.0");
    display.print(buf);

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Radio");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "SX1268 %.1f MHz", LORA_FREQ);
    display.print(buf);

    y += 34;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(4, y, 232, 28);
    display.setTextSize(1);
    display.setCursor(10, y + 6);
    display.print("Uptime");
    display.setTextSize(2);
    display.setCursor(10, y + 18);
    snprintf(buf, sizeof(buf), "%ldmin", rtc_clock.getCurrentTime() / 60);
    display.print(buf);
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

    // 底部栏
    if (_menu_state == MenuScreen::CONTACTS || _menu_state == MenuScreen::CHANNELS) {
        draw_tab_bar();
    }

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
    Serial.println("[UI] G45: Next page");

    if (_menu_state == MenuScreen::SETTINGS && !_settings_selected) {
        // 设置主菜单：向下滚动选中项
        const int num_cats = 5;
        _settings_menu_idx = (_settings_menu_idx + 1) % num_cats;
    } else if (_menu_state == MenuScreen::CONTACTS) {
        // Contacts 页：切到 Channels
        _menu_state = MenuScreen::CHANNELS;
    } else if (_menu_state == MenuScreen::CHANNELS) {
        _menu_state = MenuScreen::CHAT;
    } else if (_menu_state == MenuScreen::CHAT) {
        _menu_state = MenuScreen::SETTINGS;
    } else if (_menu_state == MenuScreen::SETTINGS && _settings_selected) {
        // 设置子分类：切到下一个主页面（Contacts）
        _settings_selected = false;
        _menu_state = MenuScreen::CONTACTS;
    }

    draw_status_screen();
    next_refresh = now + 60000;
  }

  // --- G0 (Enter / Select / Advertise) ---
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
    } else {
        // 非设置页：发送广播
        Serial.println("[UI] G0: Send advert");
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