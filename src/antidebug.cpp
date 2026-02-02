#include "antidebug.h"
#include "common.h"
#include <windows.h>
#include <winternl.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "ntdll.lib")

extern "C" {
    NTSTATUS NTAPI NtQueryInformationProcess(HANDLE, ULONG, PVOID, ULONG, PULONG);
}

#ifndef ProcessDebugPort
#define ProcessDebugPort 7
#endif
#ifndef ProcessDebugObjectHandle
#define ProcessDebugObjectHandle 30
#endif
#ifndef ProcessDebugFlags
#define ProcessDebugFlags 31
#endif

namespace catclicker {
namespace antidebug {

static std::atomic<bool> g_monitor_running{false};

bool is_debugger_present() {
    // 1. IsDebuggerPresent (PEB)
    if (IsDebuggerPresent()) return true;
    
    // 2. CheckRemoteDebuggerPresent
    BOOL remote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote)
        return true;
    
    // 3. NtQueryInformationProcess - DebugPort
    DWORD_PTR debug_port = 0;
    if (NtQueryInformationProcess(GetCurrentProcess(), ProcessDebugPort,
            &debug_port, sizeof(debug_port), nullptr) == 0 && debug_port != 0)
        return true;
    
    // 4. NtQueryInformationProcess - DebugObjectHandle
    HANDLE debug_obj = nullptr;
    if (NtQueryInformationProcess(GetCurrentProcess(), ProcessDebugObjectHandle,
            &debug_obj, sizeof(debug_obj), nullptr) == 0 && debug_obj != nullptr) {
        CloseHandle(debug_obj);
        return true;
    }
    
    // 5. NtQueryInformationProcess - DebugFlags (NoDebugInherit)
    DWORD debug_flags = 0;
    if (NtQueryInformationProcess(GetCurrentProcess(), ProcessDebugFlags,
            &debug_flags, sizeof(debug_flags), nullptr) == 0 && debug_flags == 0)
        return true;  // 0 means being debugged
    
    // 6. Timing check - debugger single-stepping is slow
    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);
    volatile int x = 0;
    for (int i = 0; i < 10; i++) x += i;
    QueryPerformanceCounter(&t2);
    double elapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart * 1e6;
    if (elapsed > 2000.0) return true;  // >2ms suggests single-stepping
    
    return false;
}

void enforce_no_debugger() {
    if (is_debugger_present()) {
        ExitProcess(0xDEAD);
    }
}

static void monitor_thread() {
    while (g_monitor_running) {
        if (is_debugger_present()) {
            ExitProcess(0xDEAD);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

void start_anti_debug_monitor() {
    if (g_monitor_running.exchange(true)) return;
    std::thread(monitor_thread).detach();
}

} // namespace antidebug
} // namespace catclicker
