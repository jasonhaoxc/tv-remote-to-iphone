#include "ble_hid_controller.h"

#include <algorithm>
#include <cstring>

#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_hid_common.h>
#include <esp_hidd.h>
#include <esp_log.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

extern "C" void ble_store_config_init(void);

namespace {

constexpr char kTag[] = "BleHidController";
constexpr uint16_t kGattSvcHidUuid16 = 0x1812;
constexpr uint8_t kReportIdKeyboard = 1;
constexpr uint8_t kReportIdMouse = 2;

// Standard USB HID keyboard usages
constexpr uint8_t kKeyEnter = 0x28;
constexpr uint8_t kKeyEscape = 0x29;
constexpr uint8_t kKeySpace = 0x2C;
constexpr uint8_t kKeyPageUp = 0x4B;
constexpr uint8_t kKeyPageDown = 0x4E;
constexpr uint8_t kKeyRightArrow = 0x4F;
constexpr uint8_t kKeyLeftArrow = 0x50;
constexpr uint8_t kKeyDownArrow = 0x51;
constexpr uint8_t kKeyUpArrow = 0x52;

// Composite BLE HID Report Map:
//   Report ID 1: Standard 8-byte Keyboard (Input) + 1-byte LEDs (Output)
//   Report ID 2: Standard 4-byte Relative Mouse (Buttons, X, Y, Wheel)
const uint8_t kCompositeHidReportMap[] = {
    // ===== Keyboard (Report ID 1) =====
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, kReportIdKeyboard,  //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs) - Modifier byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs) - Reserved byte
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs) - LED report
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const,Array,Abs) - LED padding
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data,Array,Abs) - 6 Keycodes
    0xC0,              // End Collection

    // ===== Mouse (Report ID 2) =====
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, kReportIdMouse,  //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs) - 3 Buttons
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const,Var,Abs) - Padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel) - X, Y, Wheel
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

esp_hid_raw_report_map_t s_ble_report_maps[] = {
    {
        .data = kCompositeHidReportMap,
        .len = sizeof(kCompositeHidReportMap),
    },
};

esp_hid_device_config_t s_ble_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "Xiaozhi-HID",
    .manufacturer_name = "XiaoZhi",
    .serial_number = "XZ-HID-001",
    .report_maps = s_ble_report_maps,
    .report_maps_len = 1,
};

ble_uuid16_t s_hid_adv_uuids[] = {
    BLE_UUID16_INIT(kGattSvcHidUuid16),
};

}  // namespace

BleHidController& BleHidController::GetInstance() {
    static BleHidController instance;
    return instance;
}

