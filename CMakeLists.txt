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
# Required C++ standard: C++23 (std::expected, std::move_only_function, std::format).
# Coroutines are deliberately NOT used (see design D15): hidden resumption points and
# frame allocation add unpredictable latency to the control path.
set(CPP_VERSION "23" CACHE STRING "Required C++ standard: 23")
# option to force use fmt, depends on CPP20
set(USE_FMT "auto" CACHE STRING "Use fmt library (ON | OFF | auto)")
# option to set log level: 1=debug, 2=info, 3=warning, 4=error
# Set from CLI with: cmake -DLOG_LEVEL=2 ..
set(LOG_LEVEL "1" CACHE STRING "Set log level (1=debug, 2=info, 3=warning, 4=error)")
# option to use ccache to cache compilations and avoid recompiling unchanged files.
# Note: ccache caching requires ENABLE_PCH=OFF (ccache cannot cache PCH compilations).
set(USE_CCACHE "auto" CACHE STRING "Use ccache compiler cache (ON | OFF | auto)")

if (NOT CPP_VERSION STREQUAL "23")
  message(FATAL_ERROR "C++23 is required (CPP_VERSION=${CPP_VERSION}); configure with -DCPP_VERSION=23")
endif()

# C++23 has std::format; only use fmt if explicitly forced
if (USE_FMT STREQUAL "auto")
  set(USE_FMT "OFF")
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

# if ccache is installed, use it to cache compilations and avoid recompiling
if (NOT USE_CCACHE STREQUAL "OFF")
  find_program(CCACHE_PROGRAM ccache)
  if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    if (ENABLE_PCH)
      message(WARNING "ccache cannot cache PCH compilations; set -DENABLE_PCH=OFF to get cache hits")
    endif()
  elseif (USE_CCACHE STREQUAL "ON")
    message(FATAL_ERROR "ccache requested (USE_CCACHE=ON) but ccache executable not found")
  else()
    message(STATUS "ccache not found, compilation caching disabled (install ccache or set -DUSE_CCACHE=ON)")
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
endif(NOT LIBS_ONLY)


include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
enable_testing()

execute_process(
  COMMAND bash -c "git describe --match \"v[0-9]*\" --tags --abbrev=5 HEAD | sed 's/^v//g' | sed 's/-/~/g' | tr -d '\n'"
  OUTPUT_VARIABLE RTPMIDID_VERSION
)
add_definitions(-DRTPMIDID_VERSION=\"${RTPMIDID_VERSION}\")
add_definitions(-DLOG_LEVEL=${LOG_LEVEL})
message(STATUS "Version ${RTPMIDID_VERSION}")
message(STATUS "Log level ${LOG_LEVEL} (1=debug, 2=info, 3=warning, 4=error)")

add_subdirectory(lib)
if(NOT LIBS_ONLY)
  add_subdirectory(src)
endif(NOT LIBS_ONLY)

if (ENABLE_TESTS STREQUAL "ON")
  add_subdirectory(tests)
endif()
