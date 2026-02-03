#include "license_client.h"
#include "common.h"
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

namespace catclicker {
namespace license {

namespace {

std::string g_auth_base_url = "https://auth.example.com";

const wchar_t* REG_PATH = L"Software\\CatClicker";
const wchar_t* REG_VALUE = L"Token";
static const unsigned char XOR_KEY[] = { 0x9A, 0x3F, 0xC2, 0x71, 0xE5, 0xB8, 0x4D, 0x1E };
static const size_t XOR_KEY_LEN = sizeof(XOR_KEY);

std::string xor_obfuscate(const std::string& s, bool encode) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (char)((unsigned char)out[i] ^ XOR_KEY[i % XOR_KEY_LEN]);
    return out;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find_first_of("\"", pos);
    if (pos == std::string::npos) return "";
    ++pos;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    std::string value = json.substr(pos, end - pos);
    // Unescape simple \"
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '"') { out += '"'; ++i; continue; }
        out += value[i];
    }
    return out;
}

bool extract_json_bool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return false;
    return json.compare(pos, 4, "true") == 0;
}

void escape_json_append(std::string& out, const std::string& s) {
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c >= 32 && c < 127) out += c;
        else { out += "\\u"; char buf[5]; snprintf(buf, sizeof(buf), "%04x", c); out += buf; }
    }
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Parse URL into host, path, use_https
bool parse_url(const std::string& url, std::string& host, std::string& path, bool& use_https) {
    use_https = (url.size() >= 8 && (url.substr(0, 8) == "https://" || url.substr(0, 8) == "HTTPS://"));
    size_t start = (url.size() >= 8 && (url.substr(0, 7) == "http://" || url.substr(0, 7) == "HTTP://" || url.substr(0, 8) == "https://" || url.substr(0, 8) == "HTTPS://"))
        ? (url.substr(0, 7) == "http://" || url.substr(0, 7) == "HTTP://" ? 7u : 8u) : 0;
    size_t slash = url.find('/', start);
    if (slash == std::string::npos) {
        host = url.substr(start);
        path = "/";
    } else {
        host = url.substr(start, slash - start);
        path = url.substr(slash);
    }
    if (path.empty()) path = "/";
    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        // host includes port; WinHTTP needs host and port separate
        // Keep host as "host:port" for simplicity; we'll split if needed
    }
    return !host.empty();
}

int get_default_port(bool use_https) { return use_https ? 443 : 80; }

bool http_post_json(const std::string& base_url, const std::string& path_suffix,
    const std::string& json_body, std::string& out_response) {
    std::string host, path;
    bool use_https = false;
    if (!parse_url(base_url, host, path, use_https)) return false;
    if (!path.empty() && path.back() != '/' && !path_suffix.empty() && path_suffix[0] != '/')
        path += '/';
    path += path_suffix;
    if (!path.empty() && path[0] != '/') path = "/" + path;

    std::wstring whost = utf8_to_wide(host);
    int port = get_default_port(use_https);
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        whost = utf8_to_wide(host.substr(0, colon));
    }

    HINTERNET session = WinHttpOpen(L"CatClicker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) return false;

    HINTERNET connect = WinHttpConnect(session, whost.c_str(), (INTERNET_PORT)port, 0);
    if (!connect) { WinHttpCloseHandle(session); return false; }

    DWORD flags = use_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", utf8_to_wide(path).c_str(), nullptr, nullptr, nullptr, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

    if (use_https) {
        // For production with valid TLS cert, remove the IGNORE flags below to enforce verification.
        DWORD secure_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &secure_flags, sizeof(secure_flags));
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!WinHttpAddRequestHeaders(request, headers.c_str(), (DWORD)headers.size(), WINHTTP_ADDREQ_FLAG_ADD)) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        return false;
    }

    BOOL sent = WinHttpSendRequest(request, nullptr, 0, (LPVOID)json_body.data(), (DWORD)json_body.size(), (DWORD)json_body.size(), 0);
    if (!sent) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }
    if (!WinHttpReceiveResponse(request, nullptr)) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

    out_response.clear();
    DWORD size = 0;
    do {
        WinHttpQueryDataAvailable(request, &size);
        if (size == 0) break;
        std::vector<char> buf(size);
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), size, &read) || read == 0) break;
        out_response.append(buf.data(), read);
    } while (size > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

} // namespace

