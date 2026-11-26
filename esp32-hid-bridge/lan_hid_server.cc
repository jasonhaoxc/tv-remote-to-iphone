#include "lan_hid_server.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>
#include <esp_netif.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace {

constexpr char kTag[] = "LanHidServer";

std::string Trim(const char* data, int len) {
    int start = 0;
    while (start < len && std::isspace(static_cast<unsigned char>(data[start]))) {
        start++;
    }
    int end = len;
    while (end > start && std::isspace(static_cast<unsigned char>(data[end - 1]))) {
        end--;
    }
    return std::string(data + start, end - start);
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int ParseCount(const std::string& arg, int default_count = 1) {
    if (arg.empty()) {
        return default_count;
    }
    int val = std::atoi(arg.c_str());
    if (val < 1) {
        return 1;
    }
    if (val > 10) {
        return 10;
    }
    return val;
}

void GetLocalStaIp(char* buf, size_t buf_size) {
    buf[0] = '\0';
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        std::snprintf(buf, buf_size, "0.0.0.0");
        return;
    }
    esp_netif_ip_info_t ip_info{};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        std::snprintf(buf, buf_size, IPSTR, IP2STR(&ip_info.ip));
    } else {
        std::snprintf(buf, buf_size, "0.0.0.0");
    }
}

}  // namespace

LanHidServer& LanHidServer::GetInstance() {
    static LanHidServer instance;
    return instance;
}

