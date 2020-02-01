add_library(
  rtpmididlib
  STATIC
  logger.cpp poller.cpp mdns_rtpmidi.cpp aseq.cpp netutils.cpp stringpp.cpp
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  control_socket.cpp
)

add_executable(
  rtpmidid
  main.cpp config.cpp rtpmidid.cpp
)

target_link_libraries(rtpmidid rtpmididlib -lasound -lfmt -lavahi-common -lavahi-client)
