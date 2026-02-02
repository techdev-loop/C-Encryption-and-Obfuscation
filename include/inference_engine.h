#pragma once

#include "common.h"
#include "gpu_preprocessor.h"
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime.h>
#include <vector>
#include <string>
#include <memory>

namespace catclicker {

// TensorRT logger
class TrtLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
    void set_verbose(bool verbose) { verbose_ = verbose; }
private:
    bool verbose_ = false;
};

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();
    
    // Load model - accepts .engine, .onnx, or .pt
    bool load_model(const std::string& model_path, int imgsz = 320, bool half = true);
    
    // Warmup the engine
    void warmup(int iterations = 3);
    
    // Run inference on a frame
    std::vector<Detection> predict(
        const uint8_t* frame_data,
        int width,
        int height,
        float conf_threshold = 0.3f,
        int max_detections = 10
    );
    
    int get_input_width() const { return input_width_; }
    int get_input_height() const { return input_height_; }
    
    bool is_loaded() const { return engine_ != nullptr; }

private:
    bool build_engine_from_onnx(const std::string& onnx_path, const std::string& engine_path, 
                                int imgsz, bool half);
    bool load_engine(const std::string& engine_path);
    bool convert_pt_to_onnx(const std::string& pt_path, const std::string& onnx_path, int imgsz);
    
    bool allocate_buffers();
    void process_output(float* output_data, float conf_threshold, int max_det,
                       std::vector<Detection>& detections);
    
    TrtLogger logger_;
    
    // TensorRT objects
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;
    
    // GPU preprocessor
    std::unique_ptr<GpuPreprocessor> preprocessor_;
    
    // CUDA stream
    cudaStream_t stream_ = nullptr;
    
    // Buffer management
    void* d_input_ = nullptr;
    void* d_output_ = nullptr;
    float* h_output_ = nullptr;
    
    size_t input_size_ = 0;
    size_t output_size_ = 0;
    int output_elements_ = 0;
    
    // Model dimensions
    int input_width_ = 0;
    int input_height_ = 0;
    int input_channels_ = 3;
    
    // Output dimensions for YOLOv8 parsing
    // Format: [1, num_channels, num_boxes] e.g., [1, 5, 8400] or [1, 84, 8400]
    int output_dim1_ = 0;  // num_channels (4 + num_classes)
    int output_dim2_ = 0;  // num_boxes
    
    // Binding indices
    int input_binding_idx_ = -1;
    int output_binding_idx_ = -1;
};

} // namespace catclicker
