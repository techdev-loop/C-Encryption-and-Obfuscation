#pragma once

#include <cstdint>

namespace catclicker {
namespace config {

// ============== Capture Settings ==============
constexpr int CAPTURE_SIZE = 320;
constexpr int MODEL_SIZE = 320;
constexpr int TARGET_FPS = 120;

// ============== FOV Settings ==============
constexpr int DEFAULT_FOV = 320;       // Default FOV (capture size in pixels)
constexpr int MIN_FOV = 160;           // Minimum FOV (tighter aim)
constexpr int MAX_FOV = 640;           // Maximum FOV (wider view)

// ============== Model Settings ==============
constexpr float DEFAULT_CONFIDENCE = 0.3f;
constexpr int MAX_DETECTIONS = 3;
constexpr bool USE_FP16 = true;

// ============== Mouse Settings ==============
constexpr float DEFAULT_SENSITIVITY = 0.75f;
constexpr double DEFAULT_MOUSE_DELAY = 0.0001;  // seconds
constexpr int DEFAULT_PIXEL_STEP = 4;
constexpr int DEFAULT_LOCK_THRESHOLD = 3;

// ============== Smoothing Curve Settings ==============
enum class SmoothingCurve : int {
    LINEAR = 0,      // No smoothing (original behavior)
    EASE_OUT = 1,    // Fast start, slow end (most natural for aiming)
    EASE_IN = 2,     // Slow start, fast end
    EASE_IN_OUT = 3, // Slow start and end
    SIGMOID = 4,     // S-curve (very smooth)
    EXPONENTIAL = 5  // Aggressive start, very slow end
};

constexpr int DEFAULT_SMOOTHING_CURVE = static_cast<int>(SmoothingCurve::LINEAR);
constexpr float DEFAULT_SMOOTHING_STRENGTH = 0.5f;  // 0.0 = linear, 1.0 = full curve

// ============== Auto-Click Settings ==============
enum class AutoClickMode : int {
    DISABLED = 0,           // No auto-click
    TOGGLE_WHILE_TRACKING = 1,  // Toggle on/off, shoots while tracking
    TOGGLE_LOCKED_ONLY = 2,     // Toggle on/off, shoots only when locked
    HOLD_WHILE_TRACKING = 3,    // Hold key to shoot while tracking
    HOLD_LOCKED_ONLY = 4        // Hold key to shoot only when locked
};

constexpr int DEFAULT_AUTO_CLICK_MODE = static_cast<int>(AutoClickMode::DISABLED);
constexpr double DEFAULT_CLICK_COOLDOWN = 0.05;  // 50ms between clicks (20 CPS max)
constexpr int DEFAULT_AUTO_CLICK_KEY = 0x4B;  // K key (toggle or hold)

// ============== Tracking Mode Settings ==============
enum class TrackingCenter : int {
    SCREEN_CENTER = 0,    // Traditional FPS style - center of screen
    MOUSE_POSITION = 1    // TPS/Flexible style - follows mouse cursor
};

constexpr int DEFAULT_TRACKING_CENTER = static_cast<int>(TrackingCenter::SCREEN_CENTER);

// ============== Targeting Settings ==============
constexpr float DEFAULT_HEAD_RATIO = 5.1f; // usually 2.4
constexpr float TARGET_HYSTERESIS = 0.3; // USUALLY 0.75
constexpr float MIN_TARGET_DISTANCE = 2.0f;

// ============== Frame Skip ==============
constexpr int DEFAULT_SKIP_FRAMES = 2;

// ============== Key Bindings (Virtual Key Codes) ==============
constexpr int DEFAULT_TRIGGER_KEY = 0x14;      // Caps Lock
constexpr int DEFAULT_STRAFE_LEFT = 0x41;      // A
constexpr int DEFAULT_STRAFE_RIGHT = 0x44;     // D
constexpr int DEFAULT_JUMP_KEY = 0x20;         // Space
constexpr int DEFAULT_ADS_KEY = 0x02;          // Right Mouse Button

// ============== Movement Compensation ==============
constexpr float DEFAULT_MOVEMENT_BOOST = 0.5f;
constexpr float DEFAULT_ADS_MULTIPLIER = 1.5f;
constexpr bool DEFAULT_ENABLE_MOVEMENT_COMP = true;
constexpr bool DEFAULT_ENABLE_ADS_COMP = true;

// ============== Prediction Settings ==============
constexpr bool DEFAULT_PREDICTION_ENABLED = false;
constexpr float DEFAULT_PREDICTION_STRENGTH = 0.5f;    // 0.0-1.0, blend factor
constexpr float DEFAULT_PREDICTION_LOOKAHEAD = 0.016f; // seconds (~1 frame at 60fps)
constexpr float DEFAULT_PREDICTION_PROCESS_NOISE = 0.5f;
constexpr float DEFAULT_PREDICTION_MEASUREMENT_NOISE = 2.0f;

// ============== HID Device Settings ==============
// Default: Logitech-style HID
constexpr uint16_t DEFAULT_HID_VID = 0x045e;
constexpr uint16_t DEFAULT_HID_PID = 0x00cb;
// Fallback
constexpr uint16_t FALLBACK_HID_VID = 0x1BCF;
constexpr uint16_t FALLBACK_HID_PID = 0x0005;

// ============== Thread Priority ==============
constexpr int THREAD_PRIORITY_CRITICAL = THREAD_PRIORITY_TIME_CRITICAL;
constexpr int THREAD_PRIORITY_HIGH = THREAD_PRIORITY_HIGHEST;
constexpr int THREAD_PRIORITY_LOW = THREAD_PRIORITY_LOWEST;

// ============== Debug ==============
constexpr bool DEFAULT_DEBUG_WINDOW = false;

} // namespace config

// Runtime configuration (can be modified)
struct RuntimeConfig {
    float confidence = config::DEFAULT_CONFIDENCE;
    float sensitivity_scale = config::DEFAULT_SENSITIVITY;
    double mouse_delay = config::DEFAULT_MOUSE_DELAY;
    int pixel_step = config::DEFAULT_PIXEL_STEP;
    float head_ratio = config::DEFAULT_HEAD_RATIO;
    int lock_threshold = config::DEFAULT_LOCK_THRESHOLD;
    int skip_frames = config::DEFAULT_SKIP_FRAMES;
    bool debug_window = config::DEFAULT_DEBUG_WINDOW;
    
