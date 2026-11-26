#pragma once

#include <cstdint>
#include <functional>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ble_hid_controller.h"

// Lightweight LAN UDP control server for Xiaozhi-HID.
// Allows external LAN controllers (e.g. Dangbei H3 Android TV box) to
// auto-discover the ESP32-S3 and trigger BLE HID actions with <5ms latency.
class LanHidServer {
public:
    static constexpr uint16_t kDefaultPort = 4210;
    using ModeChangedCallback = std::function<void(HidMode mode)>;

    static LanHidServer& GetInstance();

    esp_err_t Start(uint16_t port = kDefaultPort);
    void SetModeChangedCallback(ModeChangedCallback cb) { mode_cb_ = std::move(cb); }
    bool IsRunning() const { return running_; }

private:
    LanHidServer() = default;

    static void ServerTask(void* arg);
    void RunLoop();
    void HandlePacket(int sock, const char* data, int len, const void* src_addr, int addr_len);
    void BuildStatusJson(char* out, size_t out_size, const char* action, bool ok,
                         bool include_action) const;

    uint16_t port_ = kDefaultPort;
    volatile bool running_ = false;
    TaskHandle_t task_handle_ = nullptr;
    ModeChangedCallback mode_cb_;
};
