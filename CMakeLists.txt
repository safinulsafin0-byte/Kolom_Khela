
cmake_minimum_required(VERSION 3.16)
project(KolomKhela LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------
# raylib 5.0 configuration
# ----------------------------------------------------------------

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

# Configure the Android backend.
if(ANDROID)
    set(PLATFORM "Android" CACHE STRING "" FORCE)
endif()

# Compatibility for raylib 5.0 with newer CMake versions.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib)

# ----------------------------------------------------------------
# Android NDK 27 compatibility patch
# raylib 5.0 uses the deprecated ALooper_pollAll() API.
# Replace it with ALooper_pollOnce() for Android builds.
# ----------------------------------------------------------------

if(ANDROID)
    set(RAYLIB_ANDROID_SOURCE
        "${raylib_SOURCE_DIR}/src/platforms/rcore_android.c"
    )

    if(NOT EXISTS "${RAYLIB_ANDROID_SOURCE}")
        message(FATAL_ERROR
            "Cannot find raylib Android source: ${RAYLIB_ANDROID_SOURCE}"
        )
    endif()

    file(READ
        "${RAYLIB_ANDROID_SOURCE}"
        RAYLIB_ANDROID_TEXT
    )

    string(FIND
        "${RAYLIB_ANDROID_TEXT}"
        "ALooper_pollAll("
        RAYLIB_POLLALL_POSITION
    )

    if(NOT RAYLIB_POLLALL_POSITION EQUAL -1)
        string(REPLACE
            "ALooper_pollAll("
            "ALooper_pollOnce("
            RAYLIB_ANDROID_TEXT
            "${RAYLIB_ANDROID_TEXT}"
        )

        file(WRITE
            "${RAYLIB_ANDROID_SOURCE}"
            "${RAYLIB_ANDROID_TEXT}"
        )

        message(STATUS
            "Applied raylib Android NDK compatibility patch"
        )
    else()
        message(STATUS
            "No ALooper_pollAll() calls found; patch not required"
        )
    endif()
endif()

# ----------------------------------------------------------------
# Game source files
# ----------------------------------------------------------------

set(KK_SOURCES
    src/main.cpp
    src/PenPhysics.hpp
    src/SoundSynth.hpp
)

# ----------------------------------------------------------------
# Android target
# ----------------------------------------------------------------

if(ANDROID)

    # Android loads the native library named libmain.so.
    add_library(${PROJECT_NAME} SHARED ${KK_SOURCES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME "main"
    )

    # Android Native App Glue.
    set(APP_GLUE_DIR
        "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue"
    )

    if(NOT EXISTS "${APP_GLUE_DIR}/android_native_app_glue.c")
        message(FATAL_ERROR
            "Android native_app_glue.c not found in ${APP_GLUE_DIR}"
        )
    endif()

    add_library(native_app_glue STATIC
        "${APP_GLUE_DIR}/android_native_app_glue.c"
    )

    target_include_directories(native_app_glue PUBLIC
        "${APP_GLUE_DIR}"
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        raylib
        native_app_glue
        android
        log
        EGL
        GLESv2
        OpenSLES
        m
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        -u ANativeActivity_onCreate
    )

# ----------------------------------------------------------------
# Windows / Desktop target
# ----------------------------------------------------------------

else()

    add_executable(${PROJECT_NAME} ${KK_SOURCES})

    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            /W3
            /utf-8
        )
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE
            -Wall
            -Wextra
        )
    endif()

    if(WIN32 AND NOT MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE -static)
    endif()

endif()

# ----------------------------------------------------------------
# Include game source directory
# ----------------------------------------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
