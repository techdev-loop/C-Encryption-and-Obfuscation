#include "auth.h"
#include "common.h"
#include <winhttp.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <sstream>
#include <iomanip>
#include <array>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace catclicker {
namespace auth {

static std::string xor_decrypt(const char* enc, size_t len, uint8_t key) {
    std::string out;
    out.resize(len);
    for (size_t i = 0; i < len; i++) out[i] = enc[i] ^ (key + (uint8_t)i);
    return out;
}

// Obfuscated string: "api.ipify.org"
static const char s_ipify_host[] = {
    'a'^0x21, 'p'^0x22, 'i'^0x23, '.'^0x24, 'i'^0x25, 'p'^0x26, 'i'^0x27,
    'f'^0x28, 'y'^0x29, '.'^0x2a, 'o'^0x2b, 'r'^0x2c, 'g'^0x2d, 0^0x2e
};
static constexpr size_t s_ipify_len = sizeof(s_ipify_host) - 1;

std::string get_hwid() {
    std::stringstream ss;
    
    // CPU ID
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    ss << std::hex << std::setfill('0')
       << std::setw(8) << cpu_info[0] << std::setw(8) << cpu_info[1]
       << std::setw(8) << cpu_info[2] << std::setw(8) << cpu_info[3];
    
    // Volume serial (C: drive)
    DWORD vol_serial = 0;
    if (GetVolumeInformationA("C:\\", nullptr, 0, &vol_serial, nullptr, nullptr, nullptr, 0)) {
        ss << std::setw(8) << vol_serial;
    }
    
    // MAC address (first non-null adapter)
    ULONG buf_len = sizeof(IP_ADAPTER_INFO) * 16;
    std::vector<char> buf(buf_len);
    PIP_ADAPTER_INFO adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
    
    if (GetAdaptersInfo(adapters, &buf_len) == NO_ERROR) {
        for (PIP_ADAPTER_INFO a = adapters; a; a = a->Next) {
            if (a->Type == MIB_IF_TYPE_ETHERNET || a->Type == IF_TYPE_IEEE80211) {
                for (UINT i = 0; i < a->AddressLength; i++) {
                    ss << std::setw(2) << (int)a->Address[i];
                }
                break;
            }
        }
    }
    
    std::string raw = ss.str();
    // Simple hash to produce fixed-length fingerprint
    uint32_t h = 0x811c9dc5u;
    for (char c : raw) h = (h ^ (uint8_t)c) * 0x01000193u;
    return std::to_string(h) + "-" + raw.substr(0, 24);
}

std::optional<std::string> get_public_ip() {
    std::string host_str = xor_decrypt(s_ipify_host, s_ipify_len, 0x21);
    
    HINTERNET hSession = WinHttpOpen(L"CatClicker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return std::nullopt;
    
    std::wstring whost(host_str.begin(), host_str.end());
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }
    
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }
    
    DWORD size = 0;
    WinHttpQueryDataAvailable(hRequest, &size);
    std::string ip;
    if (size > 0 && size < 64) {
        ip.resize(size);
        DWORD read = 0;
        if (WinHttpReadData(hRequest, ip.data(), size, &read)) {
            ip.resize(read);
            // Trim whitespace
            while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n')) ip.pop_back();
        } else {
            ip.clear();
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    if (ip.empty()) return std::nullopt;
    return ip;
}

bool verify_access(const std::string& auth_url, const std::string& hwid, const std::string& ip) {
    if (auth_url.empty()) return true;  // No server = bypass for dev
    
    // Parse URL
    size_t proto_end = auth_url.find("://");
    std::string host, path;
    int port = 443;
    bool https = true;
    
    if (proto_end != std::string::npos) {
        std::string proto = auth_url.substr(0, proto_end);
        https = (proto == "https");
        port = https ? 443 : 80;
        
        size_t path_start = auth_url.find('/', proto_end + 3);
        if (path_start != std::string::npos) {
            host = auth_url.substr(proto_end + 3, path_start - proto_end - 3);
            path = auth_url.substr(path_start);
        } else {
            host = auth_url.substr(proto_end + 3);
            path = "/auth";
        }
        
        size_t colon = host.find(':');
        if (colon != std::string::npos) {
            port = std::stoi(host.substr(colon + 1));
            host = host.substr(0, colon);
        }
    } else {
        host = auth_url;
        path = "/auth";
    }
    
    auto url_enc = [](const std::string& s) {
        std::string r;
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.') r += (char)c;
            else { r += '%'; r += hex[c>>4]; r += hex[c&15]; }
        }
        return r;
    };
    std::string query = "?hwid=" + url_enc(hwid) + "&ip=" + url_enc(ip);
    
    std::wstring whost(host.begin(), host.end());
    std::wstring wpath(path.begin(), path.end());
    std::wstring wquery(query.begin(), query.end());
    std::wstring full_path = wpath + wquery;
    
    HINTERNET hSession = WinHttpOpen(L"CatClicker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", full_path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    if (https) WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    DWORD total = 0;
    std::string body;
    do {
        DWORD size = 0;
        WinHttpQueryDataAvailable(hRequest, &size);
        if (size == 0) break;
        size_t old_len = body.size();
        body.resize(old_len + size);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, &body[old_len], size, &read)) break;
        body.resize(old_len + read);
        total += read;
    } while (total < 256);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Server returns "OK" or "1" or "true" = authorized
    if (body.find("OK") != std::string::npos ||
        body.find("\"ok\":true") != std::string::npos ||
        body.find("\"authorized\":true") != std::string::npos ||
        body.find("1") == 0) {
        return true;
    }
    return false;
}

bool authenticate(const std::string& auth_url) {
    if (auth_url.empty()) return true;
    
    std::string hwid = get_hwid();
    auto ip_opt = get_public_ip();
    std::string ip = ip_opt ? *ip_opt : "0.0.0.0";
    
    return verify_access(auth_url, hwid, ip);
}

} // namespace auth
} // namespace catclicker
