#pragma once

#include <cstdint>

namespace catclicker {
namespace antidebug {

// Run all anti-debug checks. Returns true if debugger/tampering detected.
bool is_debugger_present();

// Call this at startup and periodically. Exits process if debugger detected.
void enforce_no_debugger();

// Start background thread that periodically checks (every ~500ms)
void start_anti_debug_monitor();

} // namespace antidebug
} // namespace catclicker
