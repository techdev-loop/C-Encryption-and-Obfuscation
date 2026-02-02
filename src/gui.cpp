#include "gui.h"
#include "priority_manager.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace catclicker {

// Color scheme
namespace colors {
    const ImVec4 bg_dark = ImVec4(0.039f, 0.039f, 0.059f, 1.0f);
    const ImVec4 bg_card = ImVec4(0.071f, 0.071f, 0.094f, 1.0f);
    const ImVec4 bg_input = ImVec4(0.118f, 0.118f, 0.157f, 1.0f);
    const ImVec4 pink_primary = ImVec4(0.784f, 0.314f, 0.706f, 1.0f);
    const ImVec4 pink_glow = ImVec4(1.0f, 0.471f, 0.863f, 1.0f);
    const ImVec4 pink_dim = ImVec4(0.471f, 0.196f, 0.392f, 1.0f);
    const ImVec4 pink_border = ImVec4(0.706f, 0.235f, 0.627f, 1.0f);
    const ImVec4 pink_accent = ImVec4(0.863f, 0.392f, 0.784f, 1.0f);
    const ImVec4 purple_accent = ImVec4(0.549f, 0.314f, 0.784f, 1.0f);
    const ImVec4 text_white = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    const ImVec4 text_gray = ImVec4(0.627f, 0.627f, 0.667f, 1.0f);
    const ImVec4 text_dim = ImVec4(0.392f, 0.392f, 0.431f, 1.0f);
    const ImVec4 text_label = ImVec4(0.784f, 0.784f, 0.824f, 1.0f);
    const ImVec4 accent_green = ImVec4(0.314f, 1.0f, 0.706f, 1.0f);
    const ImVec4 accent_orange = ImVec4(1.0f, 0.706f, 0.314f, 1.0f);
    const ImVec4 toggle_on = ImVec4(0.784f, 0.314f, 0.706f, 1.0f);
    const ImVec4 toggle_off = ImVec4(0.235f, 0.235f, 0.275f, 1.0f);
}

void AtomicConfig::load_from(const RuntimeConfig& cfg) {
    sensitivity_scale.store(cfg.sensitivity_scale);
    pixel_step.store(cfg.pixel_step);
    mouse_delay.store(cfg.mouse_delay);
    head_ratio.store(cfg.head_ratio);
    lock_threshold.store(cfg.lock_threshold);
    skip_frames.store(cfg.skip_frames);
    confidence.store(cfg.confidence);
    movement_boost.store(cfg.movement_boost);
    enable_movement_compensation.store(cfg.enable_movement_compensation);
    ads_multiplier.store(cfg.ads_multiplier);
    enable_ads_compensation.store(cfg.enable_ads_compensation);
    debug_window.store(cfg.debug_window);
    fov.store(cfg.fov);
    
    // Smoothing
    smoothing_curve.store(cfg.smoothing_curve);
    smoothing_strength.store(cfg.smoothing_strength);
    
    // Auto-click (new system)
    auto_click_mode.store(cfg.auto_click_mode);
    click_cooldown.store(cfg.click_cooldown);
    auto_click_key.store(cfg.auto_click_key);
    
    // Tracking center
    tracking_center.store(cfg.tracking_center);
    
    // Prediction
    prediction_enabled.store(cfg.prediction_enabled);
    prediction_strength.store(cfg.prediction_strength);
    prediction_lookahead.store(cfg.prediction_lookahead);
    prediction_process_noise.store(cfg.prediction_process_noise);
    prediction_measurement_noise.store(cfg.prediction_measurement_noise);
    
    // Keys
    trigger_key.store(cfg.trigger_key);
    strafe_left_key.store(cfg.strafe_left_key);
    strafe_right_key.store(cfg.strafe_right_key);
    jump_key.store(cfg.jump_key);
    ads_key.store(cfg.ads_key);
}

