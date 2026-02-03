#include "antidebug.h"
#include "common.h"
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

// NtQueryInformationProcess loaded via GetProcAddress(ntdll) - no lib link needed

namespace catclicker {
namespace antidebug {

namespace {

typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(
    HANDLE ProcessHandle, ULONG ProcessInformationClass,
    PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength);

#define ProcessDebugPort 7
#define ProcessDebugObjectHandle 30
#define ProcessDebugFlags 31

static std::atomic<bool> g_periodic_stop{false};
static std::thread g_periodic_thread;

bool check_is_debugger_present() {
    return IsDebuggerPresent() != 0;
}

bool check_remote_debugger() {
    BOOL remote = FALSE;
    return CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote;
}

bool check_nt_debug_port() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto NtQIP = (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!NtQIP) return false;
    DWORD_PTR debug_port = 0;
    NTSTATUS st = NtQIP(GetCurrentProcess(), ProcessDebugPort, &debug_port, sizeof(debug_port), nullptr);
    if (st != 0) return false;
    return debug_port != 0;
}

bool check_nt_debug_object() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto NtQIP = (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!NtQIP) return false;
    DWORD_PTR handle = 0;
    NTSTATUS st = NtQIP(GetCurrentProcess(), ProcessDebugObjectHandle, &handle, sizeof(handle), nullptr);
    if (st != 0) return false;
    return handle != 0;
}

bool check_nt_debug_flags() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto NtQIP = (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!NtQIP) return false;
    DWORD_PTR flags = 0;
    NTSTATUS st = NtQIP(GetCurrentProcess(), ProcessDebugFlags, &flags, sizeof(flags), nullptr);
    if (st != 0) return false;
    return flags == 0; // 0 means being debugged
}

bool check_debugger_windows() {
    const wchar_t* titles[] = {
        L"x64dbg", L"x32dbg", L"WinDbg", L"OllyDbg", L"IDA", L"Cheat Engine",
        L"Process Hacker", L"Process Explorer", L"API Monitor", L"Immunity",
        L"dnSpy", L"de4dot", L"x64dbg -", L"IDA -", L"Cheat Engine 7"
    };
    for (const wchar_t* t : titles) {
        if (FindWindowW(nullptr, t) != nullptr)
            return true;
    }
    return false;
}

bool check_debugger_processes() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = pe.szExeFile;
            for (auto& c : name) if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
            if (name.find(L"x64dbg") != std::wstring::npos ||
                name.find(L"x32dbg") != std::wstring::npos ||
                name.find(L"windbg") != std::wstring::npos ||
                name.find(L"ollydbg") != std::wstring::npos ||
                name.find(L"ida") != std::wstring::npos ||
                name.find(L"cheatengine") != std::wstring::npos ||
                name.find(L"procmon") != std::wstring::npos ||
                name.find(L"procexp") != std::wstring::npos ||
                name.find(L"dnspy") != std::wstring::npos ||
                name.find(L"de4dot") != std::wstring::npos) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Simple timing check: two quick operations; if elapsed is too large, likely single-stepping
bool check_timing() {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    volatile int x = 0;
    for (int i = 0; i < 10; ++i) x += i;
    QueryPerformanceCounter(&end);
    double elapsed_us = 1e6 * (end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
    return elapsed_us > 1000.0; // > 1 ms for trivial loop is suspicious
}

} // namespace

bool is_debugger_present() {
    if (check_is_debugger_present()) return true;
    if (check_remote_debugger()) return true;
    if (check_nt_debug_port()) return true;
    if (check_nt_debug_object()) return true;
    if (check_nt_debug_flags()) return true;
    if (check_debugger_windows()) return true;
    if (check_debugger_processes()) return true;
    if (check_timing()) return true;
    return false;
}

static void periodic_loop(uint32_t interval_seconds) {
    while (!g_periodic_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        if (g_periodic_stop) break;
        if (is_debugger_present())
            ExitProcess(1);
    }
}

void start_periodic_check(uint32_t interval_seconds) {
    if (g_periodic_thread.joinable()) return;
    g_periodic_stop = false;
    g_periodic_thread = std::thread(periodic_loop, interval_seconds > 0 ? interval_seconds : 30);
}

void stop_periodic_check() {
    g_periodic_stop = true;
    if (g_periodic_thread.joinable()) {
        g_periodic_thread.join();
    }
}

} // namespace antidebug
} // namespace catclicker
