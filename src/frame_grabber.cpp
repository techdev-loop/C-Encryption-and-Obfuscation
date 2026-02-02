#include "frame_grabber.h"
#include "priority_manager.h"
#include "config.h"
#include <opencv2/imgproc.hpp>
#include <intrin.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace catclicker {

// ============================================================================
// CPU Feature Detection
// ============================================================================

static bool g_cpu_features_detected = false;
static bool g_has_avx2 = false;
static bool g_has_sse41 = false;

static void detect_cpu_features() {
    if (g_cpu_features_detected) return;
    
    int cpuInfo[4] = {0};
    
    // Check for SSE4.1
    __cpuid(cpuInfo, 1);
    g_has_sse41 = (cpuInfo[2] & (1 << 19)) != 0;
    
    // Check for AVX2
    __cpuid(cpuInfo, 0);
    int nIds = cpuInfo[0];
    
    if (nIds >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        g_has_avx2 = (cpuInfo[1] & (1 << 5)) != 0;
    }
    
    g_cpu_features_detected = true;
    
    console::log_info("CPU features: SSE4.1=" + std::string(g_has_sse41 ? "yes" : "no") +
                     ", AVX2=" + std::string(g_has_avx2 ? "yes" : "no"));
}

bool cpu_supports_avx2() {
    detect_cpu_features();
    return g_has_avx2;
}

bool cpu_supports_sse41() {
    detect_cpu_features();
    return g_has_sse41;
}

// ============================================================================
// SIMD BGRA to BGR Conversion
// ============================================================================

// SSE4.1 version - processes 16 pixels at a time
// Input: BGRA BGRA BGRA BGRA ... (4 bytes per pixel)
// Output: BGR BGR BGR BGR ... (3 bytes per pixel)
void convert_bgra_to_bgr_sse41(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch) {
    // Shuffle mask to convert BGRA to BGR (and pack)
    // We process 16 input bytes (4 pixels) -> 12 output bytes (4 pixels)
    // Then we do 4 such operations for 16 pixels -> 48 output bytes
    
    const __m128i shuffle_mask0 = _mm_setr_epi8(
        0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1
    );
    const __m128i shuffle_mask1 = _mm_setr_epi8(
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 4
    );
    const __m128i shuffle_mask2 = _mm_setr_epi8(
        5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1, -1, -1, -1, -1
    );
    const __m128i shuffle_mask3 = _mm_setr_epi8(
        -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 4, 5, 6, 8, 9
    );
    const __m128i shuffle_mask4 = _mm_setr_epi8(
        10, 12, 13, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    );
    
    int dst_pitch = width * 3;
    int pixels_per_iter = 16;
    int simd_width = (width / pixels_per_iter) * pixels_per_iter;
    
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + y * src_pitch;
        uint8_t* dst_row = dst + y * dst_pitch;
        
        int x = 0;
        
        // SIMD loop - 16 pixels at a time
        for (; x < simd_width; x += 16) {
            // Load 64 bytes (16 BGRA pixels)
            __m128i bgra0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_row + x * 4));
            __m128i bgra1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_row + x * 4 + 16));
            __m128i bgra2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_row + x * 4 + 32));
            __m128i bgra3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_row + x * 4 + 48));
            
            // Convert each 4 pixels from BGRA to BGR
            // First 4 pixels: 16 bytes -> 12 bytes
            __m128i bgr0_part0 = _mm_shuffle_epi8(bgra0, shuffle_mask0);  // bytes 0-11
            __m128i bgr0_part1 = _mm_shuffle_epi8(bgra1, shuffle_mask1);  // bytes 12-15
            __m128i out0 = _mm_or_si128(bgr0_part0, bgr0_part1);
            
            // Second 4 pixels
            __m128i bgr1_part0 = _mm_shuffle_epi8(bgra1, shuffle_mask2);  // bytes 0-7
            __m128i bgr1_part1 = _mm_shuffle_epi8(bgra2, shuffle_mask3);  // bytes 8-15
            __m128i out1 = _mm_or_si128(bgr1_part0, bgr1_part1);
            
            // Third 4 pixels
            __m128i bgr2_part0 = _mm_shuffle_epi8(bgra2, shuffle_mask4);  // bytes 0-3
            __m128i bgr2_part1 = _mm_shuffle_epi8(bgra3, shuffle_mask0);  // bytes 4-15 (shifted)
            // Need to shift bgr2_part1 to align properly
            __m128i out2_temp = _mm_srli_si128(bgr2_part1, 4);  // shift right 4 bytes
            out2_temp = _mm_slli_si128(out2_temp, 4);           // shift left 4 bytes (clear high)
            __m128i out2 = _mm_or_si128(bgr2_part0, _mm_slli_si128(_mm_shuffle_epi8(bgra3, shuffle_mask0), 4));
            
            // Store 48 bytes (16 BGR pixels)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3), out0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3 + 16), out1);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3 + 32), out2);
        }
        
        // Scalar cleanup for remaining pixels
        for (; x < width; x++) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 0];  // B
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];  // G
            dst_row[x * 3 + 2] = src_row[x * 4 + 2];  // R
        }
    }
}

