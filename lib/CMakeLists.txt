add_library(
  rtpmidid
  STATIC
  netutils.cpp rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
)
