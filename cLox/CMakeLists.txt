cmake_minimum_required(VERSION 3.16)
project(clox LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to an optimised build when no build type is given.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Every .c file in this folder. CONFIGURE_DEPENDS picks up added or removed files on the next build.
file(GLOB CLOX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(clox ${CLOX_SOURCES})

if(MSVC)
  target_compile_options(clox PRIVATE /W4)
else()
  target_compile_options(clox PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# fmod and pow live in libm on Linux. Harmless on macOS.
if(UNIX)
  target_link_libraries(clox PRIVATE m)
endif()

# Debug switches, e.g. cmake -B build -DCLOX_STRESS_GC=ON
# Keep the matching #defines commented out in common.h when using these.
option(CLOX_STRESS_GC  "Collect garbage on every allocation"   OFF)
option(CLOX_LOG_GC     "Log garbage collector activity"        OFF)
option(CLOX_TRACE      "Print the stack and each instruction"  OFF)
option(CLOX_PRINT_CODE "Disassemble each compiled function"    OFF)
option(CLOX_SANITIZE   "Build with AddressSanitizer and UBSan" OFF)

if(CLOX_STRESS_GC)
  target_compile_definitions(clox PRIVATE DEBUG_STRESS_GC)
endif()
if(CLOX_LOG_GC)
  target_compile_definitions(clox PRIVATE DEBUG_LOG_GC)
endif()
if(CLOX_TRACE)
  target_compile_definitions(clox PRIVATE DEBUG_TRACE_EXECUTION)
endif()
if(CLOX_PRINT_CODE)
  target_compile_definitions(clox PRIVATE DEBUG_PRINT_CODE)
endif()

if(CLOX_SANITIZE AND NOT MSVC)
  target_compile_options(clox PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(clox PRIVATE -fsanitize=address,undefined)
endif()