// Simplified but correct SSE4.1 version - 4 pixels at a time
void convert_bgra_to_bgr_sse41_simple(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch) {
    const __m128i shuffle = _mm_setr_epi8(
        0, 1, 2,    // pixel 0: BGR
        4, 5, 6,    // pixel 1: BGR
        8, 9, 10,   // pixel 2: BGR
        12, 13, 14, // pixel 3: BGR
        -1, -1, -1, -1  // padding (unused)
    );
    
    int dst_pitch = width * 3;
    
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + y * src_pitch;
        uint8_t* dst_row = dst + y * dst_pitch;
        
        int x = 0;
        
        // Process 4 pixels at a time
        for (; x + 4 <= width; x += 4) {
            __m128i bgra = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_row + x * 4));
            __m128i bgr = _mm_shuffle_epi8(bgra, shuffle);
            
            // Store 12 bytes (we loaded 16, shuffled to 12 useful + 4 garbage)
            // Use unaligned store and only write 12 bytes
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3), bgr);
        }
        
        // Handle the overlap/overwrite from previous iteration
        // and finish remaining pixels scalar
        for (; x < width; x++) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 0];
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];
            dst_row[x * 3 + 2] = src_row[x * 4 + 2];
        }
    }
}

// AVX2 version - processes 8 pixels at a time with better throughput
void convert_bgra_to_bgr_avx2(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch) {
    // For AVX2, we process 8 pixels at a time (32 bytes in -> 24 bytes out)
    const __m256i shuffle = _mm256_setr_epi8(
        // First 128 bits (4 pixels)
        0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1,
        // Second 128 bits (4 pixels) 
        0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1
    );
    
    int dst_pitch = width * 3;
    
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + y * src_pitch;
        uint8_t* dst_row = dst + y * dst_pitch;
        
        int x = 0;
        
        // Process 8 pixels at a time (two groups of 4)
        for (; x + 8 <= width; x += 8) {
            // Load 32 bytes (8 BGRA pixels)
            __m256i bgra = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_row + x * 4));
            
            // Shuffle to remove alpha channels
            __m256i bgr = _mm256_shuffle_epi8(bgra, shuffle);
            
            // Extract the two 128-bit lanes and store them
            __m128i lo = _mm256_castsi256_si128(bgr);        // First 4 pixels (12 useful bytes)
            __m128i hi = _mm256_extracti128_si256(bgr, 1);   // Second 4 pixels (12 useful bytes)
            
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3), lo);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_row + x * 3 + 12), hi);
        }
        
        // Scalar cleanup
        for (; x < width; x++) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 0];
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];
            dst_row[x * 3 + 2] = src_row[x * 4 + 2];
        }
    }
}

// Scalar fallback
void convert_bgra_to_bgr_scalar(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch) {
    int dst_pitch = width * 3;
    
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + y * src_pitch;
        uint8_t* dst_row = dst + y * dst_pitch;
        
        for (int x = 0; x < width; x++) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 0];  // B
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];  // G
            dst_row[x * 3 + 2] = src_row[x * 4 + 2];  // R
        }
    }
}

// Auto-select best implementation
void convert_bgra_to_bgr(const uint8_t* src, uint8_t* dst, int width, int height, int src_pitch) {
    detect_cpu_features();
    
    if (g_has_avx2) {
        convert_bgra_to_bgr_avx2(src, dst, width, height, src_pitch);
    } else if (g_has_sse41) {
        convert_bgra_to_bgr_sse41_simple(src, dst, width, height, src_pitch);
    } else {
        convert_bgra_to_bgr_scalar(src, dst, width, height, src_pitch);
    }
}

// ============================================================================
// Screen Center Utilities
// ============================================================================

static std::pair<int, int> g_screen_center = {0, 0};
static bool g_screen_center_cached = false;

std::pair<int, int> get_screen_center() {
    if (!g_screen_center_cached) {
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);
        g_screen_center = {width / 2, height / 2};
        g_screen_center_cached = true;
    }
    return g_screen_center;
}

ScreenRegion get_center_region(int box_size) {
    auto [cx, cy] = get_screen_center();
    ScreenRegion region;
    region.left = cx - box_size / 2;
    region.top = cy - box_size / 2;
    region.right = region.left + box_size;
    region.bottom = region.top + box_size;
    return region;
}

// ============================================================================
// FrameGrabber Implementation
// ============================================================================

