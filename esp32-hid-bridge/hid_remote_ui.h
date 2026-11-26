#pragma once

#include <functional>

#include <lvgl.h>

#include "ble_hid_controller.h"

class HidRemoteUi {
public:
    using ActionCallback = std::function<void()>;
    using IntAdjustCallback = std::function<int(int delta)>;
    using IntGetterCallback = std::function<int()>;

    void Initialize(lv_obj_t* parent);

    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const;

    void ShowSettings();
    void HideSettings();

    void UpdateBleState(bool connected, bool advertising);
    void UpdateMode(HidMode mode);
    void ShowFeedback(const char* action_label, bool sent);

    void SetPrevHandler(ActionCallback cb) { on_prev_ = std::move(cb); }
    void SetNextHandler(ActionCallback cb) { on_next_ = std::move(cb); }
    void SetAuxHandler(ActionCallback cb) { on_aux_ = std::move(cb); }
    void SetModeToggleHandler(ActionCallback cb) { on_mode_toggle_ = std::move(cb); }
    void SetClearBondsHandler(ActionCallback cb) { on_clear_bonds_ = std::move(cb); }
    void SetWifiConfigHandler(ActionCallback cb) { on_wifi_config_ = std::move(cb); }
    void SetVolumeAdjustHandler(IntAdjustCallback cb) { on_volume_adjust_ = std::move(cb); }
    void SetVolumeGetter(IntGetterCallback cb) { get_volume_ = std::move(cb); }
    void SetBrightnessAdjustHandler(IntAdjustCallback cb) { on_brightness_adjust_ = std::move(cb); }
    void SetBrightnessGetter(IntGetterCallback cb) { get_brightness_ = std::move(cb); }
    void SetInteractionHandler(ActionCallback cb) { on_interaction_ = std::move(cb); }

private:
    static void OnOpenClicked(lv_event_t* e);
    static void OnBackClicked(lv_event_t* e);
    static void OnPrevClicked(lv_event_t* e);
    static void OnNextClicked(lv_event_t* e);
    static void OnAuxClicked(lv_event_t* e);
    static void OnModeClicked(lv_event_t* e);
    static void OnSettingsClicked(lv_event_t* e);
    static void OnSettingsBackClicked(lv_event_t* e);
    static void OnVolumeDownClicked(lv_event_t* e);
    static void OnVolumeUpClicked(lv_event_t* e);
    static void OnBrightnessDownClicked(lv_event_t* e);
    static void OnBrightnessUpClicked(lv_event_t* e);
    static void OnRepairBleClicked(lv_event_t* e);
    static void OnWifiConfigClicked(lv_event_t* e);
    static void OnRootGesture(lv_event_t* e);

    void BuildRemotePage();
    void BuildSettingsPage();
    void RefreshModeStyles();
    void RefreshSettingsValues();
    void ResetWifiConfirm();
    void NotifyInteraction();

    lv_obj_t* open_btn_ = nullptr;
    lv_obj_t* open_dot_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* remote_page_ = nullptr;
    lv_obj_t* settings_page_ = nullptr;

    // Remote page widgets
    lv_obj_t* status_pill_ = nullptr;
    lv_obj_t* status_dot_ = nullptr;
    lv_obj_t* status_label_ = nullptr;

    lv_obj_t* mode_btn_ = nullptr;
    lv_obj_t* mode_label_ = nullptr;

    lv_obj_t* prev_btn_ = nullptr;
    lv_obj_t* prev_label_ = nullptr;

    lv_obj_t* next_btn_ = nullptr;
    lv_obj_t* next_label_ = nullptr;

    lv_obj_t* feedback_label_ = nullptr;

    lv_obj_t* aux_btn_ = nullptr;
    lv_obj_t* aux_label_ = nullptr;
    lv_obj_t* settings_btn_ = nullptr;
    lv_obj_t* back_btn_ = nullptr;

    // Settings page widgets
    lv_obj_t* vol_label_ = nullptr;
    lv_obj_t* bright_label_ = nullptr;
    lv_obj_t* wifi_btn_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;
    lv_obj_t* settings_feedback_ = nullptr;

    HidMode mode_ = HidMode::kVideo;
    bool connected_ = false;
    bool advertising_ = false;
    bool wifi_confirm_pending_ = false;

    ActionCallback on_prev_;
    ActionCallback on_next_;
    ActionCallback on_aux_;
    ActionCallback on_mode_toggle_;
    ActionCallback on_clear_bonds_;
    ActionCallback on_wifi_config_;
    IntAdjustCallback on_volume_adjust_;
    IntGetterCallback get_volume_;
    IntAdjustCallback on_brightness_adjust_;
    IntGetterCallback get_brightness_;
    ActionCallback on_interaction_;
};
