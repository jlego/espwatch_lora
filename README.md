# ESPWatch LoRa

基于 ESP32-S3 的智能手表 LoRa 通信设备，配备 240x285 TFT 显示屏和 E22-400MM22S LoRa 模块。

## 硬件规格

| 组件 | 型号/参数 |
|------|----------|
| 主控 | ESP32-S3 |
| LoRa | E22-400MM22S (SX1268) |
| 屏幕 | 240x285 TFT |
| 电量计 | CW2015CHBD (I2C 0x62) |
| 温度/气压 | BMP280 (I2C 0x76) |
| 加速度计 | LIS2DH12TR (I2C 0x18) |
| 实时时钟 | BM8563EMA (I2C 0x51) |
| 按钮 | GPIO0 (G0) + GPIO45 |
| 蜂鸣器 | 被动蜂鸣器 (GPIO3) |

## 功能特性

### 消息通信
- **私聊消息** — 点对点加密通信
- **群聊广播** — 向所有在线设备发送消息
- **联系人管理** — 自动发现和保存联系人
- **消息历史** — 本地存储最近消息记录
- **聊天界面** — 气泡式消息显示，发送/接收消息左右对齐

### 界面功能
- **时钟显示** — 点阵数字时钟，支持 UTC+8 时区
- **电量显示** — CW2015 电量计实时显示
- **联系人列表** — 显示已发现的联系人
- **频道列表** — 广播频道管理
- **设置菜单** — 时间设置、设备信息等

### 控制方式
| 操作 | 功能 |
|------|------|
| G0 短按 | 确认/选择 |
| G0 长按 | 保存/返回 |
| G45 短按 | 上/下滚动 |
| G45 长按 | 进入时间设置 |

## 编译烧录

```bash
# 编译
pio run -e ESPWatch-LoRa_e22_400mm22s_companion --target upload

# 仅编译
pio run -e ESPWatch-LoRa_e22_400mm22s_companion
```

## 蓝牙配对

设备支持 BLE 蓝牙连接，可与手机 APP 配对：
- 配对码：123456
- 配对模式：MITM + Bonding

## 项目结构

```
├── variants/espwatch_lora/
│   ├── custom_main.cpp    # 主程序入口
│   ├── CustomBoard.h      # 硬件板级配置
│   └── pins_arduino.h     # 引脚定义
├── examples/companion_radio/
│   ├── MyMesh.cpp         # Mesh 网络实现
│   └── MyMesh.h           # Mesh 头文件
└── src/
    └── helpers/           # 辅助模块
```

## 基于项目

基于 [MeshCore](https://github.com/meshcore-dev/MeshCore) mesh 网络通信固件开发。