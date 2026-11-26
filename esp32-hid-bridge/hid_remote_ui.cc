#include "hid_remote_ui.h"

#include <cstdio>

namespace {

constexpr uint32_t kColorBg = 0x000000;
constexpr uint32_t kColorSurface = 0x141B26;
constexpr uint32_t kColorSurfaceAlt = 0x1B2433;
constexpr uint32_t kColorSurfacePressed = 0x263449;
constexpr uint32_t kColorBorder = 0x2E3C52;
constexpr uint32_t kColorText = 0xF8FAFC;
constexpr uint32_t kColorMuted = 0x94A3B8;
constexpr uint32_t kColorGreen = 0x22C55E;
constexpr uint32_t kColorAmber = 0xF59E0B;
constexpr uint32_t kColorRed = 0xEF4444;

constexpr uint32_t kColorReadingBg = 0x2563EB;
constexpr uint32_t kColorReadingPressed = 0x1D4ED8;
constexpr uint32_t kColorReadingBorder = 0x60A5FA;
constexpr uint32_t kColorReadingTint = 0x132240;

constexpr uint32_t kColorVideoBg = 0xDB2777;
constexpr uint32_t kColorVideoPressed = 0xBE185D;
constexpr uint32_t kColorVideoBorder = 0xF472B6;
constexpr uint32_t kColorVideoTint = 0x3A1328;

// Optical centering for the 30px Noto Sans CJK font inside zero-padded buttons.
lv_obj_t* CreateCenteredLabel(lv_obj_t* parent, const char* text, uint32_t color_hex,
                              int32_t x_ofs = 0, int32_t y_ofs = 0) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color_hex), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x_ofs, y_ofs);
    return label;
}

