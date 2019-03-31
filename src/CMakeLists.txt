add_executable(
  rtpmidid
  main.cpp rtpmidid.cpp
  logger.cpp poller.cpp mdns.cpp aseq.cpp netutils.cpp
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
)

target_link_libraries(rtpmidid ${CONAN_LIBS} -lasound)
