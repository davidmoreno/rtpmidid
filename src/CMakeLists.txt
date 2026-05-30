include("${CMAKE_SOURCE_DIR}/cmake/DmJson.cmake")
dmjson_add_library(rtpmidid-dmjson)

# set files for the static library
add_library(
  rtpmidid2-static
  STATIC

  aseq.cpp
  argv.cpp
  ini.cpp
  ini_graph.cpp
  settings.cpp
  control_socket.cpp
  control_rpc.cpp
  web_server.cpp
  stringpp.cpp
  hwautoannounce.cpp

  factory.cpp
  midirouter.cpp
  midipeer.cpp
  peer_kind.cpp
  device_identity.cpp
  device_query.cpp
  peer_device.cpp
  peer_export.cpp
  peer_import.cpp
  peer_stable_id.cpp
  rtpmidiremotehandler.cpp
  utils.cpp
  midi_normalizer.cpp

  peer_import_alsa_rtp.cpp
  peer_export_alsa_network.cpp
  peer_device_alsa_seq.cpp
  peer_device_rawmidi.cpp
  peer_device_rtpmidi_client.cpp
  peer_export_rtpmidi_server.cpp
  peer_import_rtpmidi.cpp
  peer_device_rtpmidi_session.cpp
  webui_midi_monitor_peer.cpp
  connection_db.cpp
)
add_definitions(-D_REENTRANT)

add_executable(
  rtpmidid2-daemon  
  main.cpp
)

target_link_libraries(rtpmidid2-daemon rtpmidid2-static)
target_link_libraries(rtpmidid2-static PUBLIC rtpmidid-dmjson)

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

target_link_libraries(rtpmidid2-static PUBLIC ${SQLITE3_LIBRARIES})
target_include_directories(rtpmidid2-static PUBLIC ${SQLITE3_INCLUDE_DIRS})
target_compile_options(rtpmidid2-static PUBLIC ${SQLITE3_CFLAGS_OTHER})

# Add -rdynamic for stacktrace symbol resolution
set_target_properties(rtpmidid2-daemon PROPERTIES 
  OUTPUT_NAME rtpmidid
  LINK_FLAGS "-rdynamic"
)
