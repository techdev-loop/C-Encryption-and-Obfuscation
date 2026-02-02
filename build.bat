@echo off
setlocal EnableDelayedExpansion

echo.
echo ============================================
echo   Cat Clicker C++ Build Script
echo ============================================
echo.

:: Check if cl.exe is already available
where cl >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] MSVC compiler found in PATH
    goto :compiler_ready
)

:: Try to find and initialize Visual Studio
echo [INFO] MSVC not in PATH, searching for Visual Studio...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)

:: Try common VS 2022 paths
if not defined VS_PATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    )
)

:: Try VS 2019 as fallback
if not defined VS_PATH (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
    )
)

if not defined VS_PATH (
    echo.
    echo [ERROR] Visual Studio not found!
    echo.
    echo Please install Visual Studio 2022 with C++ workload:
    echo   1. Download from: https://visualstudio.microsoft.com/vs/community/
    echo   2. During installation, select "Desktop development with C++"
    echo.
    echo Or install just the Build Tools:
    echo   winget install Microsoft.VisualStudio.2022.BuildTools
    echo.
    pause
    exit /b 1
)

echo [OK] Found Visual Studio: %VS_PATH%

:: Initialize the compiler environment
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found at: %VCVARS%
    echo Please ensure C++ tools are installed in Visual Studio.
    pause
    exit /b 1
)

echo [INFO] Initializing MSVC compiler environment...
call "%VCVARS%" >nul 2>&1

:: Verify compiler is now available
where cl >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to initialize compiler!
    pause
    exit /b 1
)

:compiler_ready
echo [OK] MSVC compiler ready

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake not found!
    echo.
    echo Install CMake:
    echo   winget install Kitware.CMake
    echo.
    echo Or download from: https://cmake.org/download/
    echo.
    pause
    exit /b 1
)
echo [OK] CMake found

:: Check for CUDA
if not defined CUDA_PATH (
    echo [INFO] CUDA_PATH not set, searching...
    for /d %%i in ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.*") do set "CUDA_PATH=%%i"
    for /d %%i in ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.*") do set "CUDA_PATH=%%i"
)

if not defined CUDA_PATH (
    echo [ERROR] CUDA not found!
    echo Download from: https://developer.nvidia.com/cuda-downloads
    pause
    exit /b 1
)
echo [OK] CUDA_PATH: %CUDA_PATH%

:: Check for TensorRT
if not defined TENSORRT_ROOT (
    echo [INFO] TENSORRT_ROOT not set, searching...
    for /d %%i in (C:\TensorRT*) do set "TENSORRT_ROOT=%%i"
    for /d %%i in (D:\TensorRT*) do set "TENSORRT_ROOT=%%i"
)

if not defined TENSORRT_ROOT (
    echo [ERROR] TensorRT not found!
    echo Download from: https://developer.nvidia.com/tensorrt
    echo Extract and set TENSORRT_ROOT environment variable.
    pause
    exit /b 1
)
echo [OK] TENSORRT_ROOT: %TENSORRT_ROOT%

:: Check for OpenCV
if not defined OpenCV_DIR (
    echo [INFO] OpenCV_DIR not set, searching...
    if exist "C:\opencv\build" set "OpenCV_DIR=C:\opencv\build"
    if exist "C:\vcpkg\installed\x64-windows\share\opencv4" set "OpenCV_DIR=C:\vcpkg\installed\x64-windows\share\opencv4"
    if exist "%USERPROFILE%\vcpkg\installed\x64-windows\share\opencv4" set "OpenCV_DIR=%USERPROFILE%\vcpkg\installed\x64-windows\share\opencv4"
)

if not defined OpenCV_DIR (
    echo.
    echo [ERROR] OpenCV not found!
    echo.
    echo Install via vcpkg:
    echo   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
    echo   C:\vcpkg\bootstrap-vcpkg.bat
    echo   C:\vcpkg\vcpkg install opencv4:x64-windows
    echo.
    echo Then set: OpenCV_DIR=C:\vcpkg\installed\x64-windows\share\opencv4
    echo.
    pause
    exit /b 1
)
echo [OK] OpenCV_DIR: %OpenCV_DIR%

:: Detect Visual Studio version for CMake generator
set "CMAKE_GENERATOR=Visual Studio 17 2022"
echo "%VS_PATH%" | findstr /i "2019" >nul && set "CMAKE_GENERATOR=Visual Studio 16 2019"

echo [OK] Using CMake generator: %CMAKE_GENERATOR%

:: Create build directory
if not exist build mkdir build
cd build

echo.
echo ============================================
echo   Running CMake Configuration
echo ============================================
echo.

cmake .. -G "%CMAKE_GENERATOR%" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DOpenCV_DIR="%OpenCV_DIR%" ^
    -DTENSORRT_ROOT="%TENSORRT_ROOT%"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Common fixes:
    echo   - Make sure all dependencies are installed
    echo   - Check that environment variables are correct
    echo   - Try deleting the 'build' folder and running again
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Building Project
echo ============================================
echo.

cmake --build . --config Release --parallel

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed!
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   BUILD SUCCESSFUL!
echo ============================================
echo.
echo Executable location:
echo   %cd%\Release\cat_clicker.exe
echo.
echo Next steps:
echo   1. Copy your 'best.engine' model file to: %cd%\Release\
echo   2. Run: Release\cat_clicker.exe --debug
echo.

cd ..
pause
