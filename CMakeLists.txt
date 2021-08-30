project(rtpmidid)
cmake_minimum_required(VERSION 3.2)
add_definitions("-std=gnu++17 -Wall -Werror")
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fstack-protector-strong")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer")


include(FindPkgConfig)
pkg_check_modules(AVAHI REQUIRED avahi-client)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(ALSA REQUIRED alsa)


include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
enable_testing()

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(tests)
