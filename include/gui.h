#pragma once

#include "common.h"
#include "config.h"
#include <d3d11.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>

namespace catclicker {

// CPU feature detection (defined in frame_grabber.cpp)
bool cpu_supports_avx2();
bool cpu_supports_sse41();


// Key name mapping for display
const std::unordered_map<int, std::string> KEY_NAMES = {
    {0x01, "Mouse 1"}, {0x02, "Mouse 2"}, {0x04, "Mouse 3"}, {0x05, "Mouse 4"}, {0x06, "Mouse 5"},
    {0x08, "Backspace"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x10, "Shift"}, {0x11, "Ctrl"},
    {0x12, "Alt"}, {0x14, "Caps Lock"}, {0x1B, "Escape"}, {0x20, "Space"},
    {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"}, {0x2D, "Insert"}, {0x2E, "Delete"},
    {0x41, "A"}, {0x42, "B"}, {0x43, "C"}, {0x44, "D"}, {0x45, "E"}, {0x46, "F"}, {0x47, "G"},
    {0x48, "H"}, {0x49, "I"}, {0x4A, "J"}, {0x4B, "K"}, {0x4C, "L"}, {0x4D, "M"}, {0x4E, "N"},
    {0x4F, "O"}, {0x50, "P"}, {0x51, "Q"}, {0x52, "R"}, {0x53, "S"}, {0x54, "T"}, {0x55, "U"},
    {0x56, "V"}, {0x57, "W"}, {0x58, "X"}, {0x59, "Y"}, {0x5A, "Z"},
    {0x70, "F1"}, {0x71, "F2"}, {0x72, "F3"}, {0x73, "F4"}, {0x74, "F5"}, {0x75, "F6"},
    {0x76, "F7"}, {0x77, "F8"}, {0x78, "F9"}, {0x79, "F10"}, {0x7A, "F11"}, {0x7B, "F12"},
};

// Smoothing curve names for display
const char* const SMOOTHING_CURVE_NAMES[] = {
    "Linear (None)",
    "Ease Out (Fast->Slow)",
    "Ease In (Slow->Fast)",
    "Ease In-Out",
    "Sigmoid (S-Curve)",
    "Exponential"
};

// Auto-click mode names for display
const char* const AUTO_CLICK_MODE_NAMES[] = {
    "Disabled",
    "Toggle - While Tracking",
    "Toggle - Locked Only",
    "Hold - While Tracking",
    "Hold - Locked Only"
};

// Tracking center mode names
const char* const TRACKING_CENTER_NAMES[] = {
    "Screen Center (FPS)",
    "Mouse Position (TPS)"
};

inline std::string get_key_name(int key_code) {
    auto it = KEY_NAMES.find(key_code);
    if (it != KEY_NAMES.end()) return it->second;
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X", key_code);
    return std::string(buf);
}

// GUI Status - all atomic for thread safety
struct GuiStatus {
    std::atomic<float> fps{0.0f};
    std::atomic<float> inference_fps{0.0f};
    std::atomic<int> targets{0};
    std::atomic<bool> active{false};
    std::atomic<bool> locked{false};
    std::atomic<bool> ads_active{false};
    std::atomic<bool> auto_click_on{false};
};

// Thread-safe config
struct AtomicConfig {
    std::atomic<float> sensitivity_scale{config::DEFAULT_SENSITIVITY};
    std::atomic<int> pixel_step{config::DEFAULT_PIXEL_STEP};
    std::atomic<double> mouse_delay{config::DEFAULT_MOUSE_DELAY};
    std::atomic<float> head_ratio{config::DEFAULT_HEAD_RATIO};
    std::atomic<int> lock_threshold{config::DEFAULT_LOCK_THRESHOLD};
    std::atomic<int> skip_frames{config::DEFAULT_SKIP_FRAMES};
    std::atomic<float> confidence{config::DEFAULT_CONFIDENCE};
    std::atomic<float> movement_boost{config::DEFAULT_MOVEMENT_BOOST};
    std::atomic<bool> enable_movement_compensation{config::DEFAULT_ENABLE_MOVEMENT_COMP};
    std::atomic<float> ads_multiplier{config::DEFAULT_ADS_MULTIPLIER};
    std::atomic<bool> enable_ads_compensation{config::DEFAULT_ENABLE_ADS_COMP};
    std::atomic<bool> debug_window{config::DEFAULT_DEBUG_WINDOW};
    std::atomic<int> fov{config::DEFAULT_FOV};
    
    // Smoothing settings
    std::atomic<int> smoothing_curve{config::DEFAULT_SMOOTHING_CURVE};
    std::atomic<float> smoothing_strength{config::DEFAULT_SMOOTHING_STRENGTH};
    
    // Auto-click settings (new system)
    std::atomic<int> auto_click_mode{config::DEFAULT_AUTO_CLICK_MODE};
    std::atomic<double> click_cooldown{config::DEFAULT_CLICK_COOLDOWN};
    std::atomic<int> auto_click_key{config::DEFAULT_AUTO_CLICK_KEY};
    
    // Tracking center mode
    std::atomic<int> tracking_center{config::DEFAULT_TRACKING_CENTER};
    
    // Prediction settings
    std::atomic<bool> prediction_enabled{config::DEFAULT_PREDICTION_ENABLED};
    std::atomic<float> prediction_strength{config::DEFAULT_PREDICTION_STRENGTH};
    std::atomic<float> prediction_lookahead{config::DEFAULT_PREDICTION_LOOKAHEAD};
    std::atomic<float> prediction_process_noise{config::DEFAULT_PREDICTION_PROCESS_NOISE};
    std::atomic<float> prediction_measurement_noise{config::DEFAULT_PREDICTION_MEASUREMENT_NOISE};
    
    // Key bindings
    std::atomic<int> trigger_key{config::DEFAULT_TRIGGER_KEY};
    std::atomic<int> strafe_left_key{config::DEFAULT_STRAFE_LEFT};
    std::atomic<int> strafe_right_key{config::DEFAULT_STRAFE_RIGHT};
    std::atomic<int> jump_key{config::DEFAULT_JUMP_KEY};
    std::atomic<int> ads_key{config::DEFAULT_ADS_KEY};
    
    void load_from(const RuntimeConfig& cfg);
    void save_to(RuntimeConfig& cfg) const;
};

// GUI class - runs in separate low-priority thread
class CatClickerGui {
public:
    CatClickerGui();
    ~CatClickerGui();
    
    bool start(const RuntimeConfig& initial_config);
    void stop();
    
    bool should_quit() const { return should_quit_.load(); }
    bool is_running() const { return running_.load(); }
    
    AtomicConfig& get_config() { return config_; }
    GuiStatus& get_status() { return status_; }
    bool config_changed() { return config_changed_.exchange(false); }

private:
    void gui_thread_func();
    bool initialize();
    bool render_frame();
    void shutdown();
    
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* render_target_ = nullptr;
    HWND hwnd_ = nullptr;
    WNDCLASSEXW wc_ = {};
    
    std::thread gui_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_quit_{false};
    std::atomic<bool> config_changed_{false};
    
    AtomicConfig config_;
    GuiStatus status_;
    
    // Local copies for ImGui sliders
    float local_sensitivity_, local_mouse_delay_, local_head_ratio_, local_confidence_;
    float local_movement_boost_, local_ads_multiplier_, local_click_cooldown_ms_;
    float local_prediction_strength_, local_prediction_lookahead_ms_;
    float local_process_noise_, local_measurement_noise_;
    float local_smoothing_strength_;
    int local_pixel_step_, local_lock_threshold_, local_skip_frames_, local_fov_;
    int local_smoothing_curve_, local_auto_click_mode_, local_tracking_center_;
    bool local_movement_comp_, local_ads_comp_, local_debug_window_;
    bool local_prediction_enabled_;
    int local_trigger_key_, local_strafe_left_, local_strafe_right_;
    int local_jump_key_, local_ads_key_, local_auto_click_key_;
    
    std::string waiting_for_key_;
    
    void setup_style();
    void render_stats_bar();
    void render_aiming_tab();
    void render_additional_tab();
    void render_keybinds_tab();
    void render_prediction_tab();
    void render_footer();
    void sync_local_from_atomic();
    void sync_atomic_from_local();
    void render_keybind_button(const char* label, int* key_value, const char* button_id);
    void check_key_capture();
    
    bool create_device_d3d(HWND hwnd);
    void cleanup_device_d3d();
    void create_render_target();
    void cleanup_render_target();
    
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace catclicker
