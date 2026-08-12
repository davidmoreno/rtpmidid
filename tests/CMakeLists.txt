add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp)
target_link_libraries(test_rtppeer rtpmidid-shared)
target_link_libraries(test_rtppeer ${FMT_LIBRARIES})
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_queue test_queue.cpp)
target_link_libraries(test_queue -pthread rtpmidid-shared)
target_link_libraries(test_queue ${FMT_LIBRARIES})
add_test(NAME test_queue COMMAND test_queue)

add_executable(test_actor test_actor.cpp)
target_link_libraries(test_actor -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_actor ${FMT_LIBRARIES})
add_test(NAME test_actor COMMAND test_actor)

add_executable(test_messages test_messages.cpp)
target_link_libraries(test_messages -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_messages ${FMT_LIBRARIES})
add_test(NAME test_messages COMMAND test_messages)

add_executable(test_router test_router.cpp)
target_link_libraries(test_router -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_router ${FMT_LIBRARIES})
add_test(NAME test_router COMMAND test_router)

add_executable(test_peer test_peer.cpp)
target_link_libraries(test_peer -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_peer ${FMT_LIBRARIES})
add_test(NAME test_peer COMMAND test_peer)

add_executable(test_network_actor test_network_actor.cpp)
target_link_libraries(test_network_actor -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_network_actor ${FMT_LIBRARIES})
add_test(NAME test_network_actor COMMAND test_network_actor)

add_executable(test_supervisor test_supervisor.cpp)
target_link_libraries(test_supervisor -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_supervisor ${FMT_LIBRARIES})
add_test(NAME test_supervisor COMMAND test_supervisor)

add_executable(test_alsa_bridge test_alsa_bridge.cpp)
target_link_libraries(test_alsa_bridge -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_alsa_bridge ${FMT_LIBRARIES})
target_link_libraries(test_alsa_bridge ${ALSA_LIBRARIES})
target_include_directories(test_alsa_bridge PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(test_alsa_bridge PUBLIC ${ALSA_CFLAGS_OTHER})
add_test(NAME test_alsa_bridge COMMAND test_alsa_bridge)

add_executable(test_control_socket test_control_socket.cpp)
target_link_libraries(test_control_socket -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_control_socket ${FMT_LIBRARIES})
add_test(NAME test_control_socket COMMAND test_control_socket)

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

if (NOT LIBS_ONLY)
    add_executable(test_jsondm
        test_jsondm.cpp test_utils.cpp
        ../src/control_status_jsondm.cpp
        ../src/peer_status_jsondm.cpp
        ../src/control_commands_jsondm.cpp
    )
    target_link_libraries(test_jsondm -pthread rtpmidid-shared)
    target_link_libraries(test_jsondm ${FMT_LIBRARIES})
    add_test(NAME test_jsondm COMMAND test_jsondm)
endif (NOT LIBS_ONLY)

if (NOT LIBS_ONLY)
    add_test(NAME test_json_dm_generator
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_json_dm_generator.py ${CMAKE_SOURCE_DIR})
endif (NOT LIBS_ONLY)
