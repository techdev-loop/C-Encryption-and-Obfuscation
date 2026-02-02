#include "priority_manager.h"
#include <sstream>
#include <array>
#include <memory>

namespace catclicker {

// Global instance
static PriorityManager g_priority_manager;

PriorityManager& get_priority_manager() {
    return g_priority_manager;
}

PriorityManager::PriorityManager() = default;

PriorityManager::~PriorityManager() {
    if (initialized_) {
        cleanup();
    }
}

bool PriorityManager::initialize() {
    if (initialized_) return true;
    
    topology_ = detect_cpu_topology();
    core_assignment_ = calculate_core_assignment(topology_);
    maximize_process_priority();
    
    initialized_ = true;
    return true;
}

CpuTopology PriorityManager::detect_cpu_topology() {
    CpuTopology topology;
    
    // Get system info
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    topology.total_logical = sysinfo.dwNumberOfProcessors;
    
    // Try to get CPU name via registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        char cpu_name[256] = {0};
        DWORD size = sizeof(cpu_name);
        if (RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr,
            (LPBYTE)cpu_name, &size) == ERROR_SUCCESS) {
            topology.cpu_name = cpu_name;
        }
        RegCloseKey(hKey);
    }
    
    // Check for Intel hybrid CPU (12th/13th/14th Gen)
    bool is_hybrid = false;
    if (topology.cpu_name.find("12th Gen") != std::string::npos ||
        topology.cpu_name.find("13th Gen") != std::string::npos ||
        topology.cpu_name.find("14th Gen") != std::string::npos ||
        topology.cpu_name.find("i5-12") != std::string::npos ||
        topology.cpu_name.find("i7-12") != std::string::npos ||
        topology.cpu_name.find("i9-12") != std::string::npos ||
        topology.cpu_name.find("i5-13") != std::string::npos ||
        topology.cpu_name.find("i7-13") != std::string::npos ||
        topology.cpu_name.find("i9-13") != std::string::npos ||
        topology.cpu_name.find("i5-14") != std::string::npos ||
        topology.cpu_name.find("i7-14") != std::string::npos ||
        topology.cpu_name.find("i9-14") != std::string::npos) {
        is_hybrid = true;
    }
    
    topology.is_hybrid = is_hybrid;
    
    if (is_hybrid) {
        // Heuristic for hybrid CPU core layout
        // P-cores have hyperthreading, E-cores don't
        int logical = topology.total_logical;
        
        if (logical == 20) {  // i7-12700 / i7-12700K
            for (int i = 0; i < 16; i++) topology.p_cores.push_back(i);
            for (int i = 16; i < 20; i++) topology.e_cores.push_back(i);
        }
        else if (logical == 24) {  // i9-12900K
            for (int i = 0; i < 16; i++) topology.p_cores.push_back(i);
            for (int i = 16; i < 24; i++) topology.e_cores.push_back(i);
        }
        else if (logical == 16) {  // i5-12600K
            for (int i = 0; i < 12; i++) topology.p_cores.push_back(i);
            for (int i = 12; i < 16; i++) topology.e_cores.push_back(i);
        }
        else {
            // Generic fallback: assume first 2/3 are P-cores with HT
            int p_logical = (logical * 2) / 3;
            for (int i = 0; i < p_logical; i++) topology.p_cores.push_back(i);
            for (int i = p_logical; i < logical; i++) topology.e_cores.push_back(i);
        }
        
        // Calculate affinity masks
        for (int core : topology.p_cores) {
            topology.p_core_mask |= (1ULL << core);
        }
        for (int core : topology.e_cores) {
            topology.e_core_mask |= (1ULL << core);
        }
        
        console::log_info("Detected hybrid CPU: " + topology.cpu_name);
        console::log_info("P-cores: " + std::to_string(topology.p_cores.size()) + 
                         " logical, E-cores: " + std::to_string(topology.e_cores.size()) + " logical");
    }
    else {
        // Non-hybrid CPU - all cores are equal
        for (int i = 0; i < topology.total_logical; i++) {
            topology.p_cores.push_back(i);
            topology.p_core_mask |= (1ULL << i);
        }
        console::log_info("Detected standard CPU: " + topology.cpu_name + 
                         " (" + std::to_string(topology.total_logical) + " logical cores)");
    }
    
    return topology;
}

