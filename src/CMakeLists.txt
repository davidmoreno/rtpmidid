add_executable(
  rtpmidid
  poller.cpp mdns_rtpmidi.cpp aseq.cpp stringpp.cpp
  main.cpp config.cpp rtpmidid.cpp
  control_socket.cpp
)

target_link_libraries(rtpmidid rtpmididlib -lasound -lfmt -lavahi-common -lavahi-client)