void AtomicConfig::save_to(RuntimeConfig& cfg) const {
    cfg.sensitivity_scale = sensitivity_scale.load();
    cfg.pixel_step = pixel_step.load();
    cfg.mouse_delay = mouse_delay.load();
    cfg.head_ratio = head_ratio.load();
    cfg.lock_threshold = lock_threshold.load();
    cfg.skip_frames = skip_frames.load();
    cfg.confidence = confidence.load();
    cfg.movement_boost = movement_boost.load();
    cfg.enable_movement_compensation = enable_movement_compensation.load();
    cfg.ads_multiplier = ads_multiplier.load();
    cfg.enable_ads_compensation = enable_ads_compensation.load();
    cfg.debug_window = debug_window.load();
    cfg.fov = fov.load();
    
    // Smoothing
    cfg.smoothing_curve = smoothing_curve.load();
    cfg.smoothing_strength = smoothing_strength.load();
    
    // Auto-click
    cfg.auto_click_mode = auto_click_mode.load();
    cfg.click_cooldown = click_cooldown.load();
    cfg.auto_click_key = auto_click_key.load();
    
    // Tracking center
    cfg.tracking_center = tracking_center.load();
    
    // Prediction
    cfg.prediction_enabled = prediction_enabled.load();
    cfg.prediction_strength = prediction_strength.load();
    cfg.prediction_lookahead = prediction_lookahead.load();
    cfg.prediction_process_noise = prediction_process_noise.load();
    cfg.prediction_measurement_noise = prediction_measurement_noise.load();
    
    // Keys
    cfg.trigger_key = trigger_key.load();
    cfg.strafe_left_key = strafe_left_key.load();
    cfg.strafe_right_key = strafe_right_key.load();
    cfg.jump_key = jump_key.load();
    cfg.ads_key = ads_key.load();
    
    // Legacy compat
    cfg.auto_click_enabled = (cfg.auto_click_mode != 0);
    cfg.auto_click_toggle_key = cfg.auto_click_key;
}

CatClickerGui::CatClickerGui() = default;

CatClickerGui::~CatClickerGui() { stop(); }

bool CatClickerGui::start(const RuntimeConfig& initial_config) {
    if (running_.load()) return true;
    
    config_.load_from(initial_config);
    running_.store(true);
    should_quit_.store(false);
    
    gui_thread_ = std::thread(&CatClickerGui::gui_thread_func, this);
    
    // Wait for initialization
    int timeout = 3000;
    while (!hwnd_ && running_.load() && timeout > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timeout -= 10;
    }
    
    return hwnd_ != nullptr;
}

void CatClickerGui::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (gui_thread_.joinable()) {
        gui_thread_.join();
    }
}

void CatClickerGui::gui_thread_func() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    
    auto& pm = get_priority_manager();
    pm.configure_gui_thread();
    
    if (!initialize()) {
        console::log_error("GUI initialization failed");
        running_.store(false);
        return;
    }
    
    sync_local_from_atomic();
    
    const auto frame_time = std::chrono::milliseconds(33);
    
    while (running_.load()) {
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        if (!render_frame()) {
            should_quit_.store(true);
            break;
        }
        
        auto elapsed = std::chrono::high_resolution_clock::now() - frame_start;
        auto sleep_time = frame_time - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
    
    shutdown();
}

bool CatClickerGui::create_device_d3d(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        fls, 2, D3D11_SDK_VERSION, &sd, &swap_chain_, &d3d_device_, &fl, &d3d_context_))) {
        return false;
    }

    create_render_target();
    return true;
}

void CatClickerGui::cleanup_device_d3d() {
    cleanup_render_target();
    if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
    if (d3d_context_) { d3d_context_->Release(); d3d_context_ = nullptr; }
    if (d3d_device_) { d3d_device_->Release(); d3d_device_ = nullptr; }
}

void CatClickerGui::create_render_target() {
    ID3D11Texture2D* back_buffer = nullptr;
    swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer) {
        d3d_device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
        back_buffer->Release();
    }
}

void CatClickerGui::cleanup_render_target() {
    if (render_target_) { render_target_->Release(); render_target_ = nullptr; }
}

