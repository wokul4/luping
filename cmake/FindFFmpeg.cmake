# FindFFmpeg.cmake
cmake_policy(PUSH)
cmake_policy(SET CMP0144 OLD)

# --- Resolve FFMPEG_ROOT ---
if(NOT DEFINED FFMPEG_ROOT)
    if(DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
    endif()
endif()
if(NOT FFMPEG_ROOT)
    foreach(_p "C:/ffmpeg" "D:/ffmpeg" "C:/Program Files/ffmpeg" "C:/tools/ffmpeg")
        if(EXISTS "${_p}/include/libavformat/avformat.h")
            set(FFMPEG_ROOT "${_p}")
            break()
        endif()
    endforeach()
endif()

message(STATUS "FFMPEG_ROOT: '${FFMPEG_ROOT}'")

# --- Find components by direct path check ---
set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARIES)

if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")

    foreach(_comp avformat avcodec avutil swscale swresample)
        if(EXISTS "${FFMPEG_ROOT}/lib/${_comp}.lib")
            list(APPEND FFMPEG_LIBRARIES "${FFMPEG_ROOT}/lib/${_comp}.lib")
        elseif(EXISTS "${FFMPEG_ROOT}/lib/${_comp}.dll.a")
            list(APPEND FFMPEG_LIBRARIES "${FFMPEG_ROOT}/lib/${_comp}.dll.a")
        endif()
    endforeach()
endif()

message(STATUS "FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFMPEG_LIBRARIES:    ${FFMPEG_LIBRARIES}")

# --- Report ---
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES
)
mark_as_advanced(FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES)

cmake_policy(POP)