void StyleButton(lv_obj_t* btn, uint32_t bg_hex, uint32_t pressed_hex, uint32_t border_hex,
                 int32_t radius, int32_t border_width = 2) {
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_hex), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(btn, border_width, 0);
    lv_obj_set_style_radius(btn, radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

lv_obj_t* CreateFullPageContainer(lv_obj_t* parent) {
    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_center(page);
    lv_obj_set_style_bg_color(page, lv_color_hex(kColorBg), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_radius(page, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

}  // namespace

void HidRemoteUi::Initialize(lv_obj_t* parent) {
    if (parent == nullptr || root_ != nullptr) {
        return;
    }

    // Floating entry pill at bottom of the XiaoZhi screen (safely inside circular bezel)
    open_btn_ = lv_button_create(parent);
    lv_obj_set_size(open_btn_, 168, 52);
    lv_obj_align(open_btn_, LV_ALIGN_BOTTOM_MID, 0, -28);
    StyleButton(open_btn_, kColorSurface, kColorSurfacePressed, kColorReadingBorder,
                LV_RADIUS_CIRCLE, 2);
    lv_obj_set_style_bg_opa(open_btn_, LV_OPA_90, 0);
    lv_obj_add_event_cb(open_btn_, OnOpenClicked, LV_EVENT_CLICKED, this);

    open_dot_ = lv_obj_create(open_btn_);
    lv_obj_set_size(open_dot_, 12, 12);
    lv_obj_set_style_radius(open_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(open_dot_, 0, 0);
    lv_obj_set_style_pad_all(open_dot_, 0, 0);
    lv_obj_set_style_bg_color(open_dot_, lv_color_hex(kColorAmber), 0);
    lv_obj_align(open_dot_, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_clear_flag(open_dot_, LV_OBJ_FLAG_CLICKABLE);

    CreateCenteredLabel(open_btn_, "遥控器", kColorText, 12, 0);

    // Full-screen circular AMOLED overlay
    root_ = CreateFullPageContainer(parent);
    lv_obj_add_event_cb(root_, OnRootGesture, LV_EVENT_GESTURE, this);

    BuildRemotePage();
    BuildSettingsPage();

    lv_obj_add_flag(settings_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    RefreshModeStyles();
    UpdateBleState(connected_, advertising_);
}

void HidRemoteUi::BuildRemotePage() {
    remote_page_ = CreateFullPageContainer(root_);
    lv_obj_add_flag(remote_page_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Top BLE status pill
    status_pill_ = lv_obj_create(remote_page_);
    lv_obj_set_size(status_pill_, 176, 44);
    lv_obj_align(status_pill_, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_pad_all(status_pill_, 0, 0);
    lv_obj_set_style_bg_color(status_pill_, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_bg_opa(status_pill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_pill_, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(status_pill_, 1, 0);
    lv_obj_set_style_radius(status_pill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(status_pill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(status_pill_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    status_dot_ = lv_obj_create(status_pill_);
    lv_obj_set_size(status_dot_, 12, 12);
    lv_obj_set_style_radius(status_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(status_dot_, 0, 0);
    lv_obj_set_style_pad_all(status_dot_, 0, 0);
    lv_obj_set_style_bg_color(status_dot_, lv_color_hex(kColorAmber), 0);
    lv_obj_align(status_dot_, LV_ALIGN_LEFT_MID, 22, 0);

    status_label_ = CreateCenteredLabel(status_pill_, "待配对", kColorText, 12, 0);

    // Mode switch pill (Reading <-> Video)
    mode_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(mode_btn_, 214, 52);
    lv_obj_align(mode_btn_, LV_ALIGN_TOP_MID, 0, 80);
    StyleButton(mode_btn_, kColorReadingTint, kColorSurfacePressed, kColorReadingBorder,
                LV_RADIUS_CIRCLE, 2);
    lv_obj_add_event_cb(mode_btn_, OnModeClicked, LV_EVENT_CLICKED, this);
    mode_label_ = CreateCenteredLabel(mode_btn_, "阅读模式", kColorText);

    // Center large Prev / Next cards (widest safe zone of the 466px circle)
    prev_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(prev_btn_, 174, 146);
    lv_obj_align(prev_btn_, LV_ALIGN_CENTER, -96, -8);
    StyleButton(prev_btn_, kColorSurface, kColorSurfacePressed, kColorBorder, 32, 2);
    lv_obj_add_event_cb(prev_btn_, OnPrevClicked, LV_EVENT_CLICKED, this);
    prev_label_ = CreateCenteredLabel(prev_btn_, "上一页", kColorText);

    next_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(next_btn_, 174, 146);
    lv_obj_align(next_btn_, LV_ALIGN_CENTER, 96, -8);
    StyleButton(next_btn_, kColorReadingBg, kColorReadingPressed, kColorReadingBorder, 32, 2);
    lv_obj_add_event_cb(next_btn_, OnNextClicked, LV_EVENT_CLICKED, this);
    next_label_ = CreateCenteredLabel(next_btn_, "下一页", kColorText);

    // Action feedback line
    feedback_label_ = lv_label_create(remote_page_);
    lv_label_set_text(feedback_label_, "点按或滑动翻页");
    lv_obj_set_style_text_color(feedback_label_, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_text_align(feedback_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(feedback_label_, LV_ALIGN_CENTER, 0, 90);

    // Bottom 3-pill utility bar: Aux (Space/Like) | Settings | XiaoZhi
    aux_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(aux_btn_, 102, 52);
    lv_obj_align(aux_btn_, LV_ALIGN_BOTTOM_MID, -112, -44);
    StyleButton(aux_btn_, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(aux_btn_, OnAuxClicked, LV_EVENT_CLICKED, this);
    aux_label_ = CreateCenteredLabel(aux_btn_, "空格", kColorText);

    settings_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(settings_btn_, 102, 52);
    lv_obj_align(settings_btn_, LV_ALIGN_BOTTOM_MID, 0, -34);
    StyleButton(settings_btn_, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder,
                LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(settings_btn_, OnSettingsClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(settings_btn_, "设置", kColorText);

    back_btn_ = lv_button_create(remote_page_);
    lv_obj_set_size(back_btn_, 102, 52);
    lv_obj_align(back_btn_, LV_ALIGN_BOTTOM_MID, 112, -44);
    StyleButton(back_btn_, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(back_btn_, OnBackClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(back_btn_, "小智", kColorText);
}

void HidRemoteUi::BuildSettingsPage() {
    settings_page_ = CreateFullPageContainer(root_);

    // Title
    lv_obj_t* title = lv_label_create(settings_page_);
    lv_label_set_text(title, "设备设置");
    lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    // Row 1: Volume control
    lv_obj_t* vol_row = lv_obj_create(settings_page_);
    lv_obj_set_size(vol_row, 336, 58);
    lv_obj_align(vol_row, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_set_style_bg_color(vol_row, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_border_color(vol_row, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(vol_row, 1, 0);
    lv_obj_set_style_radius(vol_row, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* vol_down = lv_button_create(vol_row);
    lv_obj_set_size(vol_down, 72, 48);
    lv_obj_align(vol_down, LV_ALIGN_LEFT_MID, 6, 0);
    StyleButton(vol_down, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(vol_down, OnVolumeDownClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(vol_down, "-", kColorText);

    vol_label_ = CreateCenteredLabel(vol_row, "音量 70%", kColorText);

    lv_obj_t* vol_up = lv_button_create(vol_row);
    lv_obj_set_size(vol_up, 72, 48);
    lv_obj_align(vol_up, LV_ALIGN_RIGHT_MID, -6, 0);
    StyleButton(vol_up, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(vol_up, OnVolumeUpClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(vol_up, "+", kColorText);

    // Row 2: Brightness control
    lv_obj_t* bright_row = lv_obj_create(settings_page_);
    lv_obj_set_size(bright_row, 336, 58);
    lv_obj_align(bright_row, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_set_style_pad_all(bright_row, 0, 0);
    lv_obj_set_style_bg_color(bright_row, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_border_color(bright_row, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(bright_row, 1, 0);
    lv_obj_set_style_radius(bright_row, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(bright_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bright_down = lv_button_create(bright_row);
    lv_obj_set_size(bright_down, 72, 48);
    lv_obj_align(bright_down, LV_ALIGN_LEFT_MID, 6, 0);
    StyleButton(bright_down, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE,
                1);
    lv_obj_add_event_cb(bright_down, OnBrightnessDownClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(bright_down, "-", kColorText);

    bright_label_ = CreateCenteredLabel(bright_row, "亮度 50%", kColorText);

    lv_obj_t* bright_up = lv_button_create(bright_row);
    lv_obj_set_size(bright_up, 72, 48);
    lv_obj_align(bright_up, LV_ALIGN_RIGHT_MID, -6, 0);
    StyleButton(bright_up, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE,
                1);
    lv_obj_add_event_cb(bright_up, OnBrightnessUpClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(bright_up, "+", kColorText);

    // Row 3: Re-pair BLE & Re-config Wi-Fi
    lv_obj_t* ble_btn = lv_button_create(settings_page_);
    lv_obj_set_size(ble_btn, 162, 56);
    lv_obj_align(ble_btn, LV_ALIGN_TOP_MID, -88, 242);
    StyleButton(ble_btn, 0x2B2110, 0x423216, kColorAmber, LV_RADIUS_CIRCLE, 2);
    lv_obj_add_event_cb(ble_btn, OnRepairBleClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(ble_btn, "重配蓝牙", kColorText);

    wifi_btn_ = lv_button_create(settings_page_);
    lv_obj_set_size(wifi_btn_, 162, 56);
    lv_obj_align(wifi_btn_, LV_ALIGN_TOP_MID, 88, 242);
    StyleButton(wifi_btn_, 0x2D1318, 0x451A22, kColorRed, LV_RADIUS_CIRCLE, 2);
    lv_obj_add_event_cb(wifi_btn_, OnWifiConfigClicked, LV_EVENT_CLICKED, this);
    wifi_label_ = CreateCenteredLabel(wifi_btn_, "重配WiFi", kColorText);

    // Feedback line
    settings_feedback_ = lv_label_create(settings_page_);
    lv_label_set_text(settings_feedback_, "蓝牙绑定与系统设置");
    lv_obj_set_style_text_color(settings_feedback_, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_text_align(settings_feedback_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(settings_feedback_, LV_ALIGN_TOP_MID, 0, 314);

    // Bottom Return pill
    lv_obj_t* ret_btn = lv_button_create(settings_page_);
    lv_obj_set_size(ret_btn, 152, 52);
    lv_obj_align(ret_btn, LV_ALIGN_BOTTOM_MID, 0, -34);
    StyleButton(ret_btn, kColorSurfaceAlt, kColorSurfacePressed, kColorBorder, LV_RADIUS_CIRCLE, 1);
    lv_obj_add_event_cb(ret_btn, OnSettingsBackClicked, LV_EVENT_CLICKED, this);
    CreateCenteredLabel(ret_btn, "返回", kColorText);
}

void HidRemoteUi::Show() {
    if (root_ == nullptr) {
        return;
    }
    HideSettings();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    if (open_btn_ != nullptr) {
        lv_obj_add_flag(open_btn_, LV_OBJ_FLAG_HIDDEN);
    }
    NotifyInteraction();
}

void HidRemoteUi::Hide() {
    if (root_ == nullptr) {
        return;
    }
    HideSettings();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (open_btn_ != nullptr) {
        lv_obj_remove_flag(open_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(open_btn_);
    }
    NotifyInteraction();
}

void HidRemoteUi::Toggle() {
    if (IsVisible()) {
        Hide();
    } else {
        Show();
    }
}

bool HidRemoteUi::IsVisible() const {
    return root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void HidRemoteUi::ShowSettings() {
    if (settings_page_ == nullptr || remote_page_ == nullptr) {
        return;
    }
    ResetWifiConfirm();
    RefreshSettingsValues();
    if (settings_feedback_ != nullptr) {
        lv_label_set_text(settings_feedback_, "蓝牙绑定与系统设置");
        lv_obj_set_style_text_color(settings_feedback_, lv_color_hex(kColorMuted), 0);
    }
    lv_obj_add_flag(remote_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_page_, LV_OBJ_FLAG_HIDDEN);
    NotifyInteraction();
}

void HidRemoteUi::HideSettings() {
    if (settings_page_ == nullptr || remote_page_ == nullptr) {
        return;
    }
    ResetWifiConfirm();
    lv_obj_add_flag(settings_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(remote_page_, LV_OBJ_FLAG_HIDDEN);
}

void HidRemoteUi::UpdateBleState(bool connected, bool advertising) {
    connected_ = connected;
    advertising_ = advertising;
    const uint32_t dot_color = connected ? kColorGreen : (advertising ? kColorAmber : kColorRed);

    if (open_dot_ != nullptr) {
        lv_obj_set_style_bg_color(open_dot_, lv_color_hex(dot_color), 0);
    }
    if (status_dot_ != nullptr) {
        lv_obj_set_style_bg_color(status_dot_, lv_color_hex(dot_color), 0);
    }
    if (status_label_ != nullptr) {
        lv_label_set_text(status_label_, connected ? "已连接" : (advertising ? "待配对" : "未启动"));
    }
    if (feedback_label_ != nullptr && connected) {
        lv_label_set_text(feedback_label_, "蓝牙已就绪");
        lv_obj_set_style_text_color(feedback_label_, lv_color_hex(kColorGreen), 0);
    }
}

void HidRemoteUi::UpdateMode(HidMode mode) {
    mode_ = mode;
    RefreshModeStyles();
}

void HidRemoteUi::ShowFeedback(const char* action_label, bool sent) {
    if (feedback_label_ == nullptr || action_label == nullptr) {
        return;
    }
    char buf[48];
    if (sent) {
        std::snprintf(buf, sizeof(buf), "已发: %s", action_label);
        lv_obj_set_style_text_color(feedback_label_, lv_color_hex(kColorGreen), 0);
    } else {
        std::snprintf(buf, sizeof(buf), "请先连接蓝牙");
        lv_obj_set_style_text_color(feedback_label_, lv_color_hex(kColorAmber), 0);
    }
    lv_label_set_text(feedback_label_, buf);
}

void HidRemoteUi::RefreshModeStyles() {
    if (root_ == nullptr) {
        return;
    }
    if (mode_ == HidMode::kReading) {
        lv_label_set_text(mode_label_, "阅读模式");
        lv_label_set_text(prev_label_, "上一页");
        lv_label_set_text(next_label_, "下一页");
        lv_label_set_text(aux_label_, "空格");
        lv_obj_set_style_bg_color(mode_btn_, lv_color_hex(kColorReadingTint), 0);
        lv_obj_set_style_border_color(mode_btn_, lv_color_hex(kColorReadingBorder), 0);
        lv_obj_set_style_bg_color(next_btn_, lv_color_hex(kColorReadingBg), 0);
        lv_obj_set_style_bg_color(next_btn_, lv_color_hex(kColorReadingPressed), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(next_btn_, lv_color_hex(kColorReadingBorder), 0);
    } else {
        lv_label_set_text(mode_label_, "视频模式");
        lv_label_set_text(prev_label_, "上一个");
        lv_label_set_text(next_label_, "下一个");
        lv_label_set_text(aux_label_, "点赞");
        lv_obj_set_style_bg_color(mode_btn_, lv_color_hex(kColorVideoTint), 0);
        lv_obj_set_style_border_color(mode_btn_, lv_color_hex(kColorVideoBorder), 0);
        lv_obj_set_style_bg_color(next_btn_, lv_color_hex(kColorVideoBg), 0);
        lv_obj_set_style_bg_color(next_btn_, lv_color_hex(kColorVideoPressed), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(next_btn_, lv_color_hex(kColorVideoBorder), 0);
    }
}

void HidRemoteUi::RefreshSettingsValues() {
    char buf[32];
    if (vol_label_ != nullptr && get_volume_) {
        std::snprintf(buf, sizeof(buf), "音量 %d%%", get_volume_());
        lv_label_set_text(vol_label_, buf);
    }
    if (bright_label_ != nullptr && get_brightness_) {
        std::snprintf(buf, sizeof(buf), "亮度 %d%%", get_brightness_());
        lv_label_set_text(bright_label_, buf);
    }
}

void HidRemoteUi::ResetWifiConfirm() {
    wifi_confirm_pending_ = false;
    if (wifi_label_ != nullptr) {
        lv_label_set_text(wifi_label_, "重配WiFi");
    }
}

void HidRemoteUi::NotifyInteraction() {
    if (on_interaction_) {
        on_interaction_();
    }
}

void HidRemoteUi::OnOpenClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->Show();
    }
}

void HidRemoteUi::OnBackClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->Hide();
    }
}

void HidRemoteUi::OnPrevClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->NotifyInteraction();
        if (self->on_prev_) {
            self->on_prev_();
        }
    }
}

void HidRemoteUi::OnNextClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->NotifyInteraction();
        if (self->on_next_) {
            self->on_next_();
        }
    }
}

void HidRemoteUi::OnAuxClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->NotifyInteraction();
        if (self->on_aux_) {
            self->on_aux_();
        }
    }
}

void HidRemoteUi::OnModeClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->NotifyInteraction();
        if (self->on_mode_toggle_) {
            self->on_mode_toggle_();
        }
    }
}

void HidRemoteUi::OnSettingsClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->ShowSettings();
    }
}

void HidRemoteUi::OnSettingsBackClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->HideSettings();
        self->NotifyInteraction();
    }
}

void HidRemoteUi::OnVolumeDownClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    self->ResetWifiConfirm();
    if (self->on_volume_adjust_) {
        int vol = self->on_volume_adjust_(-10);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "音量 %d%%", vol);
        lv_label_set_text(self->vol_label_, buf);
    }
}

void HidRemoteUi::OnVolumeUpClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    self->ResetWifiConfirm();
    if (self->on_volume_adjust_) {
        int vol = self->on_volume_adjust_(10);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "音量 %d%%", vol);
        lv_label_set_text(self->vol_label_, buf);
    }
}

void HidRemoteUi::OnBrightnessDownClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    self->ResetWifiConfirm();
    if (self->on_brightness_adjust_) {
        int br = self->on_brightness_adjust_(-15);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "亮度 %d%%", br);
        lv_label_set_text(self->bright_label_, buf);
    }
}

void HidRemoteUi::OnBrightnessUpClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    self->ResetWifiConfirm();
    if (self->on_brightness_adjust_) {
        int br = self->on_brightness_adjust_(15);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "亮度 %d%%", br);
        lv_label_set_text(self->bright_label_, buf);
    }
}

void HidRemoteUi::OnRepairBleClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    self->ResetWifiConfirm();
    if (self->on_clear_bonds_) {
        self->on_clear_bonds_();
    }
    if (self->settings_feedback_ != nullptr) {
        lv_label_set_text(self->settings_feedback_, "已清配对,重新广播中");
        lv_obj_set_style_text_color(self->settings_feedback_, lv_color_hex(kColorAmber), 0);
    }
}

void HidRemoteUi::OnWifiConfigClicked(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->NotifyInteraction();
    if (!self->wifi_confirm_pending_) {
        self->wifi_confirm_pending_ = true;
        lv_label_set_text(self->wifi_label_, "再点确认");
        if (self->settings_feedback_ != nullptr) {
            lv_label_set_text(self->settings_feedback_, "再次点击进入配网");
            lv_obj_set_style_text_color(self->settings_feedback_, lv_color_hex(kColorRed), 0);
        }
        return;
    }
    self->ResetWifiConfirm();
    if (self->on_wifi_config_) {
        self->on_wifi_config_();
    }
}

void HidRemoteUi::OnRootGesture(lv_event_t* e) {
    auto* self = static_cast<HidRemoteUi*>(lv_event_get_user_data(e));
    if (self == nullptr || !self->IsVisible()) {
        return;
    }
    if (self->settings_page_ != nullptr && !lv_obj_has_flag(self->settings_page_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    self->NotifyInteraction();
    if (dir == LV_DIR_LEFT || dir == LV_DIR_TOP) {
        if (self->on_next_) {
            self->on_next_();
        }
    } else if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        if (self->on_prev_) {
            self->on_prev_();
        }
    }
}
