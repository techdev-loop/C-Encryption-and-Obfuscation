cmake_minimum_required(VERSION 3.20)
project(CatClicker LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# License / HWID binding and anti-debug (optional; ON for release)
option(CATCLICKER_ENABLE_LICENSE "Enable license validation, HWID binding, and anti-debug" ON)

set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG /GL /Oi /Ot /Gy")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 --use_fast_math")
set(CMAKE_CUDA_ARCHITECTURES 86)

find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED)

# TensorRT
if(DEFINED ENV{TENSORRT_ROOT})
    set(TENSORRT_ROOT $ENV{TENSORRT_ROOT})
else()
    file(GLOB TENSORRT_SEARCH_PATHS "C:/TensorRT/TensorRT-*" "C:/TensorRT-*" "D:/TensorRT/TensorRT-*")
    if(TENSORRT_SEARCH_PATHS)
        list(GET TENSORRT_SEARCH_PATHS 0 TENSORRT_ROOT)
    endif()
endif()

if(NOT TENSORRT_ROOT)
    message(FATAL_ERROR "TensorRT not found. Set TENSORRT_ROOT environment variable.")
endif()

set(TENSORRT_INCLUDE_DIRS "${TENSORRT_ROOT}/include")
set(TENSORRT_LIB_DIR "${TENSORRT_ROOT}/lib")

find_library(NVINFER_LIB NAMES nvinfer_10 nvinfer HINTS ${TENSORRT_LIB_DIR})
find_library(NVINFER_PLUGIN_LIB NAMES nvinfer_plugin_10 nvinfer_plugin HINTS ${TENSORRT_LIB_DIR})
find_library(NVONNXPARSER_LIB NAMES nvonnxparser_10 nvonnxparser HINTS ${TENSORRT_LIB_DIR})

# hidapi
find_path(HIDAPI_INCLUDE_DIR hidapi/hidapi.h
    HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/include" "C:/vcpkg/installed/x64-windows/include")
find_library(HIDAPI_LIBRARY hidapi
    HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/lib" "C:/vcpkg/installed/x64-windows/lib")

if(HIDAPI_INCLUDE_DIR AND HIDAPI_LIBRARY)
    set(HIDAPI_FOUND TRUE)
else()
    set(HIDAPI_FOUND FALSE)
endif()

# Dear ImGui - check vcpkg first, then local
find_path(IMGUI_INCLUDE_DIR imgui.h
    HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/include" "C:/vcpkg/installed/x64-windows/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui" "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp")
    set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
    set(IMGUI_LOCAL TRUE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp")
    set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
    set(IMGUI_LOCAL TRUE)
else()
    set(IMGUI_LOCAL FALSE)
    find_library(IMGUI_LIBRARY imgui
        HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/lib" "C:/vcpkg/installed/x64-windows/lib")
endif()

if(IMGUI_LOCAL)
    set(IMGUI_SOURCES
        ${IMGUI_SOURCE_DIR}/imgui.cpp
        ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
        ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
        ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
        ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
    set(IMGUI_BACKEND_INCLUDE "${IMGUI_SOURCE_DIR}/backends")
    set(IMGUI_INCLUDE_DIR "${IMGUI_SOURCE_DIR}")
else()
    set(IMGUI_SOURCES "")
    set(IMGUI_BACKEND_INCLUDE "${IMGUI_INCLUDE_DIR}")
endif()

set(SOURCES
    src/main.cpp src/frame_grabber.cpp src/inference_engine.cpp src/mouse_controller.cpp
    src/input_tracker.cpp src/target_selector.cpp src/target_predictor.cpp
    src/gpu_preprocessor.cu src/priority_manager.cpp src/gui.cpp
    src/hwid.cpp src/license_client.cpp src/antidebug.cpp
    ${IMGUI_SOURCES})

set(HEADERS
    include/frame_grabber.h include/inference_engine.h include/mouse_controller.h
    include/input_tracker.h include/target_selector.h include/target_predictor.h
    include/gpu_preprocessor.h include/priority_manager.h include/config.h
    include/common.h include/gui.h include/hwid.h include/license_client.h include/antidebug.h include/obfuscate.h)

add_executable(cat_clicker ${SOURCES} ${HEADERS})

target_include_directories(cat_clicker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS} ${CUDAToolkit_INCLUDE_DIRS}
    ${TENSORRT_INCLUDE_DIRS} ${IMGUI_INCLUDE_DIR} ${IMGUI_BACKEND_INCLUDE})

if(HIDAPI_FOUND)
    target_include_directories(cat_clicker PRIVATE ${HIDAPI_INCLUDE_DIR})
    target_compile_definitions(cat_clicker PRIVATE HIDAPI_AVAILABLE)
endif()

target_link_libraries(cat_clicker PRIVATE
    ${OpenCV_LIBS} CUDA::cudart CUDA::cuda_driver
    ${NVINFER_LIB} ${NVINFER_PLUGIN_LIB} ${NVONNXPARSER_LIB}
    d3d11 dxgi user32 kernel32 dwmapi winhttp)

if(CATCLICKER_ENABLE_LICENSE)
    target_compile_definitions(cat_clicker PRIVATE CATCLICKER_ENABLE_LICENSE)
endif()

if(HIDAPI_FOUND)
    target_link_libraries(cat_clicker PRIVATE ${HIDAPI_LIBRARY})
endif()

if(NOT IMGUI_LOCAL AND IMGUI_LIBRARY)
    target_link_libraries(cat_clicker PRIVATE ${IMGUI_LIBRARY})
endif()

file(GLOB TRT_DLLS "${TENSORRT_ROOT}/lib/*.dll")
foreach(DLL ${TRT_DLLS})
    add_custom_command(TARGET cat_clicker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:cat_clicker>)
endforeach()

message(STATUS "OpenCV: ${OpenCV_VERSION}, CUDA: ${CUDAToolkit_VERSION}, ImGui local: ${IMGUI_LOCAL}")
