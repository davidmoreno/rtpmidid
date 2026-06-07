cmake_minimum_required(VERSION 3.20)
project(rtpmidid)

# Add option to enable/disable tests
option(ENABLE_TESTS "Enable tests" ON)
# option to enable precompiled headers
option(ENABLE_PCH "Enable precompiled headers" ON)
# option to easy use clang compiler
option(USE_CLANG "Use clang compiler" OFF)
# option force the linker, default mold, if not found lld, if not found default
set(LDD "auto" CACHE STRING "Use ldd linker (mold | lld | auto)")
# C++ standard version. Defaults to 23 (minimum supported by Debian Bookworm).
set(CPP_VERSION "23" CACHE STRING "C++ standard version (20, 23)")
# option to force use fmt
set(USE_FMT "auto" CACHE STRING "Use fmt library (ON | OFF | auto)")
# option to set log level: 1=debug, 2=info, 3=warning, 4=error
# Set from CLI with: cmake -DLOG_LEVEL=2 ..
set(LOG_LEVEL "1" CACHE STRING "Set log level (1=debug, 2=info, 3=warning, 4=error)")
option(RTPMIDID_ENABLE_TIMING "Enable steady_clock timing on MIDI hot path" OFF)

# Determine whether to use <format> or libfmt
if (USE_FMT STREQUAL "auto")
  include(CheckIncludeFileCXX)
  CHECK_INCLUDE_FILE_CXX("format" HAVE_FORMAT)
  if (HAVE_FORMAT)
    set(USE_FMT "OFF")
  else()
    set(USE_FMT "ON")
  endif()
endif()

if (USE_FMT STREQUAL "ON")
  message(STATUS "Using fmt library")
else()
  message(STATUS "Not using fmt library")
endif()

if (USE_CLANG)
  set (CMAKE_CXX_COMPILER "clang++")
  set (CMAKE_C_COMPILER "clang")
  message(STATUS "Using clang compiler")
endif()

add_definitions("-std=c++${CPP_VERSION} -Wall -Werror")

# fmt 9.x (Debian Bookworm < v10) has broken consteval format string
# checking via FMT_CONSTEVAL=consteval on GCC 12. Disable it.
# FMT_CONSTEVAL must be defined to a truly empty value (no quotes).
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_CONSTEVAL=")
# GCC 12 false-positive maybe-uninitialized with std::variant
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  add_compile_options(-Wno-error=maybe-uninitialized)
endif()

set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer")
message(STATUS "Build type ${CMAKE_BUILD_TYPE}")

# if debug build add -O0
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions ("-O0 -g")
else()
  add_definitions ("-O2")
endif()

# if mold installed, use it
if (LDD STREQUAL "auto")
  find_program(MOLD mold)
  if (MOLD)
    message(STATUS "Using mold linker")
    add_link_options("-fuse-ld=lld")
  else()
    find_program(LLD lld)
    if (LLD)
      message(STATUS "Using lld linker")
      add_link_options("-fuse-ld=lld")
    else()
      message(STATUS "Using default linker. Other options are mold and lld.")
    endif()
  endif()
elseif (LLD STREQUAL "mold")
  find_program(MOLD mold)
  if (MOLD)
    message(STATUS "Using mold linker")
    add_link_options("-fuse-ld=lld")
  else()
    message(FATAL_ERROR "mold linker not found")
  endif()
elseif (LLD STREQUAL "lld")
  find_program(LLD lld)
  if (LLD)
    message(STATUS "Using lld linker")
    add_link_options("-fuse-ld=lld")
  else()
    message(FATAL_ERROR "lld linker not found")
  endif()
endif()


if (ENABLE_PCH)
  # show a log message
  message(STATUS "Precompiled headers enabled")
else(ENABLE_PCH)
  # show a log message
  message(STATUS "Precompiled headers disabled")
endif(ENABLE_PCH)

include(FindPkgConfig)
pkg_check_modules(AVAHI REQUIRED avahi-client)
if (USE_FMT STREQUAL "ON")
  pkg_check_modules(FMT REQUIRED fmt)
  add_definitions(-DUSE_LIBFMT)
else()
  # create compatible definitions that are empty  
  set(FMT_LIBRARIES "")
  set(FMT_INCLUDE_DIRS "")
  set(FMT_CFLAGS_OTHER "")
endif()
if(NOT LIBS_ONLY)
  pkg_check_modules(ALSA REQUIRED alsa)
  pkg_check_modules(SQLITE3 REQUIRED sqlite3)
endif(NOT LIBS_ONLY)


include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
enable_testing()

execute_process(
  COMMAND bash -c "git describe --match \"v[0-9]*\" --tags --abbrev=5 HEAD | sed 's/^v//g' | sed 's/-/~/g' | tr -d '\n'"
  OUTPUT_VARIABLE RTPMIDID_VERSION
)
add_definitions(-DRTPMIDID_VERSION=\"${RTPMIDID_VERSION}\")
add_definitions(-DLOG_LEVEL=${LOG_LEVEL})
if (RTPMIDID_ENABLE_TIMING)
  add_compile_definitions(RTPMIDID_ENABLE_TIMING=1)
  message(STATUS "RTPMIDID_ENABLE_TIMING: per-packet steady_clock in peer path")
endif()
message(STATUS "Version ${RTPMIDID_VERSION}")
message(STATUS "Log level ${LOG_LEVEL} (1=debug, 2=info, 3=warning, 4=error)")

add_subdirectory(lib)
if(NOT LIBS_ONLY)
  add_subdirectory(src)
endif(NOT LIBS_ONLY)

if (ENABLE_TESTS STREQUAL "ON")
  add_subdirectory(tests)
endif()
