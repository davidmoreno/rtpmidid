set(SOURCES 
  rtppeer.cpp rtpclient.cpp rtpserver.cpp
  logger.cpp poller.cpp
  utils.cpp
)

if (MDNS STREQUAL "avahi")
  list(APPEND SOURCES
    mdns/mdns_avahi.cpp
  )

elseif (MDNS STREQUAL "custom")
  list(APPEND SOURCES
    mdns/mdns_service.cpp
    mdns/mdns.cpp
    mdns/netutils.cpp
    mdns/mdns_custom.cpp
  )
  add_definitions(-DMDNS_CUSTOM=1)
elseif (MDNS STREQUAL "empty")
  list(APPEND SOURCES
    mdns/mdns_empty.cpp
  )

endif (MDNS STREQUAL "avahi")

add_library(
  rtpmidid-shared
  SHARED
  ${SOURCES}
)

add_library(
  rtpmidid-static
  STATIC
  ${SOURCES}
)


if (MDNS STREQUAL "avahi")
  include(FindPkgConfig)
  pkg_check_modules(AVAHI REQUIRED avahi-client)
  target_link_libraries(rtpmidid-static ${AVAHI_LIBRARIES})
  target_include_directories(rtpmidid-static PUBLIC ${AVAHI_INCLUDE_DIRS})
  target_compile_options(rtpmidid-static PUBLIC ${AVAHI_CFLAGS_OTHER})

  target_link_libraries(rtpmidid-shared ${AVAHI_LIBRARIES})
  target_include_directories(rtpmidid-shared PUBLIC ${AVAHI_INCLUDE_DIRS})
  target_compile_options(rtpmidid-shared PUBLIC ${AVAHI_CFLAGS_OTHER})
endif (MDNS STREQUAL "avahi")


set_target_properties(rtpmidid-static PROPERTIES OUTPUT_NAME rtpmididstatic)
set_target_properties(rtpmidid-shared PROPERTIES OUTPUT_NAME rtpmidid)

set_target_properties(rtpmidid-shared rtpmidid-static
	PROPERTIES
		SOVERSION 0
		VERSION 21.07
	)
