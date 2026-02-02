#include "target_predictor.h"
#include <cmath>
#include <cstring>

namespace catclicker {

// ============================================================================
// KalmanFilter2D Implementation
// ============================================================================

KalmanFilter2D::KalmanFilter2D()
    : last_timestamp_(0.0)
    , process_noise_(0.1f)
    , measurement_noise_(1.0f)
    , initialized_(false)
{
    reset();
}

void KalmanFilter2D::reset() {
    state_.fill(0.0f);
    covariance_.fill(0.0f);
    
    // Initialize covariance with high uncertainty
    // Diagonal elements: [x_var, y_var, vx_var, vy_var]
    covariance_[0]  = 100.0f;  // x variance
    covariance_[5]  = 100.0f;  // y variance
    covariance_[10] = 100.0f;  // vx variance
    covariance_[15] = 100.0f;  // vy variance
    
    initialized_ = false;
    last_timestamp_ = 0.0;
}

void KalmanFilter2D::init(float x, float y, double timestamp) {
    reset();
    
    state_[0] = x;
    state_[1] = y;
    state_[2] = 0.0f;  // Initial velocity unknown
    state_[3] = 0.0f;
    
    // Lower position uncertainty since we have a measurement
    covariance_[0]  = 1.0f;   // x variance
    covariance_[5]  = 1.0f;   // y variance
    covariance_[10] = 100.0f; // vx variance (still uncertain)
    covariance_[15] = 100.0f; // vy variance (still uncertain)
    
    last_timestamp_ = timestamp;
    initialized_ = true;
}

void KalmanFilter2D::predict(double timestamp) {
    if (!initialized_) return;
    
    float dt = static_cast<float>(timestamp - last_timestamp_);
    if (dt <= 0.0f) return;
    
    // Clamp dt to avoid numerical issues
    dt = (std::min)(dt, 0.1f);
    
    // State transition matrix F:
    // [1  0  dt  0 ]   [x ]   [x + vx*dt]
    // [0  1  0   dt] * [y ] = [y + vy*dt]
    // [0  0  1   0 ]   [vx]   [vx       ]
    // [0  0  0   1 ]   [vy]   [vy       ]
    
    // Predict state
    state_[0] += state_[2] * dt;  // x += vx * dt
    state_[1] += state_[3] * dt;  // y += vy * dt
    // velocity remains constant in prediction
    
    // Predict covariance: P = F * P * F^T + Q
    // F * P
    std::array<float, 16> FP;
    float F[16] = {
        1, 0, dt, 0,
        0, 1, 0, dt,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    matrix_multiply_4x4(F, covariance_.data(), FP.data());
    
    // (F * P) * F^T
    float FT[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        dt, 0, 1, 0,
        0, dt, 0, 1
    };
    std::array<float, 16> FPFT;
    matrix_multiply_4x4(FP.data(), FT, FPFT.data());
    
    // Process noise Q (models acceleration uncertainty)
    float dt2 = dt * dt;
    float dt3 = dt2 * dt;
    float dt4 = dt2 * dt2;
    float q = process_noise_;
    
    // Simplified process noise matrix
    float Q[16] = {
        q * dt4 / 4, 0,           q * dt3 / 2, 0,
        0,           q * dt4 / 4, 0,           q * dt3 / 2,
        q * dt3 / 2, 0,           q * dt2,     0,
        0,           q * dt3 / 2, 0,           q * dt2
    };
    
    // P = F*P*F^T + Q
    matrix_add_4x4(FPFT.data(), Q, covariance_.data());
    
    last_timestamp_ = timestamp;
}

void KalmanFilter2D::update(float x, float y, double timestamp) {
    if (!initialized_) {
        init(x, y, timestamp);
        return;
    }
    
    // First predict to current timestamp
    predict(timestamp);
    
    // Measurement matrix H: we only measure position
    // H = [1 0 0 0]
    //     [0 1 0 0]
    
    // Innovation (measurement residual): y = z - H*x
    float innovation[2] = {
        x - state_[0],
        y - state_[1]
    };
    
    // Innovation covariance: S = H * P * H^T + R
    // Since H selects first 2 rows/cols, S = P[0:2, 0:2] + R
    float S[4] = {
        covariance_[0] + measurement_noise_,  covariance_[1],
        covariance_[4],                        covariance_[5] + measurement_noise_
    };
    
    // Kalman gain: K = P * H^T * S^-1
    // P * H^T gives first 2 columns of P
    float PHT[8] = {
        covariance_[0], covariance_[1],
        covariance_[4], covariance_[5],
        covariance_[8], covariance_[9],
        covariance_[12], covariance_[13]
    };
    
    // S^-1 (2x2 matrix inverse)
    float S_inv[4];
    matrix_inverse_2x2(S, S_inv);
    
    // K = PHT * S_inv (4x2 * 2x2 = 4x2)
    float K[8];
    for (int i = 0; i < 4; i++) {
        K[i * 2 + 0] = PHT[i * 2 + 0] * S_inv[0] + PHT[i * 2 + 1] * S_inv[2];
        K[i * 2 + 1] = PHT[i * 2 + 0] * S_inv[1] + PHT[i * 2 + 1] * S_inv[3];
    }
    
    // Update state: x = x + K * innovation
    for (int i = 0; i < 4; i++) {
        state_[i] += K[i * 2 + 0] * innovation[0] + K[i * 2 + 1] * innovation[1];
    }
    
    // Update covariance: P = (I - K*H) * P
    // K*H is 4x4: K (4x2) * H (2x4)
    float KH[16];
    for (int i = 0; i < 4; i++) {
        // H = [1 0 0 0; 0 1 0 0]
        KH[i * 4 + 0] = K[i * 2 + 0];  // K[i,:] * H[:,0] = K[i,0]
        KH[i * 4 + 1] = K[i * 2 + 1];  // K[i,:] * H[:,1] = K[i,1]
        KH[i * 4 + 2] = 0.0f;
        KH[i * 4 + 3] = 0.0f;
    }
    
    // I - K*H
    float IminusKH[16];
    for (int i = 0; i < 16; i++) {
        IminusKH[i] = -KH[i];
    }
    IminusKH[0]  += 1.0f;
    IminusKH[5]  += 1.0f;
    IminusKH[10] += 1.0f;
    IminusKH[15] += 1.0f;
    
    // P = (I - K*H) * P
    std::array<float, 16> newP;
    matrix_multiply_4x4(IminusKH, covariance_.data(), newP.data());
    covariance_ = newP;
    
    last_timestamp_ = timestamp;
}

void KalmanFilter2D::get_predicted_position(double future_time, float& out_x, float& out_y) const {
    float dt = static_cast<float>(future_time - last_timestamp_);
    if (dt < 0.0f) dt = 0.0f;
    
    // Simple linear extrapolation using estimated velocity
    out_x = state_[0] + state_[2] * dt;
    out_y = state_[1] + state_[3] * dt;
}

float KalmanFilter2D::get_speed() const {
    return std::sqrt(state_[2] * state_[2] + state_[3] * state_[3]);
}

void KalmanFilter2D::matrix_multiply_4x4(const float* a, const float* b, float* result) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            result[i * 4 + j] = sum;
        }
    }
}

