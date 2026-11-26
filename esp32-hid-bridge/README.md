# ESP32-S3 BLE HID + LAN UDP Bridge

本目录包含运行在 **Waveshare ESP32-S3-Touch-AMOLED-1.75**（基于 `xiaozhi-esp32` / ESP-IDF v6.0 + NimBLE）上的蓝牙 HID 模拟与局域网 UDP 桥接核心源码：

- `ble_hid_controller.h/.cc`：NimBLE 复合 BLE HID（键盘 + 鼠标），负责模拟 iPhone 辅助触控（AssistiveTouch）对称上滑/下滑/点击/双击点赞与光标微调；
- `lan_hid_server.h/.cc`：超轻量级局域网 UDP 控制服务（默认端口 `4210`），支持 `DISCOVER`、`STATUS`、`SWIPE_UP`、`SWIPE_DOWN`、`TAP`、`DOUBLE_TAP`、`MOVE:<dx>:<dy>` 指令；
- `hid_remote_ui.h/.cc`：1.75 寸圆形 AMOLED 触控遥控与设备设置界面（LVGL）。

## UDP 控制协议（端口 4210）

| 指令 | 作用 | 返回示例 |
| :--- | :--- | :--- |
| `DISCOVER` / `STATUS` | 发现设备并查询蓝牙连接状态 | `{"ok":true,"device":"Xiaozhi-HID","ip":"...","port":4210,"connected":true,"mode":"video"}` |
| `SWIPE_UP` | 模拟手指上滑（下一个视频） | `{"ok":true,"action":"swipe_up",...}` |
| `SWIPE_DOWN` | 模拟手指下滑（上一个视频） | `{"ok":true,"action":"swipe_down",...}` |
| `TAP` | 屏幕单击（暂停/播放） | `{"ok":true,"action":"tap",...}` |
| `DOUBLE_TAP` | 屏幕双击（点赞） | `{"ok":true,"action":"double_tap",...}` |
| `MOVE:<dx>:<dy>` | 微调 iOS 鼠标光标位置 | `{"ok":true,"action":"move",...}` |
