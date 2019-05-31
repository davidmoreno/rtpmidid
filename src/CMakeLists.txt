add_library(
  rtpmididlib
  STATIC
  logger.cpp poller.cpp mdns.cpp mdns_service.cpp aseq.cpp netutils.cpp stringpp.cpp
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
)

add_executable(
  rtpmidid
  main.cpp config.cpp rtpmidid.cpp
)

target_link_libraries(rtpmidid -lasound -lfmt rtpmididlib)
