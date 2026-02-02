#include "inference_engine.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace catclicker {

void TrtLogger::log(Severity severity, const char* msg) noexcept {
    if (severity == Severity::kERROR) {
        console::log_error(std::string("[TensorRT] ") + msg);
    } else if (severity == Severity::kWARNING) {
        console::log_warn(std::string("[TensorRT] ") + msg);
    } else if (verbose_ && severity == Severity::kINFO) {
        console::log_info(std::string("[TensorRT] ") + msg);
    }
}

InferenceEngine::InferenceEngine() {
    logger_.set_verbose(true);
}

InferenceEngine::~InferenceEngine() {
    if (stream_) {
        cudaStreamDestroy(stream_);
    }
    if (d_input_) cudaFree(d_input_);
    if (d_output_) cudaFree(d_output_);
    if (h_output_) delete[] h_output_;
}

bool InferenceEngine::convert_pt_to_onnx(const std::string& pt_path, const std::string& onnx_path, int imgsz) {
    console::log_info("Converting .pt to .onnx using Python...");
    
    // Build Python command - use opset 12 for better compatibility
    std::string cmd = "python -c \"from ultralytics import YOLO; "
                     "model = YOLO('" + pt_path + "'); "
                     "model.export(format='onnx', imgsz=" + std::to_string(imgsz) + ", "
                     "half=False, simplify=True, opset=12)\"";
    
    console::log_info("Running: " + cmd);
    
    int result = system(cmd.c_str());
    
    if (result != 0) {
        console::log_error("Failed to convert .pt to .onnx");
        console::log_error("Make sure Python and ultralytics are installed:");
        console::log_error("  pip install ultralytics");
        return false;
    }
    
    // Check if ONNX file was created (ultralytics puts it next to the .pt file)
    std::string expected_onnx = pt_path.substr(0, pt_path.length() - 3) + ".onnx";
    if (fs::exists(expected_onnx)) {
        if (expected_onnx != onnx_path) {
            fs::copy_file(expected_onnx, onnx_path, fs::copy_options::overwrite_existing);
        }
        console::log_ok("Created: " + onnx_path);
        return true;
    }
    
    console::log_error("ONNX file not found after conversion");
    return false;
}

bool InferenceEngine::build_engine_from_onnx(const std::string& onnx_path, const std::string& engine_path,
                                              int imgsz, bool half) {
    console::log_info("Building TensorRT engine from ONNX...");
    console::log_info("This may take a few minutes on first run...");
    
    // Create builder
    auto builder = std::unique_ptr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(logger_));
    if (!builder) {
        console::log_error("Failed to create TensorRT builder");
        return false;
    }
    
    // Create network with explicit batch
    const auto explicitBatch = 1U << static_cast<uint32_t>(
        nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
    auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(
        builder->createNetworkV2(explicitBatch));
    if (!network) {
        console::log_error("Failed to create network");
        return false;
    }
    
    // Create ONNX parser
    auto parser = std::unique_ptr<nvonnxparser::IParser>(
        nvonnxparser::createParser(*network, logger_));
    if (!parser) {
        console::log_error("Failed to create ONNX parser");
        return false;
    }
    
    // Parse ONNX file
    console::log_info("Parsing ONNX model: " + onnx_path);
    if (!parser->parseFromFile(onnx_path.c_str(), 
            static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        console::log_error("Failed to parse ONNX file");
        for (int i = 0; i < parser->getNbErrors(); i++) {
            console::log_error(std::string("  ") + parser->getError(i)->desc());
        }
        return false;
    }
    console::log_ok("ONNX model parsed successfully");
    
    // Print network info
    console::log_info("Network inputs: " + std::to_string(network->getNbInputs()));
    console::log_info("Network outputs: " + std::to_string(network->getNbOutputs()));
    
    for (int i = 0; i < network->getNbOutputs(); i++) {
        auto output = network->getOutput(i);
        auto dims = output->getDimensions();
        std::string dims_str;
        for (int d = 0; d < dims.nbDims; d++) {
            dims_str += std::to_string(dims.d[d]);
            if (d < dims.nbDims - 1) dims_str += "x";
        }
        console::log_info("Output " + std::to_string(i) + ": " + std::string(output->getName()) + " [" + dims_str + "]");
    }
    
    // Create builder config
    auto config = std::unique_ptr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
    if (!config) {
        console::log_error("Failed to create builder config");
        return false;
    }
    
    // Set memory pool limit (8GB workspace)
    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 8ULL << 30);
    
    // Enable FP16 if requested and supported
    if (half && builder->platformHasFastFp16()) {
        config->setFlag(nvinfer1::BuilderFlag::kFP16);
        console::log_info("FP16 mode enabled");
    }
    
    // Build engine
    console::log_info("Building engine (this takes 1-5 minutes)...");
    auto serialized = std::unique_ptr<nvinfer1::IHostMemory>(
        builder->buildSerializedNetwork(*network, *config));
    if (!serialized) {
        console::log_error("Failed to build engine");
        return false;
    }
    
    // Save engine to file
    std::ofstream engine_file(engine_path, std::ios::binary);
    if (!engine_file) {
        console::log_error("Failed to open engine file for writing: " + engine_path);
        return false;
    }
    engine_file.write(static_cast<const char*>(serialized->data()), serialized->size());
    engine_file.close();
    
    console::log_ok("Engine saved: " + engine_path + " (" + 
                   std::to_string(serialized->size() / 1024 / 1024) + " MB)");
    
    return true;
}

