#pragma once

#include "common.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <opencv2/core.hpp>
#include <atomic>
#include <thread>
#include <mutex>

namespace catclicker {

using Microsoft::WRL::ComPtr;

// Screen region definition
struct ScreenRegion {
    int left;
    int top;
    int right;
    int bottom;
    
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    int center_x() const { return (left + right) / 2; }
    int center_y() const { return (top + bottom) / 2; }
};

// Tracking center mode
enum class TrackingCenterMode {
    SCREEN_CENTER = 0,    // Traditional FPS - center of screen
    MOUSE_POSITION = 1    // TPS/Flexible - follows mouse cursor
};

class FrameGrabber {
public:
    // model_size = size expected by the model (e.g., 320)
    // initial_fov = initial capture region size (can be larger than model_size)
    FrameGrabber(int model_size = 320, int initial_fov = 320);
    ~FrameGrabber();
    
    // Initialize DXGI and start capture
    bool start();
    
    // Stop capture
    void stop();
    
    // Get current frame (returns reference to internal buffer, do not hold long)
    // Frame is always model_size x model_size, scaled from FOV capture
    const cv::Mat& get_frame();
    
    // Get a copy of the current frame (for debug display)
    cv::Mat get_frame_copy();
    
    // Get raw capture frame (at FOV size, for debug)
    cv::Mat get_raw_frame_copy();
    
    // Get capture region (in screen coordinates)
    ScreenRegion get_region() const;
    
    // Get current FOV
    int get_fov() const { return current_fov_.load(); }
    
    // Set FOV dynamically (thread-safe)
    // Will take effect on next frame capture
    void set_fov(int new_fov);
    
    // Get the scale factor (FOV / model_size)
    // Use this to convert detection coordinates to screen coordinates
    float get_scale_factor() const { return static_cast<float>(current_fov_.load()) / static_cast<float>(model_size_); }
    
    // Set tracking center mode
    void set_tracking_center_mode(TrackingCenterMode mode) { 
        tracking_mode_.store(static_cast<int>(mode)); 
    }
    TrackingCenterMode get_tracking_center_mode() const { 
        return static_cast<TrackingCenterMode>(tracking_mode_.load()); 
    }
    
    // Get the current tracking center (either screen center or mouse position)
    std::pair<int, int> get_tracking_center() const;
    
    // Stats
    uint64_t get_capture_count() const { return capture_count_.load(); }
    
private:
    bool init_dxgi();
    void capture_loop();
    bool acquire_frame(cv::Mat& out_frame, cv::Mat& scaled_frame);
    void update_region(int fov);
    bool recreate_staging_texture(int fov);
    
    // Get region centered on a specific point
    ScreenRegion get_region_centered_on(int center_x, int center_y, int fov) const;
    
    int model_size_;
    std::atomic<int> current_fov_;
    std::atomic<int> pending_fov_;  // For thread-safe FOV changes
    std::atomic<bool> fov_changed_{false};
    
    // Tracking center mode
    std::atomic<int> tracking_mode_{static_cast<int>(TrackingCenterMode::SCREEN_CENTER)};
    
    ScreenRegion region_;
    std::mutex region_mutex_;
    
    // DXGI resources
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> staging_texture_;
    
    // Double buffering for scaled output (model_size x model_size)
    cv::Mat buffers_[2];
    std::atomic<int> front_buffer_{0};
    
    // Raw capture buffer (FOV x FOV) for debug
    cv::Mat raw_buffer_;
    std::mutex raw_mutex_;
    
    // Current capture center (updated per frame for mouse-centered mode)
    std::atomic<int> current_center_x_{0};
    std::atomic<int> current_center_y_{0};
    
    // Thread control
    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> frame_ready_{false};
    
    // Stats
    std::atomic<uint64_t> capture_count_{0};
    
    // Screen info
    int screen_width_ = 0;
    int screen_height_ = 0;
};

// Utility: Get screen center
std::pair<int, int> get_screen_center();

// Utility: Calculate center region for given box size (screen-centered)
ScreenRegion get_center_region(int box_size);

// ============================================================================
// SIMD Pixel Conversion Functions
// ============================================================================

// Check CPU feature support at runtime
bool cpu_supports_avx2();
bool cpu_supports_sse41();

// BGRA to BGR conversion using SIMD
// Processes 16 pixels at a time with SSE4.1
void convert_bgra_to_bgr_sse41(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch);

// BGRA to BGR conversion using AVX2 (32 pixels at a time)
void convert_bgra_to_bgr_avx2(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch);

// Scalar fallback
void convert_bgra_to_bgr_scalar(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch);

// Auto-select best available implementation
void convert_bgra_to_bgr(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch);

} // namespace catclicker