void KalmanFilter2D::matrix_add_4x4(const float* a, const float* b, float* result) {
    for (int i = 0; i < 16; i++) {
        result[i] = a[i] + b[i];
    }
}

float KalmanFilter2D::matrix_determinant_2x2(const float* m) {
    return m[0] * m[3] - m[1] * m[2];
}

void KalmanFilter2D::matrix_inverse_2x2(const float* m, float* result) {
    float det = matrix_determinant_2x2(m);
    if (std::abs(det) < 1e-10f) {
        // Nearly singular, use pseudo-inverse
        det = 1e-10f;
    }
    float inv_det = 1.0f / det;
    result[0] =  m[3] * inv_det;
    result[1] = -m[1] * inv_det;
    result[2] = -m[2] * inv_det;
    result[3] =  m[0] * inv_det;
}

// ============================================================================
// TargetPredictor Implementation
// ============================================================================

TargetPredictor::TargetPredictor()
    : enabled_(false)
    , prediction_strength_(0.5f)
    , lookahead_time_(0.016f)  // ~1 frame at 60fps
    , process_noise_(0.5f)
    , measurement_noise_(2.0f)
{
}

void TargetPredictor::update_target(
    int target_id,
    float measured_x,
    float measured_y,
    double timestamp,
    float& out_x,
    float& out_y
) {
    // Find or create tracker for this target
    auto it = trackers_.find(target_id);
    
    if (it == trackers_.end()) {
        // New target, create tracker
        TrackerEntry entry;
        entry.filter.set_process_noise(process_noise_);
        entry.filter.set_measurement_noise(measurement_noise_);
        entry.filter.init(measured_x, measured_y, timestamp);
        entry.last_seen = timestamp;
        entry.update_count = 1;
        trackers_[target_id] = entry;
        
        // First measurement, no prediction available
        out_x = measured_x;
        out_y = measured_y;
        return;
    }
    
    TrackerEntry& entry = it->second;
    
    // Update Kalman filter with new measurement
    entry.filter.update(measured_x, measured_y, timestamp);
    entry.last_seen = timestamp;
    entry.update_count++;
    
    // If prediction disabled or not enough updates, return measured position
    if (!enabled_ || entry.update_count < MIN_UPDATES_FOR_PREDICTION) {
        out_x = measured_x;
        out_y = measured_y;
        return;
    }
    
    // Get predicted position
    float pred_x, pred_y;
    double future_time = timestamp + lookahead_time_;
    entry.filter.get_predicted_position(future_time, pred_x, pred_y);
    
    // Blend between measured and predicted based on strength
    out_x = measured_x + (pred_x - measured_x) * prediction_strength_;
    out_y = measured_y + (pred_y - measured_y) * prediction_strength_;
}