bool InferenceEngine::load_model(const std::string& model_path, int imgsz, bool half) {
    // Print TensorRT version
    console::log_info("TensorRT version: " + std::to_string(NV_TENSORRT_MAJOR) + "." +
                     std::to_string(NV_TENSORRT_MINOR) + "." + std::to_string(NV_TENSORRT_PATCH));
    
    std::string base_path = model_path;
    std::string extension = "";
    
    // Get file extension
    size_t dot_pos = model_path.rfind('.');
    if (dot_pos != std::string::npos) {
        extension = model_path.substr(dot_pos);
        base_path = model_path.substr(0, dot_pos);
    }
    
    std::string pt_path = base_path + ".pt";
    std::string onnx_path = base_path + ".onnx";
    std::string engine_path = base_path + ".engine";
    
    // Try loading existing engine
    if (fs::exists(engine_path)) {
        console::log_info("Found existing engine: " + engine_path);
        if (load_engine(engine_path)) {
            return true;
        }
        console::log_warn("Failed to load existing engine, will rebuild...");
        fs::remove(engine_path);
    }
    
    // Need to build engine - check for ONNX
    if (!fs::exists(onnx_path)) {
        if (fs::exists(pt_path)) {
            console::log_info("Found .pt model, converting to ONNX first...");
            if (!convert_pt_to_onnx(pt_path, onnx_path, imgsz)) {
                return false;
            }
        } else if (extension == ".pt" && fs::exists(model_path)) {
            if (!convert_pt_to_onnx(model_path, onnx_path, imgsz)) {
                return false;
            }
        } else {
            console::log_error("No model file found!");
            console::log_error("Please provide one of: " + pt_path + ", " + onnx_path + ", or " + engine_path);
            return false;
        }
    }
    
    // Build engine from ONNX
    if (!fs::exists(onnx_path)) {
        console::log_error("ONNX file not found: " + onnx_path);
        return false;
    }
    
    if (!build_engine_from_onnx(onnx_path, engine_path, imgsz, half)) {
        return false;
    }
    
    return load_engine(engine_path);
}

