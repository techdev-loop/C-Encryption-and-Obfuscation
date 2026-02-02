#pragma once

// Optional: hide process from Task Manager by renaming/unlisting.
// Grey area - use responsibly. Disabled by default.
namespace catclicker {
namespace stealth {

// Attempt to reduce visibility in task manager (optional)
void hide_from_task_manager();

// Call early in main if stealth is desired
void init_stealth(bool enable);

} // namespace stealth
} // namespace catclicker
