#include "common.h"
#include "config.h"
#include "priority_manager.h"
#include "frame_grabber.h"
#include "inference_engine.h"
#include "mouse_controller.h"
#include "input_tracker.h"
#include "target_selector.h"
#include "target_predictor.h"
#include "gui.h"
#include "hwid.h"
#include "license_client.h"
#include "antidebug.h"
#include "obfuscate.h"

#include <opencv2/opencv.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <string>

using namespace catclicker;
namespace fs = std::filesystem;

static std::atomic<bool> g_shutdown{false};

void signal_handler(int) { g_shutdown = true; }

void print_banner() {
    std::cout << "\n";
    console::set_color(console::CYAN);
    std::cout << "==================================================\n";
    std::cout << "       CAT CLICKER V16 - C++ EDITION\n";
    std::cout << "       DXGI + SIMD | TensorRT Inference\n";
    std::cout << "       + Kalman Prediction + Smoothing Curves\n";
    std::cout << "       + Mouse-Centered Tracking (TPS Mode)\n";
    std::cout << "==================================================\n";
    console::set_color(console::WHITE);
    std::cout << "\n";
}

void print_help() {
    std::cout << "Usage: cat_clicker [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --debug, -d          Show debug window\n";
    std::cout << "  --no-gui             Disable GUI\n";
    std::cout << "  --model <path>       Model path\n";
    std::cout << "  --imgsz <size>       Model input size (default: 320)\n";
    std::cout << "  --fov <size>         FOV/capture size in pixels (default: 320, range: 160-640)\n";
    std::cout << "  --fp32               Use FP32\n";
    std::cout << "  --confidence <val>   Detection threshold (default: 0.3)\n";
    std::cout << "  --sensitivity <val>  Mouse sensitivity (default: 0.75)\n";
    std::cout << "  --prediction <0-1>   Enable prediction with strength\n";
    std::cout << "  --lookahead <ms>     Prediction lookahead (default: 16)\n";
    std::cout << "  --mouse-center       Use mouse position as tracking center (TPS mode)\n";
    std::cout << "  --smoothing <0-5>    Smoothing curve type (0=linear, 1=ease-out, etc)\n";
}

// Auto-click state machine
struct AutoClickState {
    bool toggled = false;           // For toggle modes
    bool key_was_pressed = false;   // Edge detection
    double last_click_time = 0.0;
    
    // Returns true if we should click this frame
    bool should_click(int mode, int key, bool is_locked, bool is_tracking, 
                      double current_time, double cooldown) {
        if (mode == 0) return false;  // Disabled
        
        bool key_pressed = (GetAsyncKeyState(key) & 0x8000) != 0;
        
        // Handle toggle vs hold
        bool active = false;
        if (mode == 1 || mode == 2) {
            // Toggle modes
            if (key_pressed && !key_was_pressed) {
                toggled = !toggled;
            }
            active = toggled;
        } else {
            // Hold modes (3, 4)
            active = key_pressed;
        }
        key_was_pressed = key_pressed;
        
        if (!active) return false;
        
        // Check locked-only vs while-tracking
        bool condition_met = false;
        if (mode == 1 || mode == 3) {
            // While tracking - click if we have any target
            condition_met = is_tracking;
        } else {
            // Locked only - click only when crosshair is on target
            condition_met = is_locked;
        }
        
        if (!condition_met) return false;
        
        // Check cooldown
        if (current_time - last_click_time < cooldown) return false;
        
        last_click_time = current_time;
        return true;
    }
    
    bool is_active(int mode, int key) const {
        if (mode == 0) return false;
        if (mode == 1 || mode == 2) return toggled;
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
};

#if defined(CATCLICKER_ENABLE_LICENSE)
static bool run_license_checks(int argc, char* argv[]) {
    using namespace catclicker;
    if (antidebug::is_debugger_present()) {
        console::log_error(OBF("Debugger or analysis tool detected. Application cannot run.").decrypt());
        return false;
    }
    std::string auth_url = OBF("https://auth.example.com").decrypt();
    license::set_auth_base_url(auth_url);
    std::string hwid_str = hwid::get_hwid();
    if (hwid_str.empty()) {
        console::log_error(OBF("Could not generate hardware ID. Cannot verify license.").decrypt());
        return false;
    }
    std::string ip_str;
    license::LicenseResult result = license::validate_session(hwid_str, ip_str);
    if (result.need_login && !result.success) {
        std::string email, password;
        std::cout << OBF("Log in to continue (email and password):\n").decrypt();
        std::cout << "Email: "; std::getline(std::cin, email);
        std::cout << "Password: "; std::getline(std::cin, password);
        result = license::login_and_bind(email, password, hwid_str, ip_str);
        if (!result.success) {
            console::log_error(result.error_message.empty() ? OBF("Login failed.").decrypt() : result.error_message);
            return false;
        }
    } else if (!result.success) {
        console::log_error(result.error_message.empty() ? OBF("License validation failed.").decrypt() : result.error_message);
        return false;
    }
    antidebug::start_periodic_check(30);
    return true;
}
#endif

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

#if defined(CATCLICKER_ENABLE_LICENSE)
    if (!run_license_checks(argc, argv))
        return 1;
#endif