CoreAssignment PriorityManager::calculate_core_assignment(const CpuTopology& topology) {
    CoreAssignment assignment;
    
    if (topology.is_hybrid && topology.p_cores.size() >= 6) {
        // For hybrid CPUs: assign to different PHYSICAL P-cores
        // Logical 0,1 = P-core 0's threads
        // Logical 2,3 = P-core 1's threads, etc.
        
        assignment.capture = 1ULL << topology.p_cores[0];    // P-core 0
        assignment.inference = 1ULL << topology.p_cores[2];  // P-core 1
        assignment.mouse = 1ULL << topology.p_cores[4];      // P-core 2
        
        // GUI gets E-core if available
        if (!topology.e_cores.empty()) {
            assignment.gui = 1ULL << topology.e_cores[0];
        } else {
            assignment.gui = 1ULL << topology.p_cores[1];    // P-core 0 HT
        }
        
        std::ostringstream ss;
        ss << "Core assignment (hybrid): capture=" << topology.p_cores[0]
           << ", inference=" << topology.p_cores[2]
           << ", mouse=" << topology.p_cores[4]
           << ", gui=" << (topology.e_cores.empty() ? topology.p_cores[1] : topology.e_cores[0]);
        console::log_info(ss.str());
    }
    else if (topology.p_cores.size() >= 4) {
        // Standard CPU - use first 4 cores
        assignment.capture = 1ULL << topology.p_cores[0];
        assignment.inference = 1ULL << topology.p_cores[1];
        assignment.mouse = 1ULL << topology.p_cores[2];
        assignment.gui = 1ULL << topology.p_cores[3];
        
        std::ostringstream ss;
        ss << "Core assignment (standard): capture=" << topology.p_cores[0]
           << ", inference=" << topology.p_cores[1]
           << ", mouse=" << topology.p_cores[2]
           << ", gui=" << topology.p_cores[3];
        console::log_info(ss.str());
    }
    
    return assignment;
}

void PriorityManager::maximize_process_priority() {
    HANDLE process = GetCurrentProcess();
    
    // Try REALTIME, fall back to HIGH
    if (!SetPriorityClass(process, REALTIME_PRIORITY_CLASS)) {
        if (SetPriorityClass(process, HIGH_PRIORITY_CLASS)) {
            console::log_info("Process priority: HIGH (run as admin for REALTIME)");
        } else {
            console::log_warn("Could not set process priority");
        }
    } else {
        console::log_ok("Process priority: REALTIME");
    }
    
    // Set process affinity to P-cores only on hybrid CPUs
    if (topology_.is_hybrid && topology_.p_core_mask != 0) {
        if (SetProcessAffinityMask(process, topology_.p_core_mask)) {
            console::log_info("Process affinity: P-cores only");
        }
    }
    
    // Prevent Windows from throttling/sleeping
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    console::log_info("Sleep/throttle prevention: enabled");
    
    // Disable priority boost for consistent timing
    SetProcessPriorityBoost(process, TRUE);
    console::log_info("Priority boost: disabled (consistent timing)");
}

bool PriorityManager::set_thread_priority(int priority) {
    HANDLE thread = GetCurrentThread();
    return SetThreadPriority(thread, priority) != 0;
}

bool PriorityManager::set_thread_affinity(DWORD_PTR core_mask) {
    HANDLE thread = GetCurrentThread();
    return SetThreadAffinityMask(thread, core_mask) != 0;
}

void PriorityManager::configure_capture_thread() {
    set_thread_priority(THREAD_PRIORITY_TIME_CRITICAL);
    set_thread_affinity(core_assignment_.capture);
    
    std::ostringstream ss;
    ss << std::hex << "Capture thread: TIME_CRITICAL, affinity=0x" << core_assignment_.capture;
    console::log_info(ss.str());
}

void PriorityManager::configure_inference_thread() {
    set_thread_priority(THREAD_PRIORITY_TIME_CRITICAL);
    set_thread_affinity(core_assignment_.inference);
    
    std::ostringstream ss;
    ss << std::hex << "Inference thread: TIME_CRITICAL, affinity=0x" << core_assignment_.inference;
    console::log_info(ss.str());
}

void PriorityManager::configure_mouse_thread() {
    set_thread_priority(THREAD_PRIORITY_TIME_CRITICAL);
    set_thread_affinity(core_assignment_.mouse);
    
    std::ostringstream ss;
    ss << std::hex << "Mouse thread: TIME_CRITICAL, affinity=0x" << core_assignment_.mouse;
    console::log_info(ss.str());
}

void PriorityManager::configure_gui_thread() {
    set_thread_priority(THREAD_PRIORITY_LOWEST);
    set_thread_affinity(core_assignment_.gui);
    
    std::ostringstream ss;
    ss << std::hex << "GUI thread: LOWEST, affinity=0x" << core_assignment_.gui;
    console::log_info(ss.str());
}

void PriorityManager::cleanup() {
    // Reset execution state
    SetThreadExecutionState(ES_CONTINUOUS);
    initialized_ = false;
}

} // namespace catclicker
