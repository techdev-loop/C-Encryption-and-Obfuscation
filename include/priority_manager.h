#pragma once

#include "common.h"
#include <vector>
#include <string>

namespace catclicker {

// CPU topology information
struct CpuTopology {
    bool is_hybrid = false;
    std::vector<int> p_cores;      // Performance cores
    std::vector<int> e_cores;      // Efficiency cores
    DWORD_PTR p_core_mask = 0;
    DWORD_PTR e_core_mask = 0;
    int total_logical = 0;
    std::string cpu_name;
};

// Core assignments for different threads
struct CoreAssignment {
    DWORD_PTR capture = 0x1;
    DWORD_PTR inference = 0x2;
    DWORD_PTR mouse = 0x4;
    DWORD_PTR gui = 0x8;
};

class PriorityManager {
public:
    PriorityManager();
    ~PriorityManager();
    
    // Initialize and detect CPU topology
    bool initialize();
    
    // Set current thread priority
    static bool set_thread_priority(int priority);
    
    // Set current thread affinity to specific cores
    static bool set_thread_affinity(DWORD_PTR core_mask);
    
    // Configure thread for specific role
    void configure_capture_thread();
    void configure_inference_thread();
    void configure_mouse_thread();
    void configure_gui_thread();
    
    // Get core assignment
    const CoreAssignment& get_core_assignment() const { return core_assignment_; }
    const CpuTopology& get_topology() const { return topology_; }
    
    // Cleanup on exit
    void cleanup();

private:
    CpuTopology detect_cpu_topology();
    CoreAssignment calculate_core_assignment(const CpuTopology& topology);
    void maximize_process_priority();
    
    CpuTopology topology_;
    CoreAssignment core_assignment_;
    bool initialized_ = false;
};

// Global instance
PriorityManager& get_priority_manager();

} // namespace catclicker
