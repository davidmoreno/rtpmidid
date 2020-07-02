add_library(
  rtpmidid
  SHARED
  netutils.cpp rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
)

add_library(
  rtpmidid-static
  STATIC
  netutils.cpp rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
)

set_target_properties(rtpmidid-static PROPERTIES OUTPUT_NAME rtpmidid)
