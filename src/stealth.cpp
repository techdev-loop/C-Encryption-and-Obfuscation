#include "stealth.h"
#include "common.h"
#include <windows.h>
namespace catclicker {
namespace stealth {

static bool g_stealth_enabled = false;

void init_stealth(bool enable) {
    g_stealth_enabled = enable;
}

void hide_from_task_manager() {
    if (!g_stealth_enabled) return;
    // Full Task Manager hiding requires kernel drivers. We set a generic title.
    SetConsoleTitleW(L"Host");
}

} // namespace stealth
} // namespace catclicker
