#include "hwid.h"
#include "common.h"
#include <windows.h>
#include <intrin.h>
#include <sstream>
#include <iomanip>
#include <array>

namespace catclicker {
namespace hwid {

namespace {

std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)data[i];
    return ss.str();
}

// Simple FNV-1a 64-bit for combining strings into a hash
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string volume_serial() {
    wchar_t path[] = L"C:\\";
    DWORD serial = 0, maxLen = 0, flags = 0;
    if (!GetVolumeInformationW(path, nullptr, 0, &serial, &maxLen, &flags, nullptr, 0))
        return "";
    return std::to_string(serial);
}

std::string computer_name() {
    std::array<wchar_t, 256> buf{};
    DWORD n = (DWORD)buf.size();
    if (!GetComputerNameW(buf.data(), &n))
        return "";
    std::string out;
    for (DWORD i = 0; i < n && buf[i]; ++i)
        out += (char)(buf[i] & 0xFF);
    return out;
}

// CPU vendor + highest leaf for stability
std::string cpu_id_string() {
    int regs[4];
    __cpuid(regs, 0);
    std::string s;
    for (int i = 1; i <= 3; ++i) {
        for (int j = 0; j < 4; ++j)
            s += (char)((regs[i] >> (j * 8)) & 0xFF);
    }
    __cpuid(regs, 1);
    s += std::to_string(regs[0]);
    return s;
}

} // namespace

std::string get_hwid() {
    std::string a = volume_serial();
    std::string b = computer_name();
    std::string c = cpu_id_string();
    if (a.empty() && b.empty() && c.empty())
        return "";
    uint64_t h = fnv1a(a + "|" + b + "|" + c);
    uint64_t h2 = h * 0x9e3779b97f4a7c15ULL;
    h2 ^= h2 >> 33;
    h2 *= 0xff51afd7ed558ccdULL;
    h2 ^= h2 >> 33;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << h << std::setw(16) << h2;
    return ss.str();
}

} // namespace hwid
} // namespace catclicker
