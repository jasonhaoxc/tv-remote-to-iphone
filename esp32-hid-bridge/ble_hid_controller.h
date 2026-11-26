#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <esp_err.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

enum class HidMode : uint8_t {
    kReading = 0,  // Keyboard arrows / PageDown / Space for Books, WeChat Read, Kindle, PPT
    kVideo = 1,    // Relative mouse drag / tap for iOS AssistiveTouch short-video apps
};

class BleHidController {
public:
    using StateCallback = std::function<void(bool connected, bool advertising)>;
    using ActionCallback = std::function<void(const char* action_label, bool sent)>;

    static BleHidController& GetInstance();

    esp_err_t Initialize(const char* device_name = "Xiaozhi-HID");

    bool IsInitialized() const { return initialized_; }
    bool IsConnected() const { return connected_; }
    bool IsAdvertising() const { return advertising_; }

    HidMode GetMode() const { return mode_; }
    const char* GetModeName() const;
    void SetMode(HidMode mode);
    HidMode ToggleMode();

    void SetStateCallback(StateCallback callback) { state_cb_ = std::move(callback); }
    void SetActionCallback(ActionCallback callback) { action_cb_ = std::move(callback); }

    // Mode-aware actions (non-blocking, queued to worker task)
    bool Next(int count = 1);
    bool Prev(int count = 1);

    // Keyboard actions (Report ID 1)
    bool SendKey(uint8_t keycode, uint8_t modifier = 0, int count = 1);
    bool SendNamedKey(const std::string& key_name, int count = 1);

    // Mouse / iOS AssistiveTouch actions (Report ID 2)
    bool SwipeUp(int count = 1);
    bool SwipeDown(int count = 1);
    bool Tap();
    bool DoubleTap();
    bool MoveCursor(int8_t dx, int8_t dy, int steps = 1);

    // Pairing management
    bool ClearBonds();

private:
    BleHidController() = default;

    enum class CommandType : uint8_t {
        kKeyPress,
        kSwipeUp,
        kSwipeDown,
        kTap,
        kDoubleTap,
        kMoveCursor,
        kClearBonds,
    };

    struct HidCommand {
        CommandType type;
        uint8_t keycode;
        uint8_t modifier;
        uint8_t count;
        const char* label;
    };

    bool EnqueueCommand(const HidCommand& cmd);
    static void WorkerTask(void* arg);
    void ProcessCommand(const HidCommand& cmd);

    void SendKeyboardReport(uint8_t modifier, uint8_t keycode);
    void SendMouseReport(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel = 0);
    void ExecuteKeyTap(uint8_t keycode, uint8_t modifier);
    void ExecuteSwipe(int8_t step_dy);
    void ExecuteTap();
    void ExecuteDoubleTap();
    void ExecuteClearBonds();

    void StartAdvertising();
    void NotifyStateChanged();
    void NotifyAction(const char* label, bool sent);

    static void NimbleHostTask(void* param);
    static void HiddEventCallback(void* handler_args, esp_event_base_t base, int32_t id,
                                  void* event_data);
    static int GapEventCallback(struct ble_gap_event* event, void* arg);

    bool initialized_ = false;
    volatile bool connected_ = false;
    volatile bool advertising_ = false;
    volatile HidMode mode_ = HidMode::kVideo;

    std::string device_name_ = "Xiaozhi-HID";
    void* hid_dev_ = nullptr;
    uint16_t conn_handle_ = 0xFFFF;

    QueueHandle_t cmd_queue_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;

    StateCallback state_cb_;
    ActionCallback action_cb_;
};
