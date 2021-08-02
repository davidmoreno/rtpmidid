add_library(
  rtpmidid-shared
  SHARED
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  mdns_rtpmidi.cpp logger.cpp poller.cpp
  utils.cpp
)

add_library(
  rtpmidid-static
  STATIC
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  mdns_rtpmidi.cpp logger.cpp poller.cpp
  utils.cpp
)

include(FindPkgConfig)

pkg_check_modules(AVAHI REQUIRED avahi-client)

target_link_libraries(rtpmidid-static ${AVAHI_LIBRARIES})
target_include_directories(rtpmidid-static PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(rtpmidid-static PUBLIC ${AVAHI_CFLAGS_OTHER})

target_link_libraries(rtpmidid-shared ${AVAHI_LIBRARIES})
target_include_directories(rtpmidid-shared PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(rtpmidid-shared PUBLIC ${AVAHI_CFLAGS_OTHER})

set_target_properties(rtpmidid-static PROPERTIES OUTPUT_NAME rtpmididstatic)
set_target_properties(rtpmidid-shared PROPERTIES OUTPUT_NAME rtpmidid)

set_target_properties(rtpmidid-shared rtpmidid-static
	PROPERTIES
		SOVERSION 0
		VERSION 21.07
	)
