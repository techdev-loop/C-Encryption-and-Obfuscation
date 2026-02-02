# Cat Clicker V15 - C++ Edition

High-performance computer vision aimbot using DXGI Desktop Duplication and TensorRT inference.

## Features

- **DXGI Desktop Duplication** - Low-latency screen capture directly from GPU
- **TensorRT Inference** - Optimized YOLO inference with GPU preprocessing
- **HID Mouse Injection** - Direct HID communication (with SendInput fallback)
- **Intel Hybrid CPU Support** - Automatic P-core/E-core detection and thread pinning
- **Movement Compensation** - Adaptive sensitivity based on player movement
- **ADS Compensation** - Adjustable multiplier when aiming down sights




### 1. Set Environment Variables

```batch
setx CUDA_PATH "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x"
setx TENSORRT_ROOT "C:\TensorRT-10.x.x.x"
setx OpenCV_DIR "C:\opencv\build"
```


```batch
mkdir build
cmake -B build -S . ^ -DCMAKE_TOOLCHAIN_FILE=C:/Users/irena/vcpkg/scripts/buildsystems/vcpkg.cmake ^ -DVCPKG_TARGET_TRIPLET=x64-windows ^ -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

```

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--debug`, `-d` | Show debug window with detections | Off |
| `--engine <path>` | Path to TensorRT engine file | `best.engine` |
| `--confidence <val>` | Detection confidence threshold | 0.3 |
| `--sensitivity <val>` | Mouse sensitivity scale | 0.1 |
| `--help`, `-h` | Show help message | - |

## Controls

| Key | Action |
|-----|--------|
| Caps Lock (0x14) | Trigger (hold to aim) |
| A (0x41) | Strafe left detection |
| D (0x44) | Strafe right detection |
| Space (0x20) | Jump detection |
| Right Mouse (0x02) | ADS detection |
| Q / ESC | Quit (in debug window) |

## Configuration

Edit `include/config.h` to change default values:

```cpp
// Capture settings
constexpr int CAPTURE_SIZE = 320;
constexpr int MODEL_SIZE = 320;

// Mouse settings
constexpr float DEFAULT_SENSITIVITY = 0.1f;
constexpr int DEFAULT_PIXEL_STEP = 4;

// Detection settings
constexpr float DEFAULT_CONFIDENCE = 0.3f;
constexpr float DEFAULT_HEAD_RATIO = 2.7f;

// Key bindings
constexpr int DEFAULT_TRIGGER_KEY = 0x14;  // Caps Lock
```


cmake -B build -S . ^ -DCMAKE_TOOLCHAIN_FILE=C:/Users/irena/vcpkg/scripts/buildsystems/vcpkg.cmake ^ -DVCPKG_TARGET_TRIPLET=x64-windows ^ -DCMAKE_BUILD_TYPE=Release

## License

For personal/educational use only.
