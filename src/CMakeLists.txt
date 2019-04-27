add_executable(
  rtpmidid
  main.cpp rtpmidid.cpp
  logger.cpp poller.cpp mdns.cpp mdns_service.cpp aseq.cpp netutils.cpp stringpp.cpp
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
)

target_link_libraries(rtpmidid -lasound -lfmt)