bool InferenceEngine::load_engine(const std::string& engine_path) {
    std::ifstream file(engine_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        console::log_error("Failed to open engine file: " + engine_path);
        return false;
    }
    
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (file_size == 0) {
        console::log_error("Engine file is empty!");
        return false;
    }
    
    std::vector<char> engine_data(file_size);
    if (!file.read(engine_data.data(), file_size)) {
        console::log_error("Failed to read engine file");
        return false;
    }
    file.close();
    
    console::log_info("Loading engine: " + engine_path + " (" + 
                     std::to_string(file_size / 1024 / 1024) + " MB)");
    
    runtime_.reset(nvinfer1::createInferRuntime(logger_));
    if (!runtime_) {
        console::log_error("Failed to create TensorRT runtime");
        return false;
    }
    
    engine_.reset(runtime_->deserializeCudaEngine(engine_data.data(), file_size));
    if (!engine_) {
        console::log_error("Failed to deserialize engine (version mismatch?)");
        return false;
    }
    
    context_.reset(engine_->createExecutionContext());
    if (!context_) {
        console::log_error("Failed to create execution context");
        return false;
    }
    
    int least_priority, greatest_priority;
    cudaDeviceGetStreamPriorityRange(&least_priority, &greatest_priority);
    cudaStreamCreateWithPriority(&stream_, cudaStreamNonBlocking, greatest_priority);
    
    // Get binding information
    int num_io = engine_->getNbIOTensors();
    console::log_info("Engine has " + std::to_string(num_io) + " IO tensors");
    
    for (int i = 0; i < num_io; i++) {
        const char* name = engine_->getIOTensorName(i);
        auto mode = engine_->getTensorIOMode(name);
        auto dims = engine_->getTensorShape(name);
        
        std::string dims_str;
        for (int d = 0; d < dims.nbDims; d++) {
            dims_str += std::to_string(dims.d[d]);
            if (d < dims.nbDims - 1) dims_str += "x";
        }
        
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
            input_binding_idx_ = i;
            if (dims.nbDims >= 4) {
                input_channels_ = dims.d[1];
                input_height_ = dims.d[2];
                input_width_ = dims.d[3];
            }
            console::log_info("Input '" + std::string(name) + "': " + dims_str);
        } else {
            output_binding_idx_ = i;
            output_elements_ = 1;
            for (int d = 0; d < dims.nbDims; d++) {
                output_elements_ *= dims.d[d];
            }
            
            // Store output dimensions for parsing
            if (dims.nbDims >= 2) {
                output_dim1_ = dims.d[1];
                output_dim2_ = dims.d[2];
            }
            
            console::log_info("Output '" + std::string(name) + "': " + dims_str);
        }
    }
    
    if (input_binding_idx_ < 0 || output_binding_idx_ < 0) {
        console::log_error("Could not find input/output bindings");
        return false;
    }
    
    if (!allocate_buffers()) {
        return false;
    }
    
    preprocessor_ = std::make_unique<GpuPreprocessor>(input_width_, input_height_);
    if (!preprocessor_->is_initialized()) {
        console::log_error("Failed to initialize GPU preprocessor");
        return false;
    }
    
    console::log_ok("Engine loaded successfully");
    console::log_info("Model input: " + std::to_string(input_width_) + "x" + 
                     std::to_string(input_height_) + "x" + std::to_string(input_channels_));
    
    return true;
}

bool InferenceEngine::allocate_buffers() {
    input_size_ = 1 * input_channels_ * input_height_ * input_width_ * sizeof(float);
    cudaError_t err = cudaMalloc(&d_input_, input_size_);
    if (err != cudaSuccess) {
        console::log_error("Failed to allocate input buffer");
        return false;
    }
    
    output_size_ = output_elements_ * sizeof(float);
    err = cudaMalloc(&d_output_, output_size_);
    if (err != cudaSuccess) {
        console::log_error("Failed to allocate output buffer");
        return false;
    }
    
    h_output_ = new float[output_elements_];
    
    console::log_info("Allocated buffers: input=" + std::to_string(input_size_ / 1024) + 
                     "KB, output=" + std::to_string(output_size_ / 1024) + "KB");
    
    return true;
}

void InferenceEngine::warmup(int iterations) {
    if (!engine_) return;
    
    console::log_info("Warming up model...");
    
    std::vector<uint8_t> dummy(input_width_ * input_height_ * 3, 128);
    
    for (int i = 0; i < iterations; i++) {
        predict(dummy.data(), input_width_, input_height_, 0.5f, 1);
    }
    
    cudaStreamSynchronize(stream_);
    console::log_ok("Warmup complete");
}

std::vector<Detection> InferenceEngine::predict(
    const uint8_t* frame_data,
    int width,
    int height,
    float conf_threshold,
    int max_detections
) {
    std::vector<Detection> detections;
    
    if (!engine_ || !context_) {
        return detections;
    }
    
    if (!preprocessor_->preprocess(frame_data, static_cast<float*>(d_input_), stream_)) {
        console::log_error("Preprocessing failed");
        return detections;
    }
    
    const char* input_name = engine_->getIOTensorName(input_binding_idx_);
    const char* output_name = engine_->getIOTensorName(output_binding_idx_);
    
    context_->setTensorAddress(input_name, d_input_);
    context_->setTensorAddress(output_name, d_output_);
    
    bool success = context_->enqueueV3(stream_);
    if (!success) {
        console::log_error("Inference failed");
        return detections;
    }
    
    cudaMemcpyAsync(h_output_, d_output_, output_size_, cudaMemcpyDeviceToHost, stream_);
    cudaStreamSynchronize(stream_);
    
    process_output(h_output_, conf_threshold, max_detections, detections);
    
    return detections;
}