LRESULT WINAPI CatClickerGui::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    CatClickerGui* gui = reinterpret_cast<CatClickerGui*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && gui && gui->d3d_device_ && gui->swap_chain_) {
            gui->cleanup_render_target();
            gui->swap_chain_->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            gui->create_render_target();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CatClickerGui::initialize() {
    wc_ = { sizeof(wc_), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
            nullptr, nullptr, nullptr, nullptr, L"CatClickerGUI", nullptr };
    RegisterClassExW(&wc_);

    hwnd_ = CreateWindowW(wc_.lpszClassName, L"Cat Clicker V16",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 640, 680, nullptr, nullptr, wc_.hInstance, nullptr);

    if (!hwnd_) return false;
    
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!create_device_d3d(hwnd_)) {
        cleanup_device_d3d();
        UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        return false;
    }

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(d3d_device_, d3d_context_);

    setup_style();
    return true;
}

void CatClickerGui::setup_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    
    c[ImGuiCol_WindowBg] = colors::bg_dark;
    c[ImGuiCol_ChildBg] = colors::bg_card;
    c[ImGuiCol_PopupBg] = colors::bg_card;
    c[ImGuiCol_Border] = colors::pink_border;
    c[ImGuiCol_Text] = colors::text_white;
    c[ImGuiCol_TextDisabled] = colors::text_dim;
    c[ImGuiCol_FrameBg] = colors::bg_input;
    c[ImGuiCol_FrameBgHovered] = colors::pink_dim;
    c[ImGuiCol_FrameBgActive] = colors::pink_primary;
    c[ImGuiCol_SliderGrab] = colors::pink_primary;
    c[ImGuiCol_SliderGrabActive] = colors::pink_glow;
    c[ImGuiCol_Button] = colors::pink_dim;
    c[ImGuiCol_ButtonHovered] = colors::pink_primary;
    c[ImGuiCol_ButtonActive] = colors::pink_glow;
    c[ImGuiCol_CheckMark] = colors::pink_primary;
    c[ImGuiCol_Tab] = colors::bg_input;
    c[ImGuiCol_TabHovered] = colors::pink_primary;
    c[ImGuiCol_TabActive] = colors::pink_dim;
    c[ImGuiCol_Header] = colors::pink_dim;
    c[ImGuiCol_HeaderHovered] = colors::pink_primary;
    c[ImGuiCol_Separator] = colors::pink_dim;
    c[ImGuiCol_TitleBg] = colors::bg_dark;
    c[ImGuiCol_TitleBgActive] = colors::pink_dim;
    c[ImGuiCol_ScrollbarBg] = colors::bg_dark;
    c[ImGuiCol_ScrollbarGrab] = colors::pink_dim;
    
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowPadding = ImVec2(15, 15);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
}

void CatClickerGui::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanup_device_d3d();
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
}

void CatClickerGui::sync_local_from_atomic() {
    local_sensitivity_ = config_.sensitivity_scale.load();
    local_pixel_step_ = config_.pixel_step.load();
    local_mouse_delay_ = static_cast<float>(config_.mouse_delay.load());
    local_head_ratio_ = config_.head_ratio.load();
    local_lock_threshold_ = config_.lock_threshold.load();
    local_skip_frames_ = config_.skip_frames.load();
    local_confidence_ = config_.confidence.load();
    local_movement_boost_ = config_.movement_boost.load();
    local_movement_comp_ = config_.enable_movement_compensation.load();
    local_ads_multiplier_ = config_.ads_multiplier.load();
    local_ads_comp_ = config_.enable_ads_compensation.load();
    local_debug_window_ = config_.debug_window.load();
    local_fov_ = config_.fov.load();
    
    local_smoothing_curve_ = config_.smoothing_curve.load();
    local_smoothing_strength_ = config_.smoothing_strength.load();
    
    local_auto_click_mode_ = config_.auto_click_mode.load();
    local_click_cooldown_ms_ = static_cast<float>(config_.click_cooldown.load() * 1000.0);
    local_auto_click_key_ = config_.auto_click_key.load();
    
    local_tracking_center_ = config_.tracking_center.load();
    
    local_prediction_enabled_ = config_.prediction_enabled.load();
    local_prediction_strength_ = config_.prediction_strength.load();
    local_prediction_lookahead_ms_ = config_.prediction_lookahead.load() * 1000.0f;
    local_process_noise_ = config_.prediction_process_noise.load();
    local_measurement_noise_ = config_.prediction_measurement_noise.load();
    
    local_trigger_key_ = config_.trigger_key.load();
    local_strafe_left_ = config_.strafe_left_key.load();
    local_strafe_right_ = config_.strafe_right_key.load();
    local_jump_key_ = config_.jump_key.load();
    local_ads_key_ = config_.ads_key.load();
}

