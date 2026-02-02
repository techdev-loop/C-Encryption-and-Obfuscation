#include "target_selector.h"
#include <limits>
#include <cmath>

namespace catclicker {

TargetSelector::TargetSelector(float head_ratio, float hysteresis, float min_distance)
    : head_ratio_(head_ratio)
    , hysteresis_(hysteresis)
    , min_distance_(min_distance)
{}

void TargetSelector::reset() {
    has_locked_target_ = false;
    last_target_x_ = 0.0f;
    last_target_y_ = 0.0f;
}

Target TargetSelector::get_closest_target(
    const std::vector<Detection>& detections,
    const ScreenRegion& detection_box,
    int screen_center_x,
    int screen_center_y,
    float min_confidence
) {
    if (detections.empty()) {
        // No detections - lose lock
        has_locked_target_ = false;
        return invalid_target();
    }
    
    int box_left = detection_box.left;
    int box_top = detection_box.top;
    int box_size = detection_box.width();
    float frame_center = static_cast<float>(box_size) / 2.0f;
    
    // Squared thresholds
    float min_distance_sq = min_distance_ * min_distance_;
    
    // Find: 
    // 1. The closest target to screen center (potential new target)
    // 2. The target closest to our last locked position (current target continuation)
    
    Target closest_to_center = invalid_target();
    float closest_to_center_dist_sq = std::numeric_limits<float>::max();
    
    Target closest_to_last = invalid_target();
    float closest_to_last_dist_sq = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < detections.size(); i++) {
        const Detection& det = detections[i];
        
        if (det.confidence < min_confidence) {
            continue;
        }
        
        float height = det.height();
        
        // Calculate head position (relative to capture frame)
        float relative_head_x = det.center_x();
        float relative_head_y = det.center_y() - height / head_ratio_;
        
        // Calculate absolute screen position
        float absolute_head_x = relative_head_x + box_left;
        float absolute_head_y = relative_head_y + box_top;
        
        // Distance from frame center (squared)
        float dx_center = relative_head_x - frame_center;
        float dy_center = relative_head_y - frame_center;
        float dist_to_center_sq = dx_center * dx_center + dy_center * dy_center;
        
        // Skip targets too close to center (already on target)
        if (dist_to_center_sq < min_distance_sq) {
            continue;
        }
        
        // Build target data
        Target target_data;
        target_data.id = static_cast<int>(i);
        target_data.absolute_x = absolute_head_x;
        target_data.absolute_y = absolute_head_y;
        target_data.relative_x = relative_head_x;
        target_data.relative_y = relative_head_y;
        target_data.confidence = det.confidence;
        target_data.distance = std::sqrt(dist_to_center_sq);
        target_data.detection = det;
        
        // Track closest to center
        if (dist_to_center_sq < closest_to_center_dist_sq) {
            closest_to_center_dist_sq = dist_to_center_sq;
            closest_to_center = target_data;
        }
        
        // If we have a locked target, track which detection is closest to it
        if (has_locked_target_) {
            float dx_last = relative_head_x - last_target_x_;
            float dy_last = relative_head_y - last_target_y_;
            float dist_to_last_sq = dx_last * dx_last + dy_last * dy_last;
            
            // Only consider it if within lock radius
            if (dist_to_last_sq < lock_radius_sq_ && dist_to_last_sq < closest_to_last_dist_sq) {
                closest_to_last_dist_sq = dist_to_last_sq;
                closest_to_last = target_data;
            }
        }
    }
    
    // Decision logic:
    // 1. If we find our locked target (within lock radius), stick with it unless something is clearly closer to center
    // 2. If locked target "teleports" (not found within lock radius), pick the detection closest to our LAST AIM POINT
    //    This prevents snapping to a different target just because IDs flickered
    // 3. Only use closest_to_center if we have no lock history
    
    Target selected = invalid_target();
    
    if (has_locked_target_) {
        if (closest_to_last.valid()) {
            // Found our target (within lock radius)
            if (closest_to_center.valid()) {
                // Check if closest_to_center is significantly closer
                float dx = closest_to_last.relative_x - frame_center;
                float dy = closest_to_last.relative_y - frame_center;
                float current_dist_sq = dx * dx + dy * dy;
                
                float ratio = closest_to_center_dist_sq / (current_dist_sq + 0.001f);
                
                if (ratio >= 0.99f) {
                    selected = closest_to_last;
                } else {
                    selected = closest_to_center;
                }
            } else {
                selected = closest_to_last;
            }
        } else {
            // Locked target not found within lock radius (teleported/disappeared)
            // Find detection closest to where we WERE aiming (last_target position)
            // This prevents snapping to a completely different target
            
            Target closest_to_aim = invalid_target();
            float closest_to_aim_dist_sq = std::numeric_limits<float>::max();
            
            for (size_t i = 0; i < detections.size(); i++) {
                const Detection& det = detections[i];
                if (det.confidence < min_confidence) continue;
                
                float height = det.height();
                float rel_x = det.center_x();
                float rel_y = det.center_y() - height / head_ratio_;
                
                float dx = rel_x - last_target_x_;
                float dy = rel_y - last_target_y_;
                float dist_sq = dx * dx + dy * dy;
                
                if (dist_sq < closest_to_aim_dist_sq) {
                    closest_to_aim_dist_sq = dist_sq;
                    
                    closest_to_aim.id = static_cast<int>(i);
                    closest_to_aim.relative_x = rel_x;
                    closest_to_aim.relative_y = rel_y;
                    closest_to_aim.absolute_x = rel_x + box_left;
                    closest_to_aim.absolute_y = rel_y + box_top;
                    closest_to_aim.confidence = det.confidence;
                    closest_to_aim.distance = std::sqrt(dist_sq);
                    closest_to_aim.detection = det;
                }
            }
            
            if (closest_to_aim.valid()) {
                selected = closest_to_aim;
            } else {
                selected = closest_to_center;
            }
        }
    } else {
        // No lock history - just pick closest to center
        selected = closest_to_center;
    }
    
    // Update lock state (store RELATIVE coordinates)
    if (selected.valid()) {
        has_locked_target_ = true;
        last_target_x_ = selected.relative_x;
        last_target_y_ = selected.relative_y;
    } else {
        has_locked_target_ = false;
    }
    
    return selected;
}

bool TargetSelector::is_target_locked(
    float target_x,
    float target_y,
    float center_x,
    float center_y,
    float threshold
) {
    float dx = target_x - center_x;
    float dy = target_y - center_y;
    float dist_sq = dx * dx + dy * dy;
    return dist_sq <= threshold * threshold;
}

} // namespace catclicker
