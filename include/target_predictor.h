#pragma once

#include "common.h"
#include <array>
#include <unordered_map>

namespace catclicker {

// Simple 2D Kalman filter for position and velocity estimation
// State vector: [x, y, vx, vy]
// Measurement: [x, y]
class KalmanFilter2D {
public:
    KalmanFilter2D();
    
    // Initialize filter with first measurement
    void init(float x, float y, double timestamp);
    
    // Predict state to given timestamp
    void predict(double timestamp);
    
    // Update with new measurement
    void update(float x, float y, double timestamp);
    
    // Get current state estimates
    float get_x() const { return state_[0]; }
    float get_y() const { return state_[1]; }
    float get_vx() const { return state_[2]; }
    float get_vy() const { return state_[3]; }
    
    // Get predicted position at future time
    void get_predicted_position(double future_time, float& out_x, float& out_y) const;
    
    // Check if filter is initialized
    bool is_initialized() const { return initialized_; }
    
    // Reset filter
    void reset();
    
    // Get velocity magnitude
    float get_speed() const;
    
    // Set process noise (higher = more responsive, lower = smoother)
    void set_process_noise(float noise) { process_noise_ = noise; }
    
    // Set measurement noise (higher = trust predictions more, lower = trust measurements more)
    void set_measurement_noise(float noise) { measurement_noise_ = noise; }

private:
    // State vector [x, y, vx, vy]
    std::array<float, 4> state_;
    
    // Covariance matrix (4x4, stored as flat array)
    std::array<float, 16> covariance_;
    
    // Last update timestamp
    double last_timestamp_;
    
    // Noise parameters
    float process_noise_;
    float measurement_noise_;
    
    bool initialized_;
    
    // Matrix operations (inline for performance)
    void matrix_multiply_4x4(const float* a, const float* b, float* result);
    void matrix_multiply_4x2(const float* a, const float* b, float* result);
    void matrix_transpose_4x4(const float* in, float* out);
    void matrix_add_4x4(const float* a, const float* b, float* result);
    float matrix_determinant_2x2(const float* m);
    void matrix_inverse_2x2(const float* m, float* result);
};

// Manages prediction for multiple tracked targets
class TargetPredictor {
public:
    TargetPredictor();
    ~TargetPredictor() = default;
    
    // Update tracker with new detection
    // Returns predicted position if prediction is enabled, otherwise returns input position
    void update_target(
        int target_id,
        float measured_x,
        float measured_y,
        double timestamp,
        float& out_x,
        float& out_y
    );
    
    // Get predicted position for a target at a future time
    bool get_predicted_position(
        int target_id,
        double lookahead_seconds,
        float& out_x,
        float& out_y
    ) const;
    
    // Remove stale trackers (targets not seen recently)
    void cleanup_stale_trackers(double current_time, double max_age = 0.5);
    
    // Reset specific tracker
    void reset_tracker(int target_id);
    
    // Reset all trackers
    void reset_all();
    
    // Configuration
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }
    
    void set_prediction_strength(float strength) { prediction_strength_ = clamp(strength, 0.0f, 1.0f); }
    float get_prediction_strength() const { return prediction_strength_; }
    
    // Lookahead time in seconds (how far ahead to predict)
    void set_lookahead_time(float seconds) { lookahead_time_ = clamp(seconds, 0.0f, 0.1f); }
    float get_lookahead_time() const { return lookahead_time_; }
    
    // Set Kalman filter parameters
    void set_process_noise(float noise);
    void set_measurement_noise(float noise);
    
    // Get velocity info for a target (for debugging/display)
    bool get_target_velocity(int target_id, float& vx, float& vy) const;
    
    // Get number of active trackers
    size_t get_active_tracker_count() const { return trackers_.size(); }

private:
    struct TrackerEntry {
        KalmanFilter2D filter;
        double last_seen;
        int update_count;
    };
    
    std::unordered_map<int, TrackerEntry> trackers_;
    
    bool enabled_;
    float prediction_strength_;  // 0.0 = no prediction, 1.0 = full prediction
    float lookahead_time_;       // How far ahead to predict (seconds)
    float process_noise_;
    float measurement_noise_;
    
    // Minimum updates before prediction is trusted
    static constexpr int MIN_UPDATES_FOR_PREDICTION = 3;
};

} // namespace catclicker