esp_err_t BleHidController::Initialize(const char* device_name) {
    if (initialized_) {
        return ESP_OK;
    }

    if (device_name != nullptr && device_name[0] != '\0') {
        device_name_ = device_name;
    }
    s_ble_hid_config.device_name = device_name_.c_str();

    cmd_queue_ = xQueueCreate(16, sizeof(HidCommand));
    if (cmd_queue_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create HID command queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreateWithCaps(WorkerTask, "ble_hid_worker", 3072, this, 4, &worker_task_,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create HID worker task");
        vQueueDelete(cmd_queue_);
        cmd_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "esp_bt_controller_mem_release returned %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_bt_controller_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_nimble_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure NimBLE security for frictionless iPhone / iOS pairing (Just Works + Bonding + SC)
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    esp_hidd_dev_t* hid_dev = nullptr;
    ret = esp_hidd_dev_init(&s_ble_hid_config, ESP_HID_TRANSPORT_BLE, HiddEventCallback, &hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    hid_dev_ = hid_dev;

    ble_svc_gap_device_name_set(device_name_.c_str());
    ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD);

    ble_store_config_init();

    ret = esp_nimble_enable(reinterpret_cast<void*>(NimbleHostTask));
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_nimble_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(kTag, "BLE HID initialized as '%s' (Keyboard + Mouse composite)", device_name_.c_str());
    return ESP_OK;
}

const char* BleHidController::GetModeName() const {
    return mode_ == HidMode::kReading ? "reading" : "video";
}

void BleHidController::SetMode(HidMode mode) {
    mode_ = mode;
    ESP_LOGI(kTag, "HID mode set to %s", GetModeName());
}

HidMode BleHidController::ToggleMode() {
    mode_ = (mode_ == HidMode::kReading) ? HidMode::kVideo : HidMode::kReading;
    ESP_LOGI(kTag, "HID mode toggled to %s", GetModeName());
    return mode_;
}

bool BleHidController::Next(int count) {
    count = std::clamp(count, 1, 10);
    if (mode_ == HidMode::kVideo) {
        return SwipeUp(count);
    }
    return SendKey(kKeyRightArrow, 0, count);
}

bool BleHidController::Prev(int count) {
    count = std::clamp(count, 1, 10);
    if (mode_ == HidMode::kVideo) {
        return SwipeDown(count);
    }
    return SendKey(kKeyLeftArrow, 0, count);
}

bool BleHidController::SendKey(uint8_t keycode, uint8_t modifier, int count) {
    count = std::clamp(count, 1, 10);
    const char* label = "按键";
    if (keycode == kKeyRightArrow || keycode == kKeyPageDown) {
        label = "下一页";
    } else if (keycode == kKeyLeftArrow || keycode == kKeyPageUp) {
        label = "上一页";
    } else if (keycode == kKeySpace) {
        label = "空格";
    } else if (keycode == kKeyUpArrow) {
        label = "上方向键";
    } else if (keycode == kKeyDownArrow) {
        label = "下方向键";
    } else if (keycode == kKeyEnter) {
        label = "回车";
    } else if (keycode == kKeyEscape) {
        label = "返回";
    }
    return EnqueueCommand({CommandType::kKeyPress, keycode, modifier, static_cast<uint8_t>(count), label});
}

bool BleHidController::SendNamedKey(const std::string& key_name, int count) {
    if (key_name == "right" || key_name == "next") {
        return SendKey(kKeyRightArrow, 0, count);
    }
    if (key_name == "left" || key_name == "prev" || key_name == "previous") {
        return SendKey(kKeyLeftArrow, 0, count);
    }
    if (key_name == "page_down" || key_name == "pagedown") {
        return SendKey(kKeyPageDown, 0, count);
    }
    if (key_name == "page_up" || key_name == "pageup") {
        return SendKey(kKeyPageUp, 0, count);
    }
    if (key_name == "down") {
        return SendKey(kKeyDownArrow, 0, count);
    }
    if (key_name == "up") {
        return SendKey(kKeyUpArrow, 0, count);
    }
    if (key_name == "space") {
        return SendKey(kKeySpace, 0, count);
    }
    if (key_name == "enter") {
        return SendKey(kKeyEnter, 0, count);
    }
    if (key_name == "escape" || key_name == "esc") {
        return SendKey(kKeyEscape, 0, count);
    }
    ESP_LOGW(kTag, "Unknown key name: %s", key_name.c_str());
    return false;
}

bool BleHidController::SwipeUp(int count) {
    count = std::clamp(count, 1, 10);
    return EnqueueCommand({CommandType::kSwipeUp, 0, 0, static_cast<uint8_t>(count), "下一个视频"});
}

bool BleHidController::SwipeDown(int count) {
    count = std::clamp(count, 1, 10);
    return EnqueueCommand({CommandType::kSwipeDown, 0, 0, static_cast<uint8_t>(count), "上一个视频"});
}

bool BleHidController::SwipeLeft(int count) {
    count = std::clamp(count, 1, 10);
    return EnqueueCommand({CommandType::kSwipeLeft, 0, 0, static_cast<uint8_t>(count), "快退"});
}

bool BleHidController::SwipeRight(int count) {
    count = std::clamp(count, 1, 10);
    return EnqueueCommand({CommandType::kSwipeRight, 0, 0, static_cast<uint8_t>(count), "快进"});
}

bool BleHidController::Tap() {
    return EnqueueCommand({CommandType::kTap, 0, 0, 1, "单击"});
}

bool BleHidController::DoubleTap() {
    return EnqueueCommand({CommandType::kDoubleTap, 0, 0, 1, "点赞"});
}

bool BleHidController::MoveCursor(int8_t dx, int8_t dy, int steps) {
    steps = std::clamp(steps, 1, 20);
    return EnqueueCommand({CommandType::kMoveCursor, static_cast<uint8_t>(dx),
                           static_cast<uint8_t>(dy), static_cast<uint8_t>(steps), "移动光标"});
}

bool BleHidController::ClearBonds() {
    return EnqueueCommand({CommandType::kClearBonds, 0, 0, 1, "清除配对"});
}

bool BleHidController::EnqueueCommand(const HidCommand& cmd) {
    if (!initialized_ || cmd_queue_ == nullptr) {
        return false;
    }
    if (cmd.type != CommandType::kClearBonds && !connected_) {
        ESP_LOGW(kTag, "BLE HID not connected; ignoring command '%s'", cmd.label);
        NotifyAction(cmd.label, false);
        return false;
    }
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(kTag, "HID command queue full; dropping '%s'", cmd.label);
        return false;
    }
    return true;
}

void BleHidController::WorkerTask(void* arg) {
    auto* self = static_cast<BleHidController*>(arg);
    HidCommand cmd{};
    while (true) {
        if (xQueueReceive(self->cmd_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            self->ProcessCommand(cmd);
        }
    }
}

void BleHidController::ProcessCommand(const HidCommand& cmd) {
    if (cmd.type == CommandType::kClearBonds) {
        ExecuteClearBonds();
        NotifyAction(cmd.label, true);
        return;
    }

    if (!connected_ || hid_dev_ == nullptr) {
        NotifyAction(cmd.label, false);
        return;
    }

    const int repeat = std::max<int>(1, cmd.count);
    for (int i = 0; i < repeat; ++i) {
        if (!connected_) {
            NotifyAction(cmd.label, false);
            return;
        }
        switch (cmd.type) {
            case CommandType::kKeyPress:
                ExecuteKeyTap(cmd.keycode, cmd.modifier);
                break;
            case CommandType::kSwipeUp:
                ExecuteSwipe(0, -20, 12);
                break;
            case CommandType::kSwipeDown:
                ExecuteSwipe(0, 20, 12);
                break;
            case CommandType::kSwipeLeft:
                ExecuteSwipe(-16, 0, 10);
                break;
            case CommandType::kSwipeRight:
                ExecuteSwipe(16, 0, 10);
                break;
            case CommandType::kTap:
                ExecuteTap();
                break;
            case CommandType::kDoubleTap:
                ExecuteDoubleTap();
                break;
            case CommandType::kMoveCursor: {
                int8_t dx = static_cast<int8_t>(cmd.keycode);
                int8_t dy = static_cast<int8_t>(cmd.modifier);
                SendMouseReport(0x00, dx, dy, 0);
                vTaskDelay(pdMS_TO_TICKS(16));
                break;
            }
            case CommandType::kClearBonds:
                break;
        }
        if (i + 1 < repeat) {
            vTaskDelay(pdMS_TO_TICKS(180));
        }
    }
    NotifyAction(cmd.label, true);
}

void BleHidController::SendKeyboardReport(uint8_t modifier, uint8_t keycode) {
    if (hid_dev_ == nullptr || !connected_) {
        return;
    }
    uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
    esp_err_t ret = esp_hidd_dev_input_set(static_cast<esp_hidd_dev_t*>(hid_dev_), 0,
                                           kReportIdKeyboard, report, sizeof(report));
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Keyboard report failed: %s", esp_err_to_name(ret));
    }
}

void BleHidController::SendMouseReport(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    if (hid_dev_ == nullptr || !connected_) {
        return;
    }
    uint8_t report[4] = {
        buttons,
        static_cast<uint8_t>(dx),
        static_cast<uint8_t>(dy),
        static_cast<uint8_t>(wheel),
    };
    esp_err_t ret = esp_hidd_dev_input_set(static_cast<esp_hidd_dev_t*>(hid_dev_), 0,
                                           kReportIdMouse, report, sizeof(report));
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "Mouse report failed: %s", esp_err_to_name(ret));
    }
}

