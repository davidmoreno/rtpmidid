# set files for the static library
add_library(
  rtpmidid2-static
  STATIC

  aseq.cpp
  argv.cpp
  ini.cpp
  settings_jsondm.cpp
  settings.cpp
  control_commands_jsondm.cpp
  control_status_jsondm.cpp
  peer_status_jsondm.cpp
  stringpp.cpp

  utils.cpp
  midi_normalizer.cpp
  router_actor.cpp
  peer_actor.cpp
  local_rawmidi_peer_actor.cpp
  worker_actor.cpp
  network_rtpmidi_peer_actor.cpp
  rtpmidi_server_actor.cpp
  mdns_actor.cpp
  logger_actor.cpp
  supervisor_actor.cpp
  alsa_actor.cpp
  control_socket_actor.cpp

)
add_definitions(-D_REENTRANT)

add_executable(
  rtpmidid2-daemon  
  main.cpp
)

target_link_libraries(rtpmidid2-daemon rtpmidid2-static)

target_link_libraries(rtpmidid2-daemon ${FMT_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${FMT_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${FMT_CFLAGS_OTHER})


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
  target_precompile_headers(rtpmidid2-static PRIVATE precompile.hpp)
  target_precompile_headers(rtpmidid2-daemon REUSE_FROM rtpmidid2-static)
endif()

target_link_libraries(rtpmidid2-daemon rtpmidid-static)

set_target_properties(rtpmidid2-daemon PROPERTIES OUTPUT_NAME rtpmidid)
