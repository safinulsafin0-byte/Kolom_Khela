cmake_minimum_required(VERSION 3.16)
project(KolomKhela LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------- raylib 5.0
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

# Android: raylib 5.0's CMake selects the Android backend via PLATFORM.
# Configure with:  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake
#                  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
if(ANDROID)
    set(PLATFORM "Android" CACHE STRING "" FORCE)
endif()

# raylib 5.0 declares an old cmake_minimum_required; CMake 4.x rejects that unless told otherwise.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# ARMv7 fix: stb_image_resize2.h picks a NEON FP16 path that 32-bit ARM clang cannot compile
# (vld1q_f16 error). The game uses no texture resizing, so SIMD is simply switched off.
if(ANDROID)
    target_compile_definitions(raylib PRIVATE STBIR_NO_SIMD)
endif()

# ---------------------------------------------------------------- game target
set(KK_SOURCES
    src/main.cpp
    src/PenPhysics.hpp
    src/SoundSynth.hpp
)

if(ANDROID)
    # The Android loader expects libmain.so (see AndroidManifest android.app.lib_name = "main").
    add_library(${PROJECT_NAME} SHARED ${KK_SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")

    set(APP_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
    add_library(native_app_glue STATIC "${APP_GLUE_DIR}/android_native_app_glue.c")
    target_include_directories(native_app_glue PUBLIC "${APP_GLUE_DIR}")

    target_link_libraries(${PROJECT_NAME} PRIVATE raylib native_app_glue android log EGL GLESv2 OpenSLES m)
    target_link_options(${PROJECT_NAME} PRIVATE -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384)
else()
    add_executable(${PROJECT_NAME} ${KK_SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
    endif()
    if(WIN32 AND NOT MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE -static)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src)