void CatClickerGui::sync_atomic_from_local() {
    config_.sensitivity_scale.store(local_sensitivity_);
    config_.pixel_step.store(local_pixel_step_);
    config_.mouse_delay.store(static_cast<double>(local_mouse_delay_));
    config_.head_ratio.store(local_head_ratio_);
    config_.lock_threshold.store(local_lock_threshold_);
    config_.skip_frames.store(local_skip_frames_);
    config_.confidence.store(local_confidence_);
    config_.movement_boost.store(local_movement_boost_);
    config_.enable_movement_compensation.store(local_movement_comp_);
    config_.ads_multiplier.store(local_ads_multiplier_);
    config_.enable_ads_compensation.store(local_ads_comp_);
    config_.debug_window.store(local_debug_window_);
    config_.fov.store(local_fov_);
    
    config_.smoothing_curve.store(local_smoothing_curve_);
    config_.smoothing_strength.store(local_smoothing_strength_);
    
    config_.auto_click_mode.store(local_auto_click_mode_);
    config_.click_cooldown.store(static_cast<double>(local_click_cooldown_ms_) / 1000.0);
    config_.auto_click_key.store(local_auto_click_key_);
    
    config_.tracking_center.store(local_tracking_center_);
    
    config_.prediction_enabled.store(local_prediction_enabled_);
    config_.prediction_strength.store(local_prediction_strength_);
    config_.prediction_lookahead.store(local_prediction_lookahead_ms_ / 1000.0f);
    config_.prediction_process_noise.store(local_process_noise_);
    config_.prediction_measurement_noise.store(local_measurement_noise_);
    
    config_.trigger_key.store(local_trigger_key_);
    config_.strafe_left_key.store(local_strafe_left_);
    config_.strafe_right_key.store(local_strafe_right_);
    config_.jump_key.store(local_jump_key_);
    config_.ads_key.store(local_ads_key_);
}

