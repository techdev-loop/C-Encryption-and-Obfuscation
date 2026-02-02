#include "mouse_controller.h"
#include "priority_manager.h"
#include "config.h"
#include <cmath>

namespace catclicker {

MouseController::MouseController() = default;

MouseController::~MouseController() {
    stop_move_thread();
    disconnect();
}

bool MouseController::connect(uint16_t vid, uint16_t pid) {
#ifdef HIDAPI_AVAILABLE
    vid_ = vid ? vid : config::DEFAULT_HID_VID;
    pid_ = pid ? pid : config::DEFAULT_HID_PID;
    
    if (hid_init() != 0) {
        console::log_error("Failed to initialize hidapi");
        return false;
    }
    
    device_ = hid_open(vid_, pid_, nullptr);
    
    if (!device_) {
        vid_ = config::FALLBACK_HID_VID;
        pid_ = config::FALLBACK_HID_PID;
        device_ = hid_open(vid_, pid_, nullptr);
    }
    
    if (!device_) {
        console::log_warn("HID device not found, using SendInput fallback");
        connected_ = false;
        return true;
    }
    
    hid_set_nonblocking(device_, 1);
    connected_ = true;
    console::log_ok("HID device connected (Feature Report injection mode)");
    
    return true;
#else
    console::log_warn("hidapi not available, using SendInput fallback");
    connected_ = false;
    return true;
#endif
}

void MouseController::disconnect() {
#ifdef HIDAPI_AVAILABLE
    if (device_) {
        hid_close(device_);
        device_ = nullptr;
    }
    hid_exit();
#endif
    connected_ = false;
}

bool MouseController::move(int8_t dx, int8_t dy) {
#ifdef HIDAPI_AVAILABLE
    if (!connected_ || !device_) {
        FallbackMouseController::move(dx, dy);
        return true;
    }
    
    // Use Feature Report ID 2 for injection
    // Format: [Report ID] [buttons] [X] [Y] [wheel]
    // Report ID 2 is the FEATURE report defined in firmware
    uint8_t report[5] = {
        0x02,                           // Report ID 2 (Feature report for injection)
        0x00,                           // Buttons (none)
        static_cast<uint8_t>(dx),       // X movement
        static_cast<uint8_t>(dy),       // Y movement
        0x00                            // Wheel
    };
    
    // Use hid_send_feature_report for SET_REPORT (Feature)
    int result = hid_send_feature_report(device_, report, sizeof(report));
    
    return result >= 0;
#else
    FallbackMouseController::move(dx, dy);
    return true;
#endif
}

bool MouseController::click(MouseButton button) {
#ifdef HIDAPI_AVAILABLE
    if (!connected_ || !device_) {
        FallbackMouseController::click(button);
        return true;
    }
    
    // Button down via Feature Report ID 2
    uint8_t report_down[5] = {
        0x02,       // Report ID 2
        button,     // Buttons
        0x00,       // X
        0x00,       // Y
        0x00        // Wheel
    };
    hid_send_feature_report(device_, report_down, sizeof(report_down));
    
    precise_sleep_us(10000);  // 10ms
    
    // Button up
    uint8_t report_up[5] = {0x02, 0x00, 0x00, 0x00, 0x00};
    hid_send_feature_report(device_, report_up, sizeof(report_up));
    
    return true;
#else
    FallbackMouseController::click(button);
    return true;
#endif
}

void MouseController::queue_move(
    float target_x,
    float target_y,
    float center_x,
    float center_y,
    float scale_x,
    float scale_y,
    double delay,
    int pixel_step,
    int smoothing_curve,
    float smoothing_strength
) {
    stop_current_move_.store(true, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(move_mutex_);
        move_cmd_.target_x = target_x;
        move_cmd_.target_y = target_y;
        move_cmd_.center_x = center_x;
        move_cmd_.center_y = center_y;
        move_cmd_.scale_x = scale_x;
        move_cmd_.scale_y = scale_y;
        move_cmd_.delay = delay;
        move_cmd_.pixel_step = pixel_step;
        move_cmd_.smoothing_curve = smoothing_curve;
        move_cmd_.smoothing_strength = smoothing_strength;
        move_cmd_.valid = true;
    }
    
    move_pending_.store(true, std::memory_order_release);
}

void MouseController::stop_move() {
    stop_current_move_.store(true, std::memory_order_release);
}

bool MouseController::start_move_thread() {
    if (thread_running_) return true;
    
    thread_running_ = true;
    move_thread_ = std::thread(&MouseController::move_thread_func, this);
    
    return true;
}

void MouseController::stop_move_thread() {
    thread_running_ = false;
    stop_current_move_ = true;
    
    if (move_thread_.joinable()) {
        move_thread_.join();
    }
}

void MouseController::move_thread_func() {
    get_priority_manager().configure_mouse_thread();
    
    while (thread_running_) {
        if (!move_pending_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        MoveCommand cmd;
        {
            std::lock_guard<std::mutex> lock(move_mutex_);
            if (!move_cmd_.valid) {
                move_pending_.store(false, std::memory_order_release);
                continue;
            }
            cmd = move_cmd_;
            move_cmd_.valid = false;
        }
        move_pending_.store(false, std::memory_order_release);
        stop_current_move_.store(false, std::memory_order_release);
        
        execute_move(cmd);
    }
}

void MouseController::execute_move(const MoveCommand& cmd) {
    float diff_x = (cmd.target_x - cmd.center_x) * cmd.scale_x;
    float diff_y = (cmd.target_y - cmd.center_y) * cmd.scale_y;
    
    float length = std::sqrt(diff_x * diff_x + diff_y * diff_y);
    
    if (length < 1.0f) return;
    
    int num_steps = (std::max)(1, static_cast<int>(length) / cmd.pixel_step);
    
    // Direction vector
    float dir_x = diff_x / length;
    float dir_y = diff_y / length;
    
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    
    for (int k = 1; k <= num_steps; k++) {
        if (stop_current_move_.load(std::memory_order_acquire) || !thread_running_) {
            break;
        }
        
        // Calculate progress through the move (0 to 1)
        float linear_progress = static_cast<float>(k) / static_cast<float>(num_steps);
        
        // Apply smoothing curve to get actual progress
        float curved_progress = smoothing::apply_curve(
            linear_progress, 
            cmd.smoothing_curve, 
            cmd.smoothing_strength
        );
        
        // Calculate target position based on curved progress
        float target_sum_x = diff_x * curved_progress;
        float target_sum_y = diff_y * curved_progress;
        
        // Calculate delta from current accumulated position
        int x = static_cast<int>(std::round(target_sum_x - sum_x));
        int y = static_cast<int>(std::round(target_sum_y - sum_y));
        
        sum_x += x;
        sum_y += y;
        
        x = clamp(x, -127, 127);
        y = clamp(y, -127, 127);
        
        if (x != 0 || y != 0) {
            move(static_cast<int8_t>(x), static_cast<int8_t>(y));
            if (cmd.delay > 0) {
                precise_sleep_ms(cmd.delay * 1000.0);
            }
        }
    }
}

void FallbackMouseController::move(int dx, int dy) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    
    SendInput(1, &input, sizeof(INPUT));
}

void FallbackMouseController::click(MouseButton button) {
    INPUT inputs[2] = {};
    
    inputs[0].type = INPUT_MOUSE;
    if (button == BTN_LEFT) inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    else if (button == BTN_RIGHT) inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    else if (button == BTN_MIDDLE) inputs[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
    
    inputs[1].type = INPUT_MOUSE;
    if (button == BTN_LEFT) inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    else if (button == BTN_RIGHT) inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    else if (button == BTN_MIDDLE) inputs[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    
    SendInput(2, inputs, sizeof(INPUT));
}

} // namespace catclicker
