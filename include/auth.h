#pragma once

#include <string>
#include <optional>

namespace catclicker {
namespace auth {

// Collect hardware ID (CPU + disk + MAC-based fingerprint)
std::string get_hwid();

// Get public IP via external service
std::optional<std::string> get_public_ip();

// Server-side verification: returns true if authorized
// auth_url: e.g. "https://yourserver.com/auth" or "http://localhost:5000/auth"
bool verify_access(const std::string& auth_url, const std::string& hwid, const std::string& ip);

// Combined: get hwid+ip and verify against server
// Returns true only if server grants access
bool authenticate(const std::string& auth_url);

} // namespace auth
} // namespace catclicker