void InferenceEngine::process_output(
    float* output_data,
    float conf_threshold,
    int max_det,
    std::vector<Detection>& detections
) {
    // YOLOv8 output format: [1, num_classes + 4, num_boxes]
    // For single class: [1, 5, 8400] where 5 = 4 (xywh) + 1 (class conf)
    // For COCO: [1, 84, 8400] where 84 = 4 (xywh) + 80 (class confs)
    //
    // Data layout (transposed): 
    //   output_data[0 * num_boxes + i] = x center
    //   output_data[1 * num_boxes + i] = y center  
    //   output_data[2 * num_boxes + i] = width
    //   output_data[3 * num_boxes + i] = height
    //   output_data[4 * num_boxes + i] = class0 confidence
    //   output_data[5 * num_boxes + i] = class1 confidence (if multi-class)
    //   ...
    
    int num_channels = output_dim1_;  // e.g., 5 or 84
    int num_boxes = output_dim2_;     // e.g., 8400
    int num_classes = num_channels - 4;
    
    
    if (num_channels < 5 || num_boxes < 1) {
        console::log_error("Invalid output dimensions");
        return;
    }
    
    std::vector<Detection> candidates;
    candidates.reserve(100);
    
    for (int i = 0; i < num_boxes; i++) {
        // Get box coordinates (transposed format)
        float x = output_data[0 * num_boxes + i];
        float y = output_data[1 * num_boxes + i];
        float w = output_data[2 * num_boxes + i];
        float h = output_data[3 * num_boxes + i];
        
        // Find max class confidence
        float max_conf = 0.0f;
        int max_class = 0;
        for (int c = 0; c < num_classes; c++) {
            float conf = output_data[(4 + c) * num_boxes + i];
            if (conf > max_conf) {
                max_conf = conf;
                max_class = c;
            }
        }
        
        if (max_conf < conf_threshold) continue;
        
        // Convert center format to corner format
        Detection det;
        det.x1 = x - w / 2.0f;
        det.y1 = y - h / 2.0f;
        det.x2 = x + w / 2.0f;
        det.y2 = y + h / 2.0f;
        det.confidence = max_conf;
        det.class_id = max_class;
        
        // Clamp to image bounds
        det.x1 = clamp(det.x1, 0.0f, static_cast<float>(input_width_));
        det.y1 = clamp(det.y1, 0.0f, static_cast<float>(input_height_));
        det.x2 = clamp(det.x2, 0.0f, static_cast<float>(input_width_));
        det.y2 = clamp(det.y2, 0.0f, static_cast<float>(input_height_));
        
        // Filter out invalid boxes
        if (det.width() < 1 || det.height() < 1) continue;
        
        candidates.push_back(det);
    }
    
    // Sort by confidence
    std::sort(candidates.begin(), candidates.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        });
    
    // NMS
    std::vector<bool> suppressed(candidates.size(), false);
    
    for (size_t i = 0; i < candidates.size() && detections.size() < static_cast<size_t>(max_det); i++) {
        if (suppressed[i]) continue;
        
        detections.push_back(candidates[i]);
        
        for (size_t j = i + 1; j < candidates.size(); j++) {
            if (suppressed[j]) continue;
            
            float ix1 = (std::max)(candidates[i].x1, candidates[j].x1);
            float iy1 = (std::max)(candidates[i].y1, candidates[j].y1);
            float ix2 = (std::min)(candidates[i].x2, candidates[j].x2);
            float iy2 = (std::min)(candidates[i].y2, candidates[j].y2);
            
            float inter_w = (std::max)(0.0f, ix2 - ix1);
            float inter_h = (std::max)(0.0f, iy2 - iy1);
            float inter_area = inter_w * inter_h;
            
            float area_i = candidates[i].width() * candidates[i].height();
            float area_j = candidates[j].width() * candidates[j].height();
            float union_area = area_i + area_j - inter_area;
            
            float iou = inter_area / (union_area + 1e-6f);
            
            if (iou > 0.45f) {
                suppressed[j] = true;
            }
        }
    }
}

} // namespace catclicker
