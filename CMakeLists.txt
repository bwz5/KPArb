cmake_minimum_required(VERSION 3.24)

project(KPArb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KPARB_BUILD_TESTS "Build the test suite" ON)
option(KPARB_BUILD_BENCHMARKS "Build the benchmarks" ON)
option(KPARB_ENABLE_NATIVE_TUNING "Tune code generation for the host CPU" OFF)
set(KPARB_SANITIZER "" CACHE STRING "Sanitizer to enable: address, thread or empty")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Dependencies)

add_library(kparb_build_options INTERFACE)
kparb_apply_warnings(kparb_build_options)

if(KPARB_ENABLE_NATIVE_TUNING)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        target_compile_options(kparb_build_options INTERFACE -mcpu=native)
    else()
        target_compile_options(kparb_build_options INTERFACE -march=native)
    endif()
endif()

if(KPARB_SANITIZER STREQUAL "address")
    target_compile_options(kparb_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(kparb_build_options INTERFACE -fsanitize=address,undefined)
elseif(KPARB_SANITIZER STREQUAL "thread")
    target_compile_options(kparb_build_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(kparb_build_options INTERFACE -fsanitize=thread)
endif()

add_subdirectory(src)

if(KPARB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KPARB_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