bool CatClickerGui::render_frame() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }

    check_key_capture();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("Main", nullptr, flags);
    
    render_stats_bar();
    ImGui::Spacing();
    
    if (ImGui::BeginTabBar("Tabs")) {
        if (ImGui::BeginTabItem("  Aiming  ")) { render_aiming_tab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Additional  ")) { render_additional_tab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Prediction  ")) { render_prediction_tab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Keybinds  ")) { render_keybinds_tab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    
    render_footer();
    ImGui::End();

    ImGui::Render();
    const float clear[4] = { colors::bg_dark.x, colors::bg_dark.y, colors::bg_dark.z, 1.0f };
    d3d_context_->OMSetRenderTargets(1, &render_target_, nullptr);
    d3d_context_->ClearRenderTargetView(render_target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    swap_chain_->Present(2, 0);
    return true;
}

void CatClickerGui::render_stats_bar() {
    ImGui::BeginChild("Stats", ImVec2(0, 55), true);
    
    ImGui::BeginGroup();
    ImGui::TextColored(colors::text_dim, "FPS");
    ImGui::TextColored(colors::pink_primary, "%.0f", status_.fps.load());
    ImGui::EndGroup();
    
    ImGui::SameLine(80);
    ImGui::BeginGroup();
    ImGui::TextColored(colors::text_dim, "INFERENCE");
    ImGui::TextColored(colors::pink_primary, "%.0f", status_.inference_fps.load());
    ImGui::EndGroup();
    
    ImGui::SameLine(180);
    ImGui::BeginGroup();
    ImGui::TextColored(colors::text_dim, "TARGETS");
    ImGui::TextColored(colors::accent_green, "%d", status_.targets.load());
    ImGui::EndGroup();
    
    ImGui::SameLine(260);
    ImGui::BeginGroup();
    ImGui::TextColored(colors::text_dim, "STATUS");
    bool locked = status_.locked.load();
    bool active = status_.active.load();
    if (locked) ImGui::TextColored(colors::pink_primary, "LOCKED");
    else if (active) ImGui::TextColored(colors::accent_green, "ACTIVE");
    else ImGui::TextColored(colors::text_gray, "STANDBY");
    ImGui::EndGroup();
    
    if (status_.auto_click_on.load()) {
        ImGui::SameLine(360);
        ImGui::BeginGroup();
        ImGui::TextColored(colors::text_dim, "AUTO-CLICK");
        ImGui::TextColored(colors::accent_orange, "ON");
        ImGui::EndGroup();
    }
    
    if (status_.ads_active.load()) {
        ImGui::SameLine(470);
        ImGui::BeginGroup();
        ImGui::TextColored(colors::text_dim, "ADS");
        ImGui::TextColored(colors::purple_accent, "ON");
        ImGui::EndGroup();
    }
    
    ImGui::SameLine(540);
    ImGui::BeginGroup();
    ImGui::TextColored(colors::text_dim, "MODE");
    if (local_tracking_center_ == 1) {
        ImGui::TextColored(colors::accent_orange, "MOUSE");
    } else {
        ImGui::TextColored(colors::text_gray, "CENTER");
    }
    ImGui::EndGroup();
    
    ImGui::EndChild();
}

void CatClickerGui::render_aiming_tab() {
    ImGui::Spacing();
    bool changed = false;
    
    ImGui::BeginChild("Left", ImVec2(290, 460), true);
    ImGui::TextColored(colors::pink_primary, "Aiming");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_label, "Movement Comp");
    ImGui::SameLine(180);
    ImGui::PushStyleColor(ImGuiCol_Button, local_movement_comp_ ? colors::toggle_on : colors::toggle_off);
    if (ImGui::Button(local_movement_comp_ ? "ON##mc" : "OFF##mc", ImVec2(50, 24))) {
        local_movement_comp_ = !local_movement_comp_; changed = true;
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_label, "ADS Comp");
    ImGui::SameLine(180);
    ImGui::PushStyleColor(ImGuiCol_Button, local_ads_comp_ ? colors::toggle_on : colors::toggle_off);
    if (ImGui::Button(local_ads_comp_ ? "ON##ads" : "OFF##ads", ImVec2(50, 24))) {
        local_ads_comp_ = !local_ads_comp_; changed = true;
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    
    ImGui::TextColored(colors::text_gray, "Sensitivity");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##sens", &local_sensitivity_, 0.01f, 2.0f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_sensitivity_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Pixel Step");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("##pxs", &local_pixel_step_, 1, 20, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%d", local_pixel_step_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "ADS Multiplier");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##adsm", &local_ads_multiplier_, 0.5f, 3.0f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_ads_multiplier_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Movement Boost");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##mvb", &local_movement_boost_, 0.0f, 1.5f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_movement_boost_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Head Ratio");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##hr", &local_head_ratio_, 1.5f, 8.0f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_head_ratio_);
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Smoothing Curve");
    ImGui::TextColored(colors::text_dim, "Controls mouse movement easing");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_gray, "Curve Type");
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##curve", &local_smoothing_curve_, SMOOTHING_CURVE_NAMES, 6)) changed = true;
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Curve Strength");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##cstr", &local_smoothing_strength_, 0.0f, 1.0f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_smoothing_strength_);
    
    ImGui::EndChild();
    ImGui::SameLine();
    
    ImGui::BeginChild("Right", ImVec2(290, 460), true);
    ImGui::TextColored(colors::pink_primary, "Detection");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_gray, "FOV (Capture Size)");
    ImGui::TextColored(colors::text_dim, "Larger = wider view, smaller = faster");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("##fov", &local_fov_, config::MIN_FOV, config::MAX_FOV, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%d px", local_fov_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Skip Frames");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("##sf", &local_skip_frames_, 1, 10, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%d", local_skip_frames_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Confidence");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##conf", &local_confidence_, 0.1f, 0.9f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.2f", local_confidence_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Lock Threshold");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("##lt", &local_lock_threshold_, 1, 30, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%d", local_lock_threshold_);
    
    ImGui::Spacing();
    ImGui::TextColored(colors::text_gray, "Mouse Delay (s)");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##md", &local_mouse_delay_, 0.0f, 0.005f, "")) changed = true;
    ImGui::SameLine(); ImGui::Text("%.4f", local_mouse_delay_);
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Tracking Center Mode");
    ImGui::TextColored(colors::text_dim, "Where to center detection box");
    ImGui::Spacing();
    
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("##tcenter", &local_tracking_center_, TRACKING_CENTER_NAMES, 2)) changed = true;
    
    if (local_tracking_center_ == 1) {
        ImGui::Spacing();
        ImGui::TextColored(colors::accent_orange, "Mouse-centered mode active!");
        ImGui::TextColored(colors::text_dim, "Detection follows your cursor.");
        ImGui::TextColored(colors::text_dim, "Good for TPS and non-FPS games.");
    }
    
    ImGui::EndChild();
    
    if (changed) { sync_atomic_from_local(); config_changed_.store(true); }
}

