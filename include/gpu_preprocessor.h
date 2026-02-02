#pragma once

#include <cuda_runtime.h>
#include <cstdint>

namespace catclicker {

class GpuPreprocessor {
public:
    GpuPreprocessor(int width, int height);
    ~GpuPreprocessor();
    
    // Preprocess frame: BGR uint8 HWC -> RGB float32 NCHW, normalized to [0,1]
    // Input: host pointer to BGR image (width * height * 3 bytes)
    // Output: device pointer to preprocessed tensor (1 * 3 * height * width floats)
    bool preprocess(const uint8_t* host_bgr, float* device_output, cudaStream_t stream = nullptr);
    
    // Get device buffer for direct GPU-to-GPU operations
    float* get_device_buffer() { return d_output_; }
    
    // Get buffer size in bytes
    size_t get_output_size_bytes() const { return output_size_bytes_; }
    
    bool is_initialized() const { return initialized_; }

private:
    int width_;
    int height_;
    size_t input_size_bytes_;
    size_t output_size_bytes_;
    
    // Device buffers
    uint8_t* d_input_ = nullptr;
    float* d_output_ = nullptr;
    
    bool initialized_ = false;
};

// CUDA kernel launch wrapper (implemented in .cu file)
void launch_preprocess_kernel(
    const uint8_t* d_input,
    float* d_output,
    int width,
    int height,
    cudaStream_t stream
);

} // namespace catclicker