void set_auth_base_url(const std::string& url) { g_auth_base_url = url; }
std::string get_auth_base_url() { return g_auth_base_url; }

void store_token(const std::string& token) {
    if (token.empty()) { clear_token(); return; }
    std::string obf = xor_obfuscate(token, true);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, REG_VALUE, 0, REG_BINARY, (const BYTE*)obf.data(), (DWORD)obf.size());
    RegCloseKey(key);
}

std::string load_token() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return "";
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, REG_VALUE, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        RegCloseKey(key);
        return "";
    }
    std::vector<char> buf(size);
    if (RegQueryValueExW(key, REG_VALUE, nullptr, &type, (LPBYTE)buf.data(), &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return "";
    }
    RegCloseKey(key);
    std::string obf(buf.data(), buf.size());
    return xor_obfuscate(obf, false);
}

void clear_token() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, REG_VALUE);
    RegCloseKey(key);
}

LicenseResult login_and_bind(const std::string& email, const std::string& password,
                              const std::string& hwid, const std::string& ip) {
    LicenseResult r;
    std::string body = "{\"email\":\""; escape_json_append(body, email); body += "\",\"password\":\""; escape_json_append(body, password); body += "\"}";
    std::string resp;
    if (!http_post_json(g_auth_base_url, "api/auth/login", body, resp)) {
        r.error_message = "Cannot reach license server. Check internet and try again.";
        r.need_login = true;
        return r;
    }
    bool ok = extract_json_bool(resp, "ok");
    if (!ok) {
        r.error_message = extract_json_string(resp, "error");
        if (r.error_message.empty()) r.error_message = "Login failed.";
        r.need_login = true;
        return r;
    }
    std::string token = extract_json_string(resp, "token");
    if (token.empty()) {
        r.error_message = "No token received.";
        r.need_login = true;
        return r;
    }
    store_token(token);
    LicenseResult bind_r = bind_device(token, hwid, ip);
    if (!bind_r.success) {
        clear_token();
        return bind_r;
    }
    r.success = true;
    return r;
}

LicenseResult validate_session(const std::string& hwid, const std::string& ip) {
    LicenseResult r;
    std::string token = load_token();
    if (token.empty()) {
        r.need_login = true;
        r.error_message = "Not logged in. Please log in to continue.";
        return r;
    }
    std::string body = "{\"token\":\""; escape_json_append(body, token); body += "\",\"hwid\":\""; escape_json_append(body, hwid); body += "\",\"ip\":\""; escape_json_append(body, ip); body += "\"}";
    std::string resp;
    if (!http_post_json(g_auth_base_url, "api/auth/validate", body, resp)) {
        r.error_message = "Cannot reach license server. Check internet and try again.";
        r.need_login = false;
        return r;
    }
    bool ok = extract_json_bool(resp, "ok");
    if (!ok) {
        r.error_message = extract_json_string(resp, "error");
        if (r.error_message.empty()) r.error_message = "License validation failed.";
        if (r.error_message.find("expired") != std::string::npos || r.error_message.find("Invalid") != std::string::npos)
            r.need_login = true;
        return r;
    }
    r.success = true;
    return r;
}

LicenseResult bind_device(const std::string& token, const std::string& hwid, const std::string& ip) {
    LicenseResult r;
    std::string body = "{\"token\":\""; escape_json_append(body, token); body += "\",\"hwid\":\""; escape_json_append(body, hwid); body += "\",\"ip\":\""; escape_json_append(body, ip); body += "\"}";
    std::string resp;
    if (!http_post_json(g_auth_base_url, "api/auth/bind", body, resp)) {
        r.error_message = "Cannot reach license server. Check internet and try again.";
        return r;
    }
    bool ok = extract_json_bool(resp, "ok");
    if (!ok) {
        r.error_message = extract_json_string(resp, "error");
        if (r.error_message.empty()) r.error_message = "Device binding failed.";
        return r;
    }
    r.success = true;
    return r;
}

} // namespace license
} // namespace catclicker
