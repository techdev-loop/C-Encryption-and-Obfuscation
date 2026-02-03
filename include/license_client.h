#pragma once

#include <string>
#include <cstdint>

namespace catclicker {
namespace license {

struct LicenseResult {
    bool success = false;
    std::string error_message;
    bool need_login = false;  // true if token missing/expired and user should log in again
};

// Base URL for auth API (e.g. "https://auth.yourdomain.com" or "http://127.0.0.1:5000")
void set_auth_base_url(const std::string& url);
std::string get_auth_base_url();

// Token storage (encrypted in registry under HKCU). Returns empty if none stored.
void store_token(const std::string& token);
std::string load_token();
void clear_token();

// Login with email/password. On success, token is stored and device is bound.
LicenseResult login_and_bind(const std::string& email, const std::string& password,
                             const std::string& hwid, const std::string& ip);

// Validate current machine (HWID + optional IP) using stored token. Call on every startup.
// If no token or invalid, need_login is set and error_message describes the issue.
LicenseResult validate_session(const std::string& hwid, const std::string& ip);

// Optional: bind only (e.g. after first login from another module that already has token).
LicenseResult bind_device(const std::string& token, const std::string& hwid, const std::string& ip);

} // namespace license
} // namespace catclicker
