#pragma once

#include <cstdint>

namespace catclicker {
namespace antidebug {

// Call once at startup and periodically (e.g. every 30s) from a low-priority thread.
// Returns true if a debugger or memory-inspection tool is detected.
bool is_debugger_present();

// Optional: call from a background thread every interval_seconds. Exits process if detected.
void start_periodic_check(uint32_t interval_seconds);

// Stop the periodic check thread (call before shutdown).
void stop_periodic_check();

} // namespace antidebug
} // namespace catclicker
