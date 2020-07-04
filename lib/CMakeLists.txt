add_library(
  rtpmidid-shared
  SHARED
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
)

add_library(
  rtpmidid-static
  STATIC
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
)

set_target_properties(rtpmidid-static PROPERTIES OUTPUT_NAME rtpmidid)
set_target_properties(rtpmidid-shared PROPERTIES OUTPUT_NAME rtpmidid)
