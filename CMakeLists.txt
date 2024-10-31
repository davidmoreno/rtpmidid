cmake_minimum_required(VERSION 3.20)
project(rtpmidid)

# Add option to enable/disable tests
option(ENABLE_TESTS "Enable tests" ON)
# option to enable precompiled headers
option(ENABLE_PCH "Enable precompiled headers" ON)
# option to easy use clang compiler
option(USE_CLANG "Use clang compiler" OFF)
# option force the linker, default mold, if not found lld, if not found default
option(LLD "Use lld linker (mold | lld | ld | auto)" "auto")

if (USE_CLANG)
  set (CMAKE_CXX_COMPILER "clang++")
  set (CMAKE_C_COMPILER "clang")
  message(STATUS "Using clang compiler")
endif()

add_definitions("-std=c++20 -Wall -Werror")

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
message(STATUS "Version ${RTPMIDID_VERSION}")

add_subdirectory(lib)
if(NOT LIBS_ONLY)
  add_subdirectory(src)
endif(NOT LIBS_ONLY)

if (ENABLE_TESTS STREQUAL "ON")
  add_subdirectory(tests)
endif()
