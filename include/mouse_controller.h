#pragma once

#include "common.h"
#include <atomic>
#include <thread>
#include <mutex>

#ifdef HIDAPI_AVAILABLE
#include <hidapi/hidapi.h>
#endif

namespace catclicker {

// Mouse button constants
enum MouseButton : uint8_t {
    BTN_NONE = 0x00,
    BTN_LEFT = 0x01,
    BTN_RIGHT = 0x02,
    BTN_MIDDLE = 0x04
};

// Move command for the move thread
struct MoveCommand {
    float target_x;
    float target_y;
    float center_x;
    float center_y;
    float scale_x;
    float scale_y;
    double delay;
    int pixel_step;
    
    // Smoothing parameters
    int smoothing_curve;      // SmoothingCurve enum value
    float smoothing_strength; // 0.0 = linear, 1.0 = full curve
    
    bool valid = false;
};

class MouseController {
public:
    MouseController();
    ~MouseController();
    
    // Connect to HID device
    bool connect(uint16_t vid = 0, uint16_t pid = 0);
    
    // Disconnect
    void disconnect();
    
    // Check connection status
    bool is_connected() const { return connected_; }
    
    // Direct mouse movement (immediate, blocking)
    bool move(int8_t dx, int8_t dy);
    
    // Direct click
    bool click(MouseButton button = BTN_LEFT);
    
    // Queue a move command (non-blocking, processed by move thread)
    void queue_move(
        float target_x,
        float target_y,
        float center_x,
        float center_y,
        float scale_x,
        float scale_y,
        double delay,
        int pixel_step,
        int smoothing_curve = 0,
        float smoothing_strength = 0.0f
    );
    
    // Stop current move
    void stop_move();
    
    // Start/stop the move thread
    bool start_move_thread();
    void stop_move_thread();

private:
    void move_thread_func();
    void execute_move(const MoveCommand& cmd);
    
#ifdef HIDAPI_AVAILABLE
    hid_device* device_ = nullptr;
#endif
    
    bool connected_ = false;
    uint16_t vid_ = 0;
    uint16_t pid_ = 0;
    
    // Move thread
    std::thread move_thread_;
    std::atomic<bool> thread_running_{false};
    std::atomic<bool> stop_current_move_{false};
    
    // Move command (double-buffered with atomic swap)
    MoveCommand move_cmd_;
    std::mutex move_mutex_;
    std::atomic<bool> move_pending_{false};
};

// Fallback: Windows SendInput mouse movement (if HID not available)
class FallbackMouseController {
public:
    static void move(int dx, int dy);
    static void click(MouseButton button = BTN_LEFT);
};

} // namespace catclicker