void CatClickerGui::render_additional_tab() {
    ImGui::Spacing();
    bool changed = false;
    
    ImGui::BeginChild("Additional", ImVec2(0, 480), true);
    ImGui::TextColored(colors::pink_primary, "Additional Settings");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_label, "Debug Window");
    ImGui::SameLine(200);
    ImGui::PushStyleColor(ImGuiCol_Button, local_debug_window_ ? colors::toggle_on : colors::toggle_off);
    if (ImGui::Button(local_debug_window_ ? "ON##dbg" : "OFF##dbg", ImVec2(50, 24))) {
        local_debug_window_ = !local_debug_window_; changed = true;
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Auto-Click Settings");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_gray, "Auto-Click Mode");
    ImGui::SetNextItemWidth(280);
    if (ImGui::Combo("##acmode", &local_auto_click_mode_, AUTO_CLICK_MODE_NAMES, 5)) changed = true;
    
    ImGui::Spacing();
    
    switch (local_auto_click_mode_) {
        case 0:
            ImGui::TextColored(colors::text_dim, "Auto-click is disabled.");
            break;
        case 1:
            ImGui::TextColored(colors::text_dim, "Toggle ON/OFF with key.");
            ImGui::TextColored(colors::text_dim, "Clicks while tracking any target.");
            break;
        case 2:
            ImGui::TextColored(colors::text_dim, "Toggle ON/OFF with key.");
            ImGui::TextColored(colors::accent_green, "Clicks ONLY when crosshair is locked!");
            break;
        case 3:
            ImGui::TextColored(colors::text_dim, "Hold key to auto-click.");
            ImGui::TextColored(colors::text_dim, "Clicks while tracking any target.");
            break;
        case 4:
            ImGui::TextColored(colors::text_dim, "Hold key to auto-click.");
            ImGui::TextColored(colors::accent_green, "Clicks ONLY when crosshair is locked!");
            break;
    }
    
    if (local_auto_click_mode_ != 0) {
        ImGui::Spacing();
        ImGui::TextColored(colors::text_gray, "Click Cooldown (ms)");
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("##cd", &local_click_cooldown_ms_, 10.0f, 200.0f, "")) changed = true;
        ImGui::SameLine(); ImGui::Text("%.0f ms (%.0f CPS)", local_click_cooldown_ms_, 1000.0f / local_click_cooldown_ms_);
        
        ImGui::Spacing();
        render_keybind_button("Auto-Click Key", &local_auto_click_key_, "autoclick");
    }
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Smoothing Curve Preview");
    ImGui::Spacing();
    
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size(200, 100);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(30, 30, 40, 255), 4.0f);
    
    for (int i = 1; i < 4; i++) {
        float x = canvas_pos.x + (canvas_size.x * i / 4);
        draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y),
            IM_COL32(60, 60, 80, 255));
    }
    
    ImVec2 prev_point = canvas_pos;
    prev_point.y += canvas_size.y;
    
    for (int i = 1; i <= 50; i++) {
        float t = static_cast<float>(i) / 50.0f;
        float curved = smoothing::apply_curve(t, local_smoothing_curve_, local_smoothing_strength_);
        
        ImVec2 point;
        point.x = canvas_pos.x + t * canvas_size.x;
        point.y = canvas_pos.y + canvas_size.y - curved * canvas_size.y;
        
        draw_list->AddLine(prev_point, point, IM_COL32(200, 80, 180, 255), 2.0f);
        prev_point = point;
    }
    
    draw_list->AddLine(
        ImVec2(canvas_pos.x, canvas_pos.y + canvas_size.y),
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y),
        IM_COL32(100, 100, 100, 128), 1.0f);
    
    ImGui::Dummy(canvas_size);
    ImGui::TextColored(colors::text_dim, "Pink = current curve, Gray = linear");
    
    ImGui::EndChild();
    
    if (changed) { sync_atomic_from_local(); config_changed_.store(true); }
}