FrameGrabber::FrameGrabber(int model_size, int initial_fov) 
    : model_size_(model_size)
    , current_fov_(initial_fov)
    , pending_fov_(initial_fov) {
    // Clamp FOV to valid range
    int fov = clamp(initial_fov, config::MIN_FOV, config::MAX_FOV);
    current_fov_.store(fov);
    pending_fov_.store(fov);
    update_region(fov);
    
    // Initialize center to screen center
    auto [cx, cy] = get_screen_center();
    current_center_x_.store(cx);
    current_center_y_.store(cy);
}

FrameGrabber::~FrameGrabber() {
    stop();
}

void FrameGrabber::update_region(int fov) {
    std::lock_guard<std::mutex> lock(region_mutex_);
    region_ = get_center_region(fov);
}

ScreenRegion FrameGrabber::get_region_centered_on(int center_x, int center_y, int fov) const {
    ScreenRegion region;
    region.left = center_x - fov / 2;
    region.top = center_y - fov / 2;
    region.right = region.left + fov;
    region.bottom = region.top + fov;
    
    // Clamp to screen bounds
    if (region.left < 0) {
        region.right -= region.left;
        region.left = 0;
    }
    if (region.top < 0) {
        region.bottom -= region.top;
        region.top = 0;
    }
    if (region.right > screen_width_) {
        region.left -= (region.right - screen_width_);
        region.right = screen_width_;
    }
    if (region.bottom > screen_height_) {
        region.top -= (region.bottom - screen_height_);
        region.bottom = screen_height_;
    }
    
    // Final clamp
    region.left = clamp(region.left, 0, screen_width_ - fov);
    region.top = clamp(region.top, 0, screen_height_ - fov);
    region.right = region.left + fov;
    region.bottom = region.top + fov;
    
    return region;
}

ScreenRegion FrameGrabber::get_region() const {
    // Return region based on current tracking mode
    int fov = current_fov_.load();
    
    if (tracking_mode_.load() == static_cast<int>(TrackingCenterMode::MOUSE_POSITION)) {
        return get_region_centered_on(current_center_x_.load(), current_center_y_.load(), fov);
    }
    
    return get_center_region(fov);
}

std::pair<int, int> FrameGrabber::get_tracking_center() const {
    if (tracking_mode_.load() == static_cast<int>(TrackingCenterMode::MOUSE_POSITION)) {
        return {current_center_x_.load(), current_center_y_.load()};
    }
    return get_screen_center();
}

void FrameGrabber::set_fov(int new_fov) {
    new_fov = clamp(new_fov, config::MIN_FOV, config::MAX_FOV);
    if (new_fov != current_fov_.load()) {
        pending_fov_.store(new_fov);
        fov_changed_.store(true);
    }
}

bool FrameGrabber::recreate_staging_texture(int fov) {
    staging_texture_.Reset();
    
    D3D11_TEXTURE2D_DESC staging_desc = {};
    staging_desc.Width = fov;
    staging_desc.Height = fov;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    HRESULT hr = device_->CreateTexture2D(&staging_desc, nullptr, &staging_texture_);
    if (FAILED(hr)) {
        console::log_error("Failed to create staging texture for FOV " + std::to_string(fov));
        return false;
    }
    return true;
}

bool FrameGrabber::init_dxgi() {
    HRESULT hr;
    
    screen_width_ = GetSystemMetrics(SM_CXSCREEN);
    screen_height_ = GetSystemMetrics(SM_CYSCREEN);
    
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL feature_level;
    
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        feature_levels, 1, D3D11_SDK_VERSION, &device_, &feature_level, &context_);
    
    if (FAILED(hr)) {
        console::log_error("Failed to create D3D11 device");
        return false;
    }
    
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;
    
    hr = output1->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        console::log_error("Failed to create desktop duplication");
        return false;
    }
    
    // Create initial staging texture
    if (!recreate_staging_texture(current_fov_.load())) {
        return false;
    }
    
    // Detect CPU features for SIMD selection
    detect_cpu_features();
    
    console::log_ok("DXGI Desktop Duplication initialized");
    return true;
}

bool FrameGrabber::start() {
    if (running_) return true;
    
    if (!init_dxgi()) {
        return false;
    }
    
    // Pre-allocate buffers (output is always model_size)
    buffers_[0] = cv::Mat(model_size_, model_size_, CV_8UC3);
    buffers_[1] = cv::Mat(model_size_, model_size_, CV_8UC3);
    
    running_ = true;
    capture_thread_ = std::thread(&FrameGrabber::capture_loop, this);
    
    int timeout_ms = 2000;
    while (!frame_ready_ && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timeout_ms -= 10;
    }
    
    if (!frame_ready_) {
        console::log_error("Timeout waiting for first frame");
        stop();
        return false;
    }
    
    console::log_ok("FrameGrabber started - FOV: " + std::to_string(current_fov_.load()) + 
                   ", output: " + std::to_string(model_size_) + "x" + std::to_string(model_size_) +
                   ", SIMD: " + (g_has_avx2 ? "AVX2" : (g_has_sse41 ? "SSE4.1" : "scalar")));
    
    return true;
}