void BleHidController::ExecuteKeyTap(uint8_t keycode, uint8_t modifier) {
    SendKeyboardReport(modifier, keycode);
    vTaskDelay(pdMS_TO_TICKS(35));
    SendKeyboardReport(0, 0);
}

void BleHidController::ExecuteSwipe(int8_t step_dx, int8_t step_dy, int steps) {
    // 1. Wake up auto-hidden iOS AssistiveTouch cursor in-place before pressing.
    SendMouseReport(0x00, 1, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(16));
    SendMouseReport(0x00, -1, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(40));

    // 2. Press left mouse button and hold briefly so iOS commits touch-down before dragging.
    SendMouseReport(0x01, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(40));

    // 3. Drag smoothly along (step_dx, step_dy).
    for (int i = 0; i < steps; ++i) {
        SendMouseReport(0x01, step_dx, step_dy, 0);
        vTaskDelay(pdMS_TO_TICKS(14));
    }

    // 4. Release button
    SendMouseReport(0x00, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(40));

    // 5. Return pointer at the exact same speed/step count so iOS acceleration is symmetric
    //    and the cursor lands right back at its original center point.
    for (int i = 0; i < steps; ++i) {
        SendMouseReport(0x00, static_cast<int8_t>(-step_dx), static_cast<int8_t>(-step_dy), 0);
        vTaskDelay(pdMS_TO_TICKS(14));
    }
}