void CatClickerGui::render_prediction_tab() {
    ImGui::Spacing();
    bool changed = false;
    
    ImGui::BeginChild("Prediction", ImVec2(0, 400), true);
    ImGui::TextColored(colors::pink_primary, "Kalman Filter Prediction");
    ImGui::TextColored(colors::text_dim, "Predict target movement for smoother tracking");
    ImGui::Spacing();
    
    ImGui::TextColored(colors::text_label, "Enable Prediction");
    ImGui::SameLine(200);
    ImGui::PushStyleColor(ImGuiCol_Button, local_prediction_enabled_ ? colors::toggle_on : colors::toggle_off);
    if (ImGui::Button(local_prediction_enabled_ ? "ON##pred" : "OFF##pred", ImVec2(50, 24))) {
        local_prediction_enabled_ = !local_prediction_enabled_; changed = true;
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    
    if (local_prediction_enabled_) {
        ImGui::TextColored(colors::text_gray, "Prediction Strength");
        ImGui::TextColored(colors::text_dim, "0.0 = measurements only, 1.0 = full prediction");
        ImGui::SetNextItemWidth(250);
        if (ImGui::SliderFloat("##ps", &local_prediction_strength_, 0.0f, 1.0f, "")) changed = true;
        ImGui::SameLine(); ImGui::Text("%.2f", local_prediction_strength_);
        
        ImGui::Spacing();
        ImGui::TextColored(colors::text_gray, "Lookahead Time (ms)");
        ImGui::SetNextItemWidth(250);
        if (ImGui::SliderFloat("##la", &local_prediction_lookahead_ms_, 1.0f, 100.0f, "")) changed = true;
        ImGui::SameLine(); ImGui::Text("%.0f ms", local_prediction_lookahead_ms_);
        
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(colors::pink_accent, "Advanced Tuning");
        ImGui::Spacing();
        
        ImGui::TextColored(colors::text_gray, "Process Noise");
        ImGui::TextColored(colors::text_dim, "Higher = more responsive");
        ImGui::SetNextItemWidth(250);
        if (ImGui::SliderFloat("##pn", &local_process_noise_, 0.01f, 5.0f, "")) changed = true;
        ImGui::SameLine(); ImGui::Text("%.2f", local_process_noise_);
        
        ImGui::Spacing();
        ImGui::TextColored(colors::text_gray, "Measurement Noise");
        ImGui::TextColored(colors::text_dim, "Higher = trust predictions more");
        ImGui::SetNextItemWidth(250);
        if (ImGui::SliderFloat("##mn", &local_measurement_noise_, 0.1f, 10.0f, "")) changed = true;
        ImGui::SameLine(); ImGui::Text("%.2f", local_measurement_noise_);
    } else {
        ImGui::TextColored(colors::text_dim, "Enable prediction to see settings...");
    }
    
    ImGui::EndChild();
    
    if (changed) { sync_atomic_from_local(); config_changed_.store(true); }
}

void CatClickerGui::render_keybind_button(const char* label, int* key_value, const char* button_id) {
    ImGui::TextColored(colors::text_label, "%s", label);
    ImGui::SameLine(150);
    
    bool waiting = (waiting_for_key_ == button_id);
    
    if (waiting) {
        ImGui::PushStyleColor(ImGuiCol_Button, colors::accent_orange);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::accent_orange);
        if (ImGui::Button("Press key...", ImVec2(120, 28))) waiting_for_key_.clear();
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, colors::purple_accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::pink_primary);
        std::string btn_label = get_key_name(*key_value) + "##" + button_id;
        if (ImGui::Button(btn_label.c_str(), ImVec2(120, 28))) waiting_for_key_ = button_id;
        ImGui::PopStyleColor(2);
    }
}