    // FOV (capture region size in pixels)
    int fov = config::DEFAULT_FOV;
    
    // Movement compensation
    float movement_boost = config::DEFAULT_MOVEMENT_BOOST;
    bool enable_movement_compensation = config::DEFAULT_ENABLE_MOVEMENT_COMP;
    
    // ADS compensation  
    float ads_multiplier = config::DEFAULT_ADS_MULTIPLIER;
    bool enable_ads_compensation = config::DEFAULT_ENABLE_ADS_COMP;
    
    // Smoothing curve settings
    int smoothing_curve = config::DEFAULT_SMOOTHING_CURVE;
    float smoothing_strength = config::DEFAULT_SMOOTHING_STRENGTH;
    
    // Auto-click settings (new unified system)
    int auto_click_mode = config::DEFAULT_AUTO_CLICK_MODE;
    double click_cooldown = config::DEFAULT_CLICK_COOLDOWN;
    int auto_click_key = config::DEFAULT_AUTO_CLICK_KEY;
    
    // Tracking center mode
    int tracking_center = config::DEFAULT_TRACKING_CENTER;
    
    // Prediction settings
    bool prediction_enabled = config::DEFAULT_PREDICTION_ENABLED;
    float prediction_strength = config::DEFAULT_PREDICTION_STRENGTH;
    float prediction_lookahead = config::DEFAULT_PREDICTION_LOOKAHEAD;
    float prediction_process_noise = config::DEFAULT_PREDICTION_PROCESS_NOISE;
    float prediction_measurement_noise = config::DEFAULT_PREDICTION_MEASUREMENT_NOISE;
    
    // Key bindings
    int trigger_key = config::DEFAULT_TRIGGER_KEY;
    int strafe_left_key = config::DEFAULT_STRAFE_LEFT;
    int strafe_right_key = config::DEFAULT_STRAFE_RIGHT;
    int jump_key = config::DEFAULT_JUMP_KEY;
    int ads_key = config::DEFAULT_ADS_KEY;
    
    // Legacy compatibility (maps to auto_click_mode)
    bool auto_click_enabled = false;
    int auto_click_toggle_key = config::DEFAULT_AUTO_CLICK_KEY;
};

} // namespace catclicker