    print_banner();
    
    RuntimeConfig cfg;
    bool enable_gui = true;
    std::string model_path = "best";
    int imgsz = 320;
    bool half = true;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_help(); return 0; }
        else if (arg == "--debug" || arg == "-d") cfg.debug_window = true;
        else if (arg == "--no-gui") enable_gui = false;
        else if ((arg == "--model") && i + 1 < argc) model_path = argv[++i];
        else if (arg == "--imgsz" && i + 1 < argc) imgsz = std::stoi(argv[++i]);
        else if (arg == "--fp32") half = false;
        else if (arg == "--confidence" && i + 1 < argc) cfg.confidence = std::stof(argv[++i]);
        else if (arg == "--sensitivity" && i + 1 < argc) cfg.sensitivity_scale = std::stof(argv[++i]);
        else if (arg == "--prediction" && i + 1 < argc) {
            cfg.prediction_enabled = true;
            cfg.prediction_strength = clamp(std::stof(argv[++i]), 0.0f, 1.0f);
        }
        else if (arg == "--lookahead" && i + 1 < argc)
            cfg.prediction_lookahead = clamp(std::stof(argv[++i]) / 1000.0f, 0.001f, 0.1f);
        else if (arg == "--fov" && i + 1 < argc)
            cfg.fov = clamp(std::stoi(argv[++i]), config::MIN_FOV, config::MAX_FOV);
        else if (arg == "--mouse-center")
            cfg.tracking_center = static_cast<int>(config::TrackingCenter::MOUSE_POSITION);
        else if (arg == "--smoothing" && i + 1 < argc)
            cfg.smoothing_curve = clamp(std::stoi(argv[++i]), 0, 5);
        else if (arg == "--no-prediction") cfg.prediction_enabled = false;
    }
    
    // Find model
    bool model_found = false;
    for (const auto& ext : {".engine", ".onnx", ".pt"}) {
        std::string check = model_path;
        size_t dot = model_path.rfind('.');
        if (dot != std::string::npos) check = model_path.substr(0, dot);
        check += ext;
        if (fs::exists(check)) { model_found = true; break; }
    }
    if (!model_found) {
        console::log_error("No model found! Use --model <path>");
        return 1;
    }
    
    // Initialize systems
    auto& priority_mgr = get_priority_manager();
    priority_mgr.initialize();
    priority_mgr.configure_inference_thread();
    
    MouseController mouse;
    if (!mouse.connect() || !mouse.start_move_thread()) {
        console::log_error("Mouse init failed");
        return 1;
    }
    
    InferenceEngine engine;
    if (!engine.load_model(model_path, imgsz, half)) {
        console::log_error("Model load failed");
        return 1;
    }
    engine.warmup(3);
    
    FrameGrabber grabber(engine.get_input_width(), cfg.fov);
    
    // Set initial tracking mode
    grabber.set_tracking_center_mode(static_cast<TrackingCenterMode>(cfg.tracking_center));
    
    if (!grabber.start()) {
        console::log_error("Frame grabber failed");
        return 1;
    }
    
    InputTracker input_tracker(cfg.strafe_left_key, cfg.strafe_right_key, cfg.jump_key, cfg.ads_key);
    TargetSelector target_selector(cfg.head_ratio, config::TARGET_HYSTERESIS, config::MIN_TARGET_DISTANCE);
    
    TargetPredictor predictor;
    predictor.set_enabled(cfg.prediction_enabled);
    predictor.set_prediction_strength(cfg.prediction_strength);
    predictor.set_lookahead_time(cfg.prediction_lookahead);
    predictor.set_process_noise(cfg.prediction_process_noise);
    predictor.set_measurement_noise(cfg.prediction_measurement_noise);
    
    // Start GUI in separate thread
    std::unique_ptr<CatClickerGui> gui;
    if (enable_gui) {
        gui = std::make_unique<CatClickerGui>();
        if (!gui->start(cfg)) {
            console::log_warn("GUI failed, continuing without");
            gui.reset();
        } else {
            console::log_ok("GUI started (separate thread, low priority)");
        }
    }
    
    // Main loop
    console::log_ok("Starting main loop...\n");
    
    uint64_t frame_count = 0, fps_counter = 0, inference_count = 0;
    double fps_time = get_time_seconds(), fps_display = 0, inference_fps = 0;
    std::vector<Detection> last_detections;
    bool was_active = false;
    AutoClickState auto_click_state;
    
    while (!g_shutdown) {
        // Check GUI quit
        if (gui && gui->should_quit()) {
            g_shutdown = true;
            break;
        }
        
        // Apply config changes from GUI
        if (gui && gui->config_changed()) {
            gui->get_config().save_to(cfg);
            
            // Update FOV
            if (cfg.fov != grabber.get_fov()) {
                grabber.set_fov(cfg.fov);
            }
            
            // Update tracking center mode
            grabber.set_tracking_center_mode(static_cast<TrackingCenterMode>(cfg.tracking_center));
            
            predictor.set_enabled(cfg.prediction_enabled);
            predictor.set_prediction_strength(cfg.prediction_strength);
            predictor.set_lookahead_time(cfg.prediction_lookahead);
            predictor.set_process_noise(cfg.prediction_process_noise);
            predictor.set_measurement_noise(cfg.prediction_measurement_noise);
            
            input_tracker.set_strafe_left_key(cfg.strafe_left_key);
            input_tracker.set_strafe_right_key(cfg.strafe_right_key);
            input_tracker.set_jump_key(cfg.jump_key);
            input_tracker.set_ads_key(cfg.ads_key);
            
            target_selector.set_head_ratio(cfg.head_ratio);
        }
        
        // Get current tracking center (screen center or mouse position)
        auto [track_center_x, track_center_y] = grabber.get_tracking_center();
        
        // Get scale factor for coordinate conversion
        float scale_factor = grabber.get_scale_factor();
        
        const cv::Mat& frame = grabber.get_frame();
        if (frame.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        frame_count++;
        fps_counter++;
        double current_time = get_time_seconds();
        
        bool active = input_tracker.is_trigger_active(cfg.trigger_key);
        bool is_locked = false;
        bool is_tracking = false;
        
        if (was_active && !active) {
            predictor.reset_all();
            target_selector.reset();
        }
        was_active = active;
        
        bool ads_active = cfg.enable_ads_compensation && input_tracker.is_ads_active();
        
        if (frame_count % cfg.skip_frames == 0) {
            last_detections = engine.predict(frame.data, frame.cols, frame.rows, cfg.confidence, config::MAX_DETECTIONS);
            inference_count++;
            
            if (active && !last_detections.empty()) {
                // Get current detection box (based on current FOV and tracking center)
                ScreenRegion detection_box = grabber.get_region();
                float scale = grabber.get_scale_factor();
                
                // Scale detections from model space to screen space
                std::vector<Detection> scaled_detections;
                for (const auto& det : last_detections) {
                    Detection scaled = det;
                    scaled.x1 *= scale;
                    scaled.y1 *= scale;
                    scaled.x2 *= scale;
                    scaled.y2 *= scale;
                    scaled_detections.push_back(scaled);
                }
                
                Target target = target_selector.get_closest_target(scaled_detections, detection_box,
                    track_center_x, track_center_y, cfg.confidence);
                
                if (target.valid()) {
                    is_tracking = true;
                    float aim_x = target.absolute_x, aim_y = target.absolute_y;
                    
                    if (cfg.prediction_enabled) {
                        predictor.update_target(target.id, target.absolute_x, target.absolute_y,
                            current_time, aim_x, aim_y);
                    }
                    
                    if (TargetSelector::is_target_locked(aim_x, aim_y, (float)track_center_x,
                        (float)track_center_y, (float)cfg.lock_threshold)) {
                        is_locked = true;
                    } else {
                        AdaptiveScale adapt_scale;
                        if (cfg.enable_movement_compensation || ads_active) {
                            adapt_scale = calculate_adaptive_scale(aim_x, aim_y, (float)track_center_x,
                                (float)track_center_y, cfg.sensitivity_scale, input_tracker,
                                cfg.movement_boost, ads_active, cfg.ads_multiplier);
                        } else {
                            adapt_scale.scale_x = adapt_scale.scale_y = cfg.sensitivity_scale;
                        }
                        mouse.queue_move(aim_x, aim_y, (float)track_center_x, (float)track_center_y,
                            adapt_scale.scale_x, adapt_scale.scale_y, cfg.mouse_delay, cfg.pixel_step,
                            cfg.smoothing_curve, cfg.smoothing_strength);
                    }
                }
            }
            predictor.cleanup_stale_trackers(current_time);
        }
        
        // Handle auto-click with new mode system
        if (auto_click_state.should_click(cfg.auto_click_mode, cfg.auto_click_key, 
                                          is_locked, is_tracking, current_time, cfg.click_cooldown)) {
            mouse.click(BTN_LEFT);
        }
        
        // FPS counter
        double elapsed = get_time_seconds() - fps_time;
        if (elapsed >= 1.0) {
            fps_display = fps_counter / elapsed;
            inference_fps = inference_count / elapsed;
            fps_counter = inference_count = 0;
            fps_time = get_time_seconds();
            
            if (gui) {
                auto& status = gui->get_status();
                status.fps.store((float)fps_display);
                status.inference_fps.store((float)inference_fps);
            }
            
            if (!cfg.debug_window && !gui) {
                std::cout << "\rFPS: " << std::fixed << std::setprecision(1) << fps_display
                          << " | Inf: " << inference_fps << " | " << (active ? "ACTIVE" : "STANDBY")
                          << (is_locked ? " [LOCKED]" : "") 
                          << (cfg.tracking_center == 1 ? " [MOUSE]" : "")
                          << "          " << std::flush;
            }
        }
        
        // Update GUI status frequently
        if (gui && frame_count % 10 == 0) {
            auto& status = gui->get_status();
            status.active.store(active);
            status.locked.store(is_locked);
            status.ads_active.store(ads_active);
            status.auto_click_on.store(auto_click_state.is_active(cfg.auto_click_mode, cfg.auto_click_key));
            status.targets.store((int)last_detections.size());
        }
        
        // Debug window
        if (cfg.debug_window) {
            cv::Mat debug_frame = grabber.get_frame_copy();
            if (!debug_frame.empty()) {
                int fc = debug_frame.cols / 2;
                
                for (const auto& det : last_detections) {
                    cv::rectangle(debug_frame, cv::Point((int)det.x1, (int)det.y1),
                        cv::Point((int)det.x2, (int)det.y2), cv::Scalar(0, 255, 0), 2);
                    
                    int height = (int)(det.y2 - det.y1);
                    int hx = (int)det.center_x(), hy = (int)det.center_y() - height / (int)cfg.head_ratio;
                    cv::circle(debug_frame, cv::Point(hx, hy), 5, cv::Scalar(0, 255, 0), -1);
                    cv::line(debug_frame, cv::Point(hx, hy), cv::Point(fc, fc), cv::Scalar(0, 255, 255), 2);
                }
                
                cv::line(debug_frame, cv::Point(fc - 15, fc), cv::Point(fc + 15, fc), cv::Scalar(0, 255, 255), 2);
                cv::line(debug_frame, cv::Point(fc, fc - 15), cv::Point(fc, fc + 15), cv::Scalar(0, 255, 255), 2);
                
                std::ostringstream ss;
                ss << "FPS: " << std::fixed << std::setprecision(0) << fps_display << " (inf: " << inference_fps << ")";
                cv::putText(debug_frame, ss.str(), cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
                cv::putText(debug_frame, active ? "ACTIVE" : "STANDBY", cv::Point(10, 50),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, active ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 255), 2);
                
                // Show tracking mode
                std::string mode_str = (cfg.tracking_center == 1) ? "MOUSE CENTER" : "SCREEN CENTER";
                cv::putText(debug_frame, mode_str, cv::Point(10, 75),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 165, 0), 1);
                
                cv::imshow("Cat Clicker V16", debug_frame);
                if ((cv::waitKey(1) & 0xFF) == 'q') g_shutdown = true;
            }
        }
    }
    
    // Cleanup
    std::cout << "\n\nShutting down...\n";
    
    if (gui) { gui->stop(); gui.reset(); }
    grabber.stop();
    mouse.stop_move_thread();
    mouse.disconnect();
    if (cfg.debug_window) cv::destroyAllWindows();
    priority_mgr.cleanup();

#if defined(CATCLICKER_ENABLE_LICENSE)
    catclicker::antidebug::stop_periodic_check();
#endif

    console::log_ok("Done.");
    return 0;
}