void CatClickerGui::check_key_capture() {
    if (waiting_for_key_.empty()) return;
    
    for (int vk = 0x01; vk <= 0xFF; vk++) {
        if (vk == VK_LBUTTON || vk == VK_ESCAPE) continue;
        
        if (GetAsyncKeyState(vk) & 0x8001) {
            int* target = nullptr;
            if (waiting_for_key_ == "trigger") target = &local_trigger_key_;
            else if (waiting_for_key_ == "strafe_left") target = &local_strafe_left_;
            else if (waiting_for_key_ == "strafe_right") target = &local_strafe_right_;
            else if (waiting_for_key_ == "jump") target = &local_jump_key_;
            else if (waiting_for_key_ == "ads") target = &local_ads_key_;
            else if (waiting_for_key_ == "autoclick") target = &local_auto_click_key_;
            
            if (target) {
                *target = vk;
                sync_atomic_from_local();
                config_changed_.store(true);
            }
            waiting_for_key_.clear();
            break;
        }
    }
}

void CatClickerGui::render_keybinds_tab() {
    ImGui::Spacing();
    
    ImGui::BeginChild("Keybinds", ImVec2(0, 400), true);
    ImGui::TextColored(colors::pink_primary, "Keybinds");
    ImGui::TextColored(colors::text_dim, "Click a button then press a key to rebind");
    ImGui::Spacing(); ImGui::Spacing();
    
    render_keybind_button("Trigger Key", &local_trigger_key_, "trigger");
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Movement Keys");
    ImGui::Spacing();
    
    render_keybind_button("Strafe Left", &local_strafe_left_, "strafe_left");
    ImGui::Spacing();
    render_keybind_button("Strafe Right", &local_strafe_right_, "strafe_right");
    ImGui::Spacing();
    render_keybind_button("Jump", &local_jump_key_, "jump");
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    render_keybind_button("ADS Key", &local_ads_key_, "ads");
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(colors::pink_accent, "Auto-Click Key");
    ImGui::TextColored(colors::text_dim, "Used for toggle or hold modes");
    ImGui::Spacing();
    render_keybind_button("Auto-Click", &local_auto_click_key_, "autoclick");
    
    ImGui::EndChild();
}

void CatClickerGui::render_footer() {
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(colors::pink_primary, "CAT CLICKER V16");
    ImGui::SameLine(200);
    ImGui::TextColored(colors::text_dim, "SIMD: %s", cpu_supports_avx2() ? "AVX2" : (cpu_supports_sse41() ? "SSE4.1" : "Scalar"));
    ImGui::SameLine(380);
    ImGui::TextColored(colors::text_dim, "GUI: 30 FPS (low priority)");
}

} // namespace catclicker
