add_executable(
  rtpmidid2-daemon  
  main.cpp
  aseq.cpp
  argv.cpp
  ini.cpp
  control_socket.cpp
  stringpp.cpp

  factory.cpp
  midirouter.cpp
  midipeer.cpp
  rtpmidiremotehandler.cpp
  utils.cpp

  local_alsa_listener.cpp
  local_alsa_multi_listener.cpp
  local_alsa_peer.cpp
  network_rtpmidi_client.cpp
  network_rtpmidi_listener.cpp
  network_rtpmidi_multi_listener.cpp
  network_rtpmidi_peer.cpp
)

target_link_libraries(rtpmidid2-daemon ${AVAHI_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${AVAHI_CFLAGS_OTHER})

target_link_libraries(rtpmidid2-daemon ${FMT_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${FMT_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${FMT_CFLAGS_OTHER})

target_link_libraries(rtpmidid2-daemon ${ALSA_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${ALSA_CFLAGS_OTHER})

if(ENABLE_PCH)
  target_precompile_headers(rtpmidid2-daemon PUBLIC precompile.hpp)
endif()

target_link_libraries(rtpmidid2-daemon rtpmidid-static)

set_target_properties(rtpmidid2-daemon PROPERTIES OUTPUT_NAME rtpmidid)