void BleHidController::ExecuteTap() {
    SendMouseReport(0x01, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(35));
    SendMouseReport(0x00, 0, 0, 0);
}

void BleHidController::ExecuteDoubleTap() {
    ExecuteTap();
    vTaskDelay(pdMS_TO_TICKS(75));
    ExecuteTap();
}

void BleHidController::ExecuteClearBonds() {
    ESP_LOGI(kTag, "Clearing all BLE bonds");
    if (connected_ && conn_handle_ != 0xFFFF) {
        ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    ble_store_clear();
    connected_ = false;
    conn_handle_ = 0xFFFF;
    StartAdvertising();
}

void BleHidController::StartAdvertising() {
    if (ble_gap_adv_active()) {
        advertising_ = true;
        NotifyStateChanged();
        return;
    }

    uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGW(kTag, "ble_hs_id_infer_auto failed (rc=%d), falling back to PUBLIC", rc);
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    // Primary ADV_IND: Flags + Keyboard Appearance + HID Service UUID (0x1812) + Complete Name
    struct ble_hs_adv_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    fields.appearance_is_present = 1;
    fields.uuids16 = s_hid_adv_uuids;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.name = reinterpret_cast<const uint8_t*>(device_name_.c_str());
    fields.name_len = static_cast<uint8_t>(device_name_.size());
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_gap_adv_set_fields failed: rc=%d", rc);
        advertising_ = false;
        NotifyStateChanged();
        return;
    }

    // Active Scan Response (SCAN_RSP): Complete Name + TX Power so iOS active scan stays stable
    struct ble_hs_adv_fields rsp_fields;
    std::memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = reinterpret_cast<const uint8_t*>(device_name_.c_str());
    rsp_fields.name_len = static_cast<uint8_t>(device_name_.size());
    rsp_fields.name_is_complete = 1;
    rsp_fields.tx_pwr_lvl_is_present = 1;
    rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGW(kTag, "ble_gap_adv_rsp_set_fields failed: rc=%d", rc);
    }

    struct ble_gap_adv_params adv_params;
    std::memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // Apple Accessory Design Guidelines recommend 20ms interval for reliable iOS discovery
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(20);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(30);

    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &adv_params,
                           GapEventCallback, this);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_gap_adv_start failed: rc=%d", rc);
        advertising_ = false;
    } else {
        advertising_ = true;
        ESP_LOGI(kTag, "BLE HID advertising started ('%s', addr_type=%u)", device_name_.c_str(),
                 own_addr_type);
    }
    NotifyStateChanged();
}

