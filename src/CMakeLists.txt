add_executable(
  rtpmidid-daemon  
  aseq.cpp stringpp.cpp
  main.cpp config.cpp rtpmidid.cpp
  control_socket.cpp
)

include(FindPkgConfig)

pkg_check_modules(AVAHI REQUIRED avahi-client)
target_link_libraries(rtpmidid-daemon ${AVAHI_LIBRARIES})
target_include_directories(rtpmidid-daemon PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(rtpmidid-daemon PUBLIC ${AVAHI_CFLAGS_OTHER})

pkg_check_modules(FMT REQUIRED fmt)
target_link_libraries(rtpmidid-daemon ${FMT_LIBRARIES})
target_include_directories(rtpmidid-daemon PUBLIC ${FMT_INCLUDE_DIRS})
target_compile_options(rtpmidid-daemon PUBLIC ${FMT_CFLAGS_OTHER})

pkg_check_modules(ALSA REQUIRED alsa)
target_link_libraries(rtpmidid-daemon ${ALSA_LIBRARIES})
target_include_directories(rtpmidid-daemon PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(rtpmidid-daemon PUBLIC ${ALSA_CFLAGS_OTHER})

target_link_libraries(rtpmidid-daemon rtpmidid-static)

set_target_properties(rtpmidid-daemon PROPERTIES OUTPUT_NAME rtpmidid)