void FrameGrabber::stop() {
    running_ = false;
    
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    duplication_.Reset();
    staging_texture_.Reset();
    context_.Reset();
    device_.Reset();
}

bool FrameGrabber::acquire_frame(cv::Mat& out_frame, cv::Mat& scaled_frame) {
    // Check for FOV change
    if (fov_changed_.load()) {
        int new_fov = pending_fov_.load();
        if (recreate_staging_texture(new_fov)) {
            current_fov_.store(new_fov);
            update_region(new_fov);
            console::log_info("FOV changed to " + std::to_string(new_fov));
        }
        fov_changed_.store(false);
    }
    
    int fov = current_fov_.load();
    
    // Determine capture center based on tracking mode
    int center_x, center_y;
    if (tracking_mode_.load() == static_cast<int>(TrackingCenterMode::MOUSE_POSITION)) {
        auto [mx, my] = get_mouse_position();
        center_x = mx;
        center_y = my;
        current_center_x_.store(mx);
        current_center_y_.store(my);
    } else {
        auto [sx, sy] = get_screen_center();
        center_x = sx;
        center_y = sy;
        current_center_x_.store(sx);
        current_center_y_.store(sy);
    }
    
    ScreenRegion region = get_region_centered_on(center_x, center_y, fov);
    
    HRESULT hr;
    ComPtr<IDXGIResource> desktop_resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    
    hr = duplication_->AcquireNextFrame(0, &frame_info, &desktop_resource);
    
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        console::log_warn("Desktop duplication access lost, reinitializing...");
        duplication_.Reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        ComPtr<IDXGIDevice> dxgi_device;
        device_.As(&dxgi_device);
        ComPtr<IDXGIAdapter> adapter;
        dxgi_device->GetAdapter(&adapter);
        ComPtr<IDXGIOutput> output;
        adapter->EnumOutputs(0, &output);
        ComPtr<IDXGIOutput1> output1;
        output.As(&output1);
        output1->DuplicateOutput(device_.Get(), &duplication_);
        return false;
    }
    
    if (FAILED(hr)) {
        return false;
    }
    
    ComPtr<ID3D11Texture2D> desktop_texture;
    hr = desktop_resource.As(&desktop_texture);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return false;
    }
    
    // Copy the region to staging texture
    D3D11_BOX source_box;
    source_box.left = region.left;
    source_box.top = region.top;
    source_box.right = region.right;
    source_box.bottom = region.bottom;
    source_box.front = 0;
    source_box.back = 1;
    
    context_->CopySubresourceRegion(staging_texture_.Get(), 0, 0, 0, 0,
        desktop_texture.Get(), 0, &source_box);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return false;
    }
    
    // Create temp buffer for raw capture
    cv::Mat raw_frame(fov, fov, CV_8UC3);
    
    // Convert BGRA to BGR using SIMD
    convert_bgra_to_bgr(
        static_cast<const uint8_t*>(mapped.pData),
        raw_frame.data,
        fov,
        fov,
        mapped.RowPitch
    );
    
    context_->Unmap(staging_texture_.Get(), 0);
    duplication_->ReleaseFrame();
    
    // Store raw frame for debug
    {
        std::lock_guard<std::mutex> lock(raw_mutex_);
        raw_buffer_ = raw_frame.clone();
    }
    
    // Scale to model size if needed
    if (fov != model_size_) {
        cv::resize(raw_frame, scaled_frame, cv::Size(model_size_, model_size_), 0, 0, cv::INTER_LINEAR);
    } else {
        raw_frame.copyTo(scaled_frame);
    }
    
    return true;
}

void FrameGrabber::capture_loop() {
    get_priority_manager().configure_capture_thread();
    
    while (running_) {
        int back_idx = 1 - front_buffer_.load(std::memory_order_acquire);
        
        cv::Mat raw_frame;
        if (acquire_frame(raw_frame, buffers_[back_idx])) {
            front_buffer_.store(back_idx, std::memory_order_release);
            capture_count_.fetch_add(1, std::memory_order_relaxed);
            frame_ready_.store(true, std::memory_order_release);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

const cv::Mat& FrameGrabber::get_frame() {
    int idx = front_buffer_.load(std::memory_order_acquire);
    return buffers_[idx];
}

cv::Mat FrameGrabber::get_frame_copy() {
    int idx = front_buffer_.load(std::memory_order_acquire);
    return buffers_[idx].clone();
}

cv::Mat FrameGrabber::get_raw_frame_copy() {
    std::lock_guard<std::mutex> lock(raw_mutex_);
    return raw_buffer_.clone();
}

} // namespace catclicker