void BleHidController::NotifyStateChanged() {
    if (state_cb_) {
        state_cb_(connected_, advertising_);
    }
}

void BleHidController::NotifyAction(const char* label, bool sent) {
    if (action_cb_) {
        action_cb_(label, sent);
    }
}

void BleHidController::NimbleHostTask(void* param) {
    ESP_LOGI(kTag, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleHidController::HiddEventCallback(void* handler_args, esp_event_base_t base, int32_t id,
                                         void* event_data) {
    auto& self = GetInstance();
    auto event = static_cast<esp_hidd_event_t>(id);
    switch (event) {
        case ESP_HIDD_START_EVENT:
            ESP_LOGI(kTag, "ESP_HIDD_START_EVENT");
            self.StartAdvertising();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            ESP_LOGI(kTag, "ESP_HIDD_CONNECT_EVENT (waiting for encryption/bonding)");
            self.advertising_ = false;
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            ESP_LOGI(kTag, "ESP_HIDD_DISCONNECT_EVENT");
            self.connected_ = false;
            self.conn_handle_ = 0xFFFF;
            self.NotifyStateChanged();
            break;
        case ESP_HIDD_PROTOCOL_MODE_EVENT:
        case ESP_HIDD_CONTROL_EVENT:
        case ESP_HIDD_OUTPUT_EVENT:
        case ESP_HIDD_FEATURE_EVENT:
        case ESP_HIDD_STOP_EVENT:
        default:
            break;
    }
}

int BleHidController::GapEventCallback(struct ble_gap_event* event, void* arg) {
    auto* self = static_cast<BleHidController*>(arg);
    struct ble_gap_conn_desc desc;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                self->conn_handle_ = event->connect.conn_handle;
                self->advertising_ = false;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0 &&
                    desc.sec_state.encrypted) {
                    ESP_LOGI(kTag, "BLE GAP connected (handle=%u, already encrypted)",
                             self->conn_handle_);
                    self->connected_ = true;
                    self->NotifyStateChanged();
                } else {
                    ESP_LOGI(kTag, "BLE GAP connected (handle=%u), initiating security/pairing",
                             self->conn_handle_);
                    int sec_rc = ble_gap_security_initiate(event->connect.conn_handle);
                    if (sec_rc != 0) {
                        ESP_LOGW(kTag, "ble_gap_security_initiate returned rc=%d", sec_rc);
                    }
                }
                // Request iOS-friendly connection parameters for responsive HID + Wi-Fi coexist
                struct ble_gap_upd_params params = {
                    .itvl_min = 12,   // 15ms
                    .itvl_max = 24,   // 30ms
                    .latency = 0,
                    .supervision_timeout = 400,  // 4s
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                ble_gap_update_params(event->connect.conn_handle, &params);
            } else {
                ESP_LOGW(kTag, "BLE GAP connect failed (status=%d), restarting advertising",
                         event->connect.status);
                self->StartAdvertising();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kTag, "BLE GAP disconnected (reason=%d)", event->disconnect.reason);
            self->connected_ = false;
            self->conn_handle_ = 0xFFFF;
            self->StartAdvertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(kTag, "BLE GAP adv complete (reason=%d)", event->adv_complete.reason);
            self->advertising_ = false;
            if (!self->connected_) {
                self->StartAdvertising();
            }
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(kTag, "BLE GAP encryption changed (status=%d)", event->enc_change.status);
            if (event->enc_change.status == 0) {
                self->connected_ = true;
                self->NotifyStateChanged();
            } else {
                ESP_LOGW(kTag, "Encryption/pairing failed (status=%d), disconnecting",
                         event->enc_change.status);
                self->connected_ = false;
                if (self->conn_handle_ != 0xFFFF) {
                    ble_gap_terminate(self->conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
                }
            }
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;

        default:
            return 0;
    }
}
