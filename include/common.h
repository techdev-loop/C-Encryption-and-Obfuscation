#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <iostream>
#include <algorithm>

// SIMD intrinsics
#include <immintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>

namespace catclicker {

// High-resolution timing
using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::duration<double>;

// Precise sleep using spin-wait for sub-millisecond accuracy
inline void precise_sleep_us(int64_t microseconds) {
    if (microseconds <= 0) return;
    
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    int64_t target_ticks = (microseconds * freq.QuadPart) / 1000000;
    int64_t end_ticks = start.QuadPart + target_ticks;
    
    // For longer waits, sleep most of the time then spin
    if (microseconds > 1000) {
        int64_t sleep_us = microseconds - 500;
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }
    
    // Spin-wait for precision
    do {
        QueryPerformanceCounter(&now);
        _mm_pause(); // CPU hint to reduce power
    } while (now.QuadPart < end_ticks);
}

inline void precise_sleep_ms(double milliseconds) {
    precise_sleep_us(static_cast<int64_t>(milliseconds * 1000.0));
}

// Get current time in seconds (high precision)
inline double get_time_seconds() {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

// Detection result structure
struct Detection {
    float x1, y1, x2, y2;  // Bounding box
    float confidence;
    int class_id;
    
    float width() const { return x2 - x1; }
    float height() const { return y2 - y1; }
    float center_x() const { return (x1 + x2) / 2.0f; }
    float center_y() const { return (y1 + y2) / 2.0f; }
};

// Target information
struct Target {
    int id;
    float absolute_x;
    float absolute_y;
    float relative_x;
    float relative_y;
    float confidence;
    float distance;
    Detection detection;
    
    bool valid() const { return id >= 0; }
};

// Invalid target sentinel
inline Target invalid_target() {
    Target t{};
    t.id = -1;
    return t;
}

// Distance calculation
inline float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

// Clamp value
template<typename T>
inline T clamp(T value, T min_val, T max_val) {
    return (std::max)(min_val, (std::min)(value, max_val));
}

// ============================================================================
// Smoothing Curve Functions
// ============================================================================
// All functions take t in [0, 1] and return a value in [0, 1]
// t represents progress through the movement (0 = start, 1 = end)

namespace smoothing {

// Linear (no smoothing)
inline float linear(float t) {
    return t;
}

// Ease out (fast start, slow end) - most natural for aiming
// Uses quadratic easing: f(t) = 1 - (1-t)^2
inline float ease_out(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv;
}

// Ease out cubic (even smoother deceleration)
inline float ease_out_cubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Ease in (slow start, fast end)
// Uses quadratic easing: f(t) = t^2
inline float ease_in(float t) {
    return t * t;
}

// Ease in-out (slow start and end)
// Uses smoothstep: f(t) = 3t^2 - 2t^3
inline float ease_in_out(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Sigmoid curve (very smooth S-curve)
// Scaled and shifted sigmoid: f(t) = 1 / (1 + e^(-12*(t-0.5)))
inline float sigmoid(float t) {
    float x = 12.0f * (t - 0.5f);
    return 1.0f / (1.0f + std::exp(-x));
}

// Exponential (aggressive start, very slow end)
// f(t) = 1 - e^(-5t)
inline float exponential(float t) {
    return 1.0f - std::exp(-5.0f * t);
}

// Apply curve with strength blending
// strength = 0: linear, strength = 1: full curve
inline float apply_curve(float t, int curve_type, float strength) {
    float curved;
    switch (curve_type) {
        case 0: curved = linear(t); break;
        case 1: curved = ease_out(t); break;
        case 2: curved = ease_in(t); break;
        case 3: curved = ease_in_out(t); break;
        case 4: curved = sigmoid(t); break;
        case 5: curved = exponential(t); break;
        default: curved = linear(t); break;
    }
    
    // Blend between linear and curved
    return linear(t) * (1.0f - strength) + curved * strength;
}

} // namespace smoothing

// ============================================================================
// Mouse Position Tracking
// ============================================================================

inline std::pair<int, int> get_mouse_position() {
    POINT pt;
    if (GetCursorPos(&pt)) {
        return {pt.x, pt.y};
    }
    // Fallback to screen center if GetCursorPos fails
    return {GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2};
}

// Console colors for logging
namespace console {
    inline void set_color(int color) {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
    }
    
    constexpr int WHITE = 7;
    constexpr int GREEN = 10;
    constexpr int CYAN = 11;
    constexpr int RED = 12;
    constexpr int YELLOW = 14;
    
    inline void log_ok(const std::string& msg) {
        set_color(GREEN);
        std::cout << "[OK] ";
        set_color(WHITE);
        std::cout << msg << std::endl;
    }
    
    inline void log_info(const std::string& msg) {
        set_color(CYAN);
        std::cout << "[INFO] ";
        set_color(WHITE);
        std::cout << msg << std::endl;
    }
    
    inline void log_warn(const std::string& msg) {
        set_color(YELLOW);
        std::cout << "[WARN] ";
        set_color(WHITE);
        std::cout << msg << std::endl;
    }
    
    inline void log_error(const std::string& msg) {
        set_color(RED);
        std::cout << "[ERROR] ";
        set_color(WHITE);
        std::cout << msg << std::endl;
    }
}

} // namespace catclicker