esp_err_t LanHidServer::Start(uint16_t port) {
    if (running_) {
        return ESP_OK;
    }
    port_ = port;
    running_ = true;
    BaseType_t ok = xTaskCreate(ServerTask, "lan_hid_udp", 4096, this, 4, &task_handle_);
    if (ok != pdPASS) {
        running_ = false;
        ESP_LOGE(kTag, "Failed to create lan_hid_udp task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "LAN HID UDP server task scheduled on port %u", port_);
    return ESP_OK;
}

void LanHidServer::ServerTask(void* arg) {
    static_cast<LanHidServer*>(arg)->RunLoop();
    vTaskDelete(nullptr);
}

void LanHidServer::RunLoop() {
    // Wait until Wi-Fi STA netif is created and has obtained an IP address
    // (calling lwip_socket before esp_netif_init/lwIP starts causes an assert).
    while (running_) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info{};
        if (netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
            ip_info.ip.addr != 0) {
            ESP_LOGI(kTag, "Wi-Fi STA ready (" IPSTR "), opening UDP socket on port %u",
                     IP2STR(&ip_info.ip), port_);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (running_) {
        int sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        int reuse = 1;
        lwip_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        int broadcast = 1;
        lwip_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        struct timeval timeout {};
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in bind_addr {};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = htons(port_);

        if (lwip_bind(sock, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            lwip_close(sock);
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        ESP_LOGI(kTag, "Listening for LAN HID UDP commands on port %u", port_);

        char rx_buf[256];
        while (running_) {
            struct sockaddr_storage src_addr {};
            socklen_t addr_len = sizeof(src_addr);
            int len = lwip_recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                                    reinterpret_cast<struct sockaddr*>(&src_addr), &addr_len);
            if (len > 0) {
                rx_buf[len] = '\0';
                HandlePacket(sock, rx_buf, len, &src_addr, static_cast<int>(addr_len));
            } else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(kTag, "UDP recvfrom error (errno=%d), reopening socket", errno);
                break;
            }
        }

        lwip_close(sock);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void LanHidServer::BuildStatusJson(char* out, size_t out_size, const char* action, bool ok,
                                   bool include_action) const {
    auto& hid = BleHidController::GetInstance();
    char ip_str[20];
    GetLocalStaIp(ip_str, sizeof(ip_str));

    if (include_action) {
        std::snprintf(
            out, out_size,
            "{\"ok\":%s,\"action\":\"%s\",\"device\":\"Xiaozhi-HID\",\"ip\":\"%s\",\"port\":%u,"
            "\"connected\":%s,\"advertising\":%s,\"mode\":\"%s\"}",
            ok ? "true" : "false", action, ip_str, static_cast<unsigned>(port_),
            hid.IsConnected() ? "true" : "false", hid.IsAdvertising() ? "true" : "false",
            hid.GetModeName());
    } else {
        std::snprintf(
            out, out_size,
            "{\"ok\":true,\"device\":\"Xiaozhi-HID\",\"ip\":\"%s\",\"port\":%u,"
            "\"connected\":%s,\"advertising\":%s,\"mode\":\"%s\"}",
            ip_str, static_cast<unsigned>(port_), hid.IsConnected() ? "true" : "false",
            hid.IsAdvertising() ? "true" : "false", hid.GetModeName());
    }
}

void LanHidServer::HandlePacket(int sock, const char* data, int len, const void* src_addr,
                                int addr_len) {
    std::string raw = Trim(data, len);
    if (raw.empty()) {
        return;
    }

    std::string cmd = raw;
    std::string arg1;
    std::string arg2;
    size_t p1 = raw.find(':');
    if (p1 != std::string::npos) {
        cmd = raw.substr(0, p1);
        std::string rest = raw.substr(p1 + 1);
        size_t p2 = rest.find(':');
        if (p2 != std::string::npos) {
            arg1 = rest.substr(0, p2);
            arg2 = rest.substr(p2 + 1);
        } else {
            arg1 = rest;
        }
    }

    std::string upper_cmd = ToUpper(cmd);
    auto& hid = BleHidController::GetInstance();

    char reply[256];
    if (upper_cmd == "DISCOVER" || upper_cmd == "PING" || upper_cmd == "STATUS") {
        BuildStatusJson(reply, sizeof(reply), "", true, false);
    } else if (upper_cmd == "NEXT") {
        bool ok = hid.Next(ParseCount(arg1));
        BuildStatusJson(reply, sizeof(reply), "next", ok, true);
    } else if (upper_cmd == "PREV") {
        bool ok = hid.Prev(ParseCount(arg1));
        BuildStatusJson(reply, sizeof(reply), "prev", ok, true);
    } else if (upper_cmd == "SWIPE_UP") {
        bool ok = hid.SwipeUp(ParseCount(arg1));
        BuildStatusJson(reply, sizeof(reply), "swipe_up", ok, true);
    } else if (upper_cmd == "SWIPE_DOWN") {
        bool ok = hid.SwipeDown(ParseCount(arg1));
        BuildStatusJson(reply, sizeof(reply), "swipe_down", ok, true);
    } else if (upper_cmd == "TAP") {
        bool ok = hid.Tap();
        BuildStatusJson(reply, sizeof(reply), "tap", ok, true);
    } else if (upper_cmd == "DOUBLE_TAP") {
        bool ok = hid.DoubleTap();
        BuildStatusJson(reply, sizeof(reply), "double_tap", ok, true);
    } else if (upper_cmd == "MOVE" && !arg1.empty()) {
        int dx = std::clamp(std::atoi(arg1.c_str()), -127, 127);
        int dy = std::clamp(arg2.empty() ? 0 : std::atoi(arg2.c_str()), -127, 127);
        bool ok = hid.MoveCursor(static_cast<int8_t>(dx), static_cast<int8_t>(dy), 2);
        BuildStatusJson(reply, sizeof(reply), "move", ok, true);
    } else if (upper_cmd == "MODE") {
        std::string m = ToLower(arg1);
        HidMode mode = (m == "reading") ? HidMode::kReading : HidMode::kVideo;
        hid.SetMode(mode);
        if (mode_cb_) {
            mode_cb_(mode);
        }
        BuildStatusJson(reply, sizeof(reply), "mode", true, true);
    } else if (upper_cmd == "KEY" && !arg1.empty()) {
        bool ok = hid.SendNamedKey(ToLower(arg1), ParseCount(arg2));
        BuildStatusJson(reply, sizeof(reply), "key", ok, true);
    } else {
        std::snprintf(reply, sizeof(reply), "{\"ok\":false,\"error\":\"unknown_command\"}");
    }

    lwip_sendto(sock, reply, std::strlen(reply), 0,
                reinterpret_cast<const struct sockaddr*>(src_addr),
                static_cast<socklen_t>(addr_len));
}