bool TargetPredictor::get_predicted_position(
    int target_id,
    double lookahead_seconds,
    float& out_x,
    float& out_y
) const {
    auto it = trackers_.find(target_id);
    if (it == trackers_.end() || !it->second.filter.is_initialized()) {
        return false;
    }
    
    const TrackerEntry& entry = it->second;
    if (entry.update_count < MIN_UPDATES_FOR_PREDICTION) {
        return false;
    }
    
    double future_time = entry.last_seen + lookahead_seconds;
    entry.filter.get_predicted_position(future_time, out_x, out_y);
    return true;
}

void TargetPredictor::cleanup_stale_trackers(double current_time, double max_age) {
    for (auto it = trackers_.begin(); it != trackers_.end(); ) {
        if (current_time - it->second.last_seen > max_age) {
            it = trackers_.erase(it);
        } else {
            ++it;
        }
    }
}

void TargetPredictor::reset_tracker(int target_id) {
    trackers_.erase(target_id);
}

void TargetPredictor::reset_all() {
    trackers_.clear();
}

void TargetPredictor::set_process_noise(float noise) {
    process_noise_ = noise;
    for (auto& pair : trackers_) {
        pair.second.filter.set_process_noise(noise);
    }
}

void TargetPredictor::set_measurement_noise(float noise) {
    measurement_noise_ = noise;
    for (auto& pair : trackers_) {
        pair.second.filter.set_measurement_noise(noise);
    }
}

bool TargetPredictor::get_target_velocity(int target_id, float& vx, float& vy) const {
    auto it = trackers_.find(target_id);
    if (it == trackers_.end() || !it->second.filter.is_initialized()) {
        vx = 0.0f;
        vy = 0.0f;
        return false;
    }
    
    vx = it->second.filter.get_vx();
    vy = it->second.filter.get_vy();
    return true;
}

} // namespace catclicker
