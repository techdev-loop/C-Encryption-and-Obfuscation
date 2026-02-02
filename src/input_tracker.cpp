#include "input_tracker.h"

namespace catclicker {

InputTracker::InputTracker(int strafe_left_key, int strafe_right_key, int jump_key, int ads_key)
    : strafe_left_key_(strafe_left_key)
    , strafe_right_key_(strafe_right_key)
    , jump_key_(jump_key)
    , ads_key_(ads_key)
{}

bool InputTracker::is_key_pressed(int vk_code) {
    return (GetAsyncKeyState(vk_code) & 0x8000) != 0;
}

MovementState InputTracker::get_movement_state() const {
    MovementState state;
    state.strafing_left = is_key_pressed(strafe_left_key_);
    state.strafing_right = is_key_pressed(strafe_right_key_);
    state.jumping = is_key_pressed(jump_key_);
    return state;
}

bool InputTracker::is_ads_active() const {
    return is_key_pressed(ads_key_);
}

bool InputTracker::is_trigger_active(int trigger_key) const {
    return is_key_pressed(trigger_key);
}

AdaptiveScale calculate_adaptive_scale(
    float target_x,
    float target_y,
    float center_x,
    float center_y,
    float base_scale,
    const InputTracker& input_tracker,
    float movement_boost,
    bool ads_active,
    float ads_multiplier
) {
    MovementState movement = input_tracker.get_movement_state();
    
    // Apply ADS multiplier to base scale
    float effective_base = ads_active ? base_scale * ads_multiplier : base_scale;
    
    float scale_x = effective_base;
    float scale_y = effective_base;
    
    // Target direction
    bool target_is_right = target_x > center_x;
    bool target_is_left = target_x < center_x;
    bool target_is_below = target_y > center_y;
    
    // Horizontal compensation: boost when strafing opposite to target direction
    if (target_is_right && movement.strafing_left) {
        scale_x = effective_base + movement_boost;
    } else if (target_is_left && movement.strafing_right) {
        scale_x = effective_base + movement_boost;
    }
    
    // Vertical compensation: boost when jumping and target is below
    if (movement.jumping && target_is_below) {
        scale_y = effective_base + movement_boost;
    }
    
    // Clamp to reasonable range
    float max_scale = effective_base * 2.5f;
    float min_scale = effective_base * 0.5f;
    
    scale_x = clamp(scale_x, min_scale, max_scale);
    scale_y = clamp(scale_y, min_scale, max_scale);
    
    return {scale_x, scale_y};
}

} // namespace catclicker
