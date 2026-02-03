#pragma once

#include <string>

namespace catclicker {
namespace hwid {

// Builds a stable hardware ID (volume serial + computer name + processor info).
// Safe to call from any thread. Returns empty string on failure.
std::string get_hwid();

} // namespace hwid
} // namespace catclicker
