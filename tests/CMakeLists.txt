add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp)
target_link_libraries(test_rtppeer rtpmidid-shared)
target_link_libraries(test_rtppeer ${FMT_LIBRARIES})
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_rtpserver test_rtpserver.cpp test_utils.cpp)
target_link_libraries(test_rtpserver rtpmidid-shared -pthread)
target_link_libraries(test_rtpserver ${FMT_LIBRARIES})
add_test(NAME test_rtpserver COMMAND test_rtpserver)

add_executable(test_poller test_poller.cpp test_utils.cpp)
target_link_libraries(test_poller rtpmidid-shared -pthread)
target_link_libraries(test_poller ${FMT_LIBRARIES})
add_test(NAME test_poller COMMAND test_poller)


add_executable(test_misc test_misc.cpp test_utils.cpp)
target_link_libraries(test_misc rtpmidid-shared -pthread)
target_link_libraries(test_misc ${FMT_LIBRARIES})
add_test(NAME test_misc COMMAND test_poller)

# add_executable(test_rtpmidid
#     test_rtpmidid.cpp test_utils.cpp
#     ../src/aseq.cpp  ../src/config.cpp ../src/control_socket.cpp ../src/rtpmidid.cpp ../src/stringpp.cpp
# )
# target_link_libraries(test_rtpmidid rtpmidid-shared -pthread)

# target_link_libraries(test_rtpmidid ${AVAHI_LIBRARIES})
# target_include_directories(test_rtpmidid PUBLIC ${AVAHI_INCLUDE_DIRS})
# target_compile_options(test_rtpmidid PUBLIC ${AVAHI_CFLAGS_OTHER})

# target_link_libraries(test_rtpmidid ${FMT_LIBRARIES})
# target_include_directories(test_rtpmidid PUBLIC ${FMT_INCLUDE_DIRS})
# target_compile_options(test_rtpmidid PUBLIC ${FMT_CFLAGS_OTHER})

# target_link_libraries(test_rtpmidid ${ALSA_LIBRARIES})
# target_include_directories(test_rtpmidid PUBLIC ${ALSA_INCLUDE_DIRS})
# target_compile_options(test_rtpmidid PUBLIC ${ALSA_CFLAGS_OTHER})

add_executable(test_signals
    test_signals.cpp
)
target_link_libraries(test_signals -pthread rtpmidid-shared)
target_link_libraries(test_signals ${FMT_LIBRARIES})
add_test(NAME test_signals COMMAND test_signals)


#
# MIDIROUTER
#

if (NOT LIBS_ONLY)
  add_executable(test_midirouter
      test_midirouter.cpp test_utils.cpp
  )
  target_link_libraries(test_midirouter -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_midirouter COMMAND test_midirouter)
  target_link_libraries(test_midirouter ${ALSA_LIBRARIES})
  target_link_libraries(test_midirouter ${FMT_LIBRARIES})
  target_include_directories(test_midirouter PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_midirouter PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)

#
# MIDIROUTER2
#

if (NOT LIBS_ONLY)
  add_executable(test_midirouter2
      test_midirouter2.cpp test_utils.cpp
  )
  target_link_libraries(test_midirouter2 -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_midirouter2 COMMAND test_midirouter2)
  target_link_libraries(test_midirouter2 ${ALSA_LIBRARIES})
  target_link_libraries(test_midirouter2 ${FMT_LIBRARIES})
  target_include_directories(test_midirouter2 PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_midirouter2 PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)


# disabled as failing
# add_test(NAME test_rtpmidid COMMAND test_rtpmidid)

#
# RTPCLIENT
#

if (NOT LIBS_ONLY)
    add_executable(test_rtpclient
        test_rtpclient.cpp test_utils.cpp
    )
    target_link_libraries(test_rtpclient -pthread rtpmidid-shared)
    add_test(NAME test_rtpclient COMMAND test_rtpclient)
    target_link_libraries(test_rtpclient ${ALSA_LIBRARIES})
    target_link_libraries(test_rtpclient ${FMT_LIBRARIES})
    target_include_directories(test_rtpclient PUBLIC ${ALSA_INCLUDE_DIRS})
    target_compile_options(test_rtpclient PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)


if (NOT LIBS_ONLY)
    add_executable(test_midinormalizer
        test_midinormalizer.cpp test_utils.cpp
    )
    target_link_libraries(test_midinormalizer -pthread rtpmidid-shared rtpmidid2-static)
    add_test(NAME test_midinormalizer COMMAND test_midinormalizer)
    target_link_libraries(test_midinormalizer ${ALSA_LIBRARIES})
    target_link_libraries(test_midinormalizer ${FMT_LIBRARIES})
    target_include_directories(test_midinormalizer PUBLIC ${ALSA_INCLUDE_DIRS})
    target_compile_options(test_midinormalizer PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)


if (NOT LIBS_ONLY)
    add_executable(test_settings
        test_settings.cpp test_utils.cpp
    )
    target_link_libraries(test_settings -pthread rtpmidid-shared rtpmidid2-static)
    add_test(NAME test_settings COMMAND test_settings)
    target_link_libraries(test_settings ${ALSA_LIBRARIES})
    target_link_libraries(test_settings ${FMT_LIBRARIES})
    target_include_directories(test_settings PUBLIC ${ALSA_INCLUDE_DIRS})
    target_compile_options(test_settings PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)
