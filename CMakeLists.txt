cmake_minimum_required(VERSION 3.20)
project(ScreenRecorder VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_definitions(-DUNICODE -D_UNICODE)

# --- CMake modules ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Find FFmpeg ---
find_package(FFmpeg REQUIRED)

# --- Sources ---
set(SOURCES
    app/main.cpp
    app/AppWindow.cpp
    app/HotkeyManager.cpp

    core/AppSettings.cpp
    core/RecorderController.cpp

    platform/Logger.cpp
    platform/D3D11Device.cpp

    ui/BackgroundRenderer.cpp

    capture/MonitorEnumerator.cpp
    capture/DesktopDuplicationSource.cpp
    capture/FrameSaver.cpp
    capture/GameWindowEnumerator.cpp
    capture/GameCaptureSource.cpp

    encoder/FFmpegEncoder.cpp

    audio/AudioCaptureWasapi.cpp
    audio/AudioMixer.cpp
    audio/AudioEncoder.cpp
)

set(RC_FILE ScreenRecorder.rc)

add_executable(ScreenRecorder ${SOURCES} ${RC_FILE})

set_target_properties(ScreenRecorder PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(ScreenRecorder PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_compile_definitions(ScreenRecorder PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Resource compiler include paths
set_target_properties(ScreenRecorder PROPERTIES
    VS_RESOURCE_GENERAL_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(ScreenRecorder PRIVATE
    d3d11.lib
    dxgi.lib
    dxguid.lib
    windowscodecs.lib
    ole32.lib
    uuid.lib
    comctl32.lib
    dwmapi.lib
    gdiplus.lib
    ${FFMPEG_LIBRARIES}
)
