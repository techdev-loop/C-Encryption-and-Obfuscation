#include "gpu_preprocessor.h"
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>

namespace catclicker {

// CUDA kernel: BGR uint8 HWC -> RGB float32 NCHW with normalization
__global__ void preprocess_kernel(
    const uint8_t* __restrict__ input,  // BGR HWC
    float* __restrict__ output,          // RGB NCHW
    int width,
    int height
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= width || y >= height) return;
    
    int pixel_idx = y * width + x;
    int input_idx = pixel_idx * 3;
    int plane_size = width * height;
    
    // Read BGR
    float b = input[input_idx + 0];
    float g = input[input_idx + 1];
    float r = input[input_idx + 2];
    
    // Normalize to [0, 1]
    const float scale = 1.0f / 255.0f;
    
    // Write as RGB NCHW (channel-first, RGB order)
    output[0 * plane_size + pixel_idx] = r * scale;  // R channel
    output[1 * plane_size + pixel_idx] = g * scale;  // G channel
    output[2 * plane_size + pixel_idx] = b * scale;  // B channel
}

void launch_preprocess_kernel(
    const uint8_t* d_input,
    float* d_output,
    int width,
    int height,
    cudaStream_t stream
) {
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    
    preprocess_kernel<<<grid, block, 0, stream>>>(d_input, d_output, width, height);
}

GpuPreprocessor::GpuPreprocessor(int width, int height)
    : width_(width), height_(height) {
    
    input_size_bytes_ = width * height * 3 * sizeof(uint8_t);
    output_size_bytes_ = 1 * 3 * width * height * sizeof(float);  // NCHW
    
    cudaError_t err;
    
    // Allocate device input buffer
    err = cudaMalloc(&d_input_, input_size_bytes_);
    if (err != cudaSuccess) {
        printf("[GpuPreprocessor] Failed to allocate input buffer: %s\n", cudaGetErrorString(err));
        return;
    }
    
    // Allocate device output buffer
    err = cudaMalloc(&d_output_, output_size_bytes_);
    if (err != cudaSuccess) {
        printf("[GpuPreprocessor] Failed to allocate output buffer: %s\n", cudaGetErrorString(err));
        cudaFree(d_input_);
        d_input_ = nullptr;
        return;
    }
    
    initialized_ = true;
    printf("[GpuPreprocessor] Initialized %dx%d buffers\n", width, height);
}

GpuPreprocessor::~GpuPreprocessor() {
    if (d_input_) {
        cudaFree(d_input_);
        d_input_ = nullptr;
    }
    if (d_output_) {
        cudaFree(d_output_);
        d_output_ = nullptr;
    }
}

bool GpuPreprocessor::preprocess(const uint8_t* host_bgr, float* device_output, cudaStream_t stream) {
    if (!initialized_) return false;
    
    cudaError_t err;
    
    // Copy input to device
    if (stream) {
        err = cudaMemcpyAsync(d_input_, host_bgr, input_size_bytes_, cudaMemcpyHostToDevice, stream);
    } else {
        err = cudaMemcpy(d_input_, host_bgr, input_size_bytes_, cudaMemcpyHostToDevice);
    }
    
    if (err != cudaSuccess) {
        return false;
    }
    
    // Run preprocessing kernel
    float* output = device_output ? device_output : d_output_;
    launch_preprocess_kernel(d_input_, output, width_, height_, stream);
    
    return true;
}

} // namespace catclicker
