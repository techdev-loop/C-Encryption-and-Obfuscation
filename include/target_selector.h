#pragma once

#include "common.h"
#include "frame_grabber.h"
#include <vector>

namespace catclicker {

class TargetSelector {
public:
    TargetSelector(float head_ratio = 2.7f, float hysteresis = 0.75f, float min_distance = 2.0f);
    
    // Find the best target from detections
    // Uses position-based lock retention to prevent flickering
    Target get_closest_target(
        const std::vector<Detection>& detections,
        const ScreenRegion& detection_box,
        int screen_center_x,
        int screen_center_y,
        float min_confidence
    );
    
    // Check if target is within lock threshold
    static bool is_target_locked(
        float target_x,
        float target_y,
        float center_x,
        float center_y,
        float threshold
    );
    
    // Update settings
    void set_head_ratio(float ratio) { head_ratio_ = ratio; }
    void set_hysteresis(float hysteresis) { hysteresis_ = hysteresis; }
    void set_min_distance(float distance) { min_distance_ = distance; }
    
    // Set the lock retention radius (how close a new detection must be to previous target to be considered the same)
    void set_lock_radius(float radius) { lock_radius_sq_ = radius * radius; }
    
    // Reset target tracking (e.g., when trigger released)
    void reset();

private:
    float head_ratio_;
    float hysteresis_;
    float min_distance_;
    
    // Lock retention: store last locked target position (RELATIVE to frame center)
    float last_target_x_ = 0.0f;
    float last_target_y_ = 0.0f;
    bool has_locked_target_ = false;
    
    // Lock radius squared (for fast comparison)
    // If a detection is within this distance of the last target, consider it the same target
    float lock_radius_sq_ = 81.0f;  // 50 pixels default
};

} // namespace catclicker
