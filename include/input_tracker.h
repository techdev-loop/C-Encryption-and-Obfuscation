#pragma once

#include "common.h"
#include "config.h"

namespace catclicker {

struct MovementState {
    bool strafing_left = false;
    bool strafing_right = false;
    bool jumping = false;
};

class InputTracker {
public:
    InputTracker(
        int strafe_left_key = config::DEFAULT_STRAFE_LEFT,
        int strafe_right_key = config::DEFAULT_STRAFE_RIGHT,
        int jump_key = config::DEFAULT_JUMP_KEY,
        int ads_key = config::DEFAULT_ADS_KEY
    );
    
    // Get current movement state
    MovementState get_movement_state() const;
    
    // Check if ADS is active
    bool is_ads_active() const;
    
    // Check if trigger key is held
    bool is_trigger_active(int trigger_key) const;
    
    // Update keybinds
    void set_strafe_left_key(int key) { strafe_left_key_ = key; }
    void set_strafe_right_key(int key) { strafe_right_key_ = key; }
    void set_jump_key(int key) { jump_key_ = key; }
    void set_ads_key(int key) { ads_key_ = key; }
    
    // Get current keybinds
    int get_strafe_left_key() const { return strafe_left_key_; }
    int get_strafe_right_key() const { return strafe_right_key_; }
    int get_jump_key() const { return jump_key_; }
    int get_ads_key() const { return ads_key_; }

private:
    static bool is_key_pressed(int vk_code);
    
    int strafe_left_key_;
    int strafe_right_key_;
    int jump_key_;
    int ads_key_;
};

// Calculate adaptive sensitivity based on movement and ADS
struct AdaptiveScale {
    float scale_x;
    float scale_y;
};

AdaptiveScale calculate_adaptive_scale(
    float target_x,
    float target_y,
    float center_x,
    float center_y,
    float base_scale,
    const InputTracker& input_tracker,
    float movement_boost = 0.5f,
    bool ads_active = false,
    float ads_multiplier = 1.5f
);

} // namespace catclicker
