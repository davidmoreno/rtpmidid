add_executable(
  rtpmidid-daemon  
  mdns_rtpmidi.cpp aseq.cpp stringpp.cpp
  main.cpp config.cpp rtpmidid.cpp
  control_socket.cpp
)

target_link_libraries(rtpmidid-daemon rtpmidid-static -lasound -lfmt -lavahi-common -lavahi-client)

set_target_properties(rtpmidid-daemon PROPERTIES OUTPUT_NAME rtpmidid)