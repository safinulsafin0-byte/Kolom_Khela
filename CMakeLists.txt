cmake_minimum_required(VERSION 3.22)
project(KolomKhela LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND NOT ANDROID)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# raylib (fetched automatically). Keep this tag the same one your main.cpp
# already compiles with. 5.5 is the stable release that ships stb_image_resize2
# (the same header that was failing on armeabi-v7a).
# ---------------------------------------------------------------------------
set(RAYLIB_TAG "5.5" CACHE STRING "raylib git tag")

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(ANDROID)
    set(PLATFORM "Android" CACHE STRING "" FORCE)
endif()

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${RAYLIB_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# ---------------------------------------------------------------------------
# FIX for:  stb_image_resize2.h: call to undeclared function 'vld1q_f16'
# On armeabi-v7a, stb_image_resize2 picks a NEON FP16 path that the 32-bit ARM
# toolchain cannot compile. KolomKhela never resizes textures (all graphics are
# procedural), so we simply switch the SIMD code path off for raylib.
# ---------------------------------------------------------------------------
if(ANDROID)
    target_compile_definitions(raylib PRIVATE STBIR_NO_SIMD)
endif()

# ---------------------------------------------------------------------------
# Game target
# ---------------------------------------------------------------------------
if(ANDROID)
    # Gradle expects libKolomKhela.so (android.app.lib_name = "KolomKhela")
    add_library(KolomKhela SHARED src/main.cpp)

    target_link_libraries(KolomKhela PRIVATE raylib log android EGL GLESv2 OpenSLES m dl)

    # keep raylib's ANativeActivity_onCreate (native_app_glue) in the .so
    # + 16 KB page alignment (needed for Android 15 devices on arm64)
    target_link_options(KolomKhela PRIVATE
        -u ANativeActivity_onCreate
        -Wl,-z,max-page-size=16384)
else()
    add_executable(KolomKhela src/main.cpp)
    target_link_libraries(KolomKhela PRIVATE raylib)

    if(MSVC)
        target_compile_options(KolomKhela PRIVATE /utf-8)
    endif()
endif()

target_include_directories(KolomKhela PRIVATE src)
