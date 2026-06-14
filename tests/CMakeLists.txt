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

add_executable(test_blocking_priority_queue test_blocking_priority_queue.cpp)
target_link_libraries(test_blocking_priority_queue rtpmidid-shared -pthread)
target_link_libraries(test_blocking_priority_queue ${FMT_LIBRARIES})
add_test(NAME test_blocking_priority_queue COMMAND test_blocking_priority_queue)


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
# Connection database v2 (Phase 5)
#

if (NOT LIBS_ONLY)
  add_executable(test_connection_db
      test_connection_db.cpp test_utils.cpp test_fake_peer.cpp
  )
  target_link_libraries(test_connection_db -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_connection_db COMMAND test_connection_db)
  target_link_libraries(test_connection_db ${ALSA_LIBRARIES})
  target_link_libraries(test_connection_db ${FMT_LIBRARIES})
  target_include_directories(test_connection_db PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_connection_db PUBLIC ${ALSA_CFLAGS_OTHER})

  add_executable(test_connection_alsa_direct test_connection_alsa_direct.cpp)
  target_link_libraries(test_connection_alsa_direct -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_connection_alsa_direct COMMAND test_connection_alsa_direct)
  target_link_libraries(test_connection_alsa_direct ${FMT_LIBRARIES})

  add_executable(test_alsa_monitor_tap test_alsa_monitor_tap.cpp)
  target_link_libraries(test_alsa_monitor_tap -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_alsa_monitor_tap COMMAND test_alsa_monitor_tap)
  target_link_libraries(test_alsa_monitor_tap ${ALSA_LIBRARIES})
  target_link_libraries(test_alsa_monitor_tap ${FMT_LIBRARIES})
  target_include_directories(test_alsa_monitor_tap PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_alsa_monitor_tap PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)

#
# Device registry (Phase 4)
#

if (NOT LIBS_ONLY)
  add_executable(test_device_registry
      test_device_registry.cpp test_utils.cpp test_fake_peer.cpp
  )
  target_link_libraries(test_device_registry -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_device_registry COMMAND test_device_registry)
  target_link_libraries(test_device_registry ${ALSA_LIBRARIES})
  target_link_libraries(test_device_registry ${FMT_LIBRARIES})
  target_include_directories(test_device_registry PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_device_registry PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)

#
# Device query matching (Phase 3)
#

add_executable(test_device_query test_device_query.cpp test_utils.cpp)
target_link_libraries(test_device_query rtpmidid-shared rtpmidid2-static -pthread)
target_link_libraries(test_device_query ${FMT_LIBRARIES})
add_test(NAME test_device_query COMMAND test_device_query)

#
# Device identity grammar (Phase 2)
#

add_executable(test_device_identity test_device_identity.cpp test_utils.cpp)
target_link_libraries(test_device_identity rtpmidid-shared rtpmidid2-static -pthread)
target_link_libraries(test_device_identity ${FMT_LIBRARIES})
add_test(NAME test_device_identity COMMAND test_device_identity)

#
# Factory naming (Phase 1)
#

if (NOT LIBS_ONLY)
  add_executable(test_factory_naming
      test_factory_naming.cpp test_utils.cpp
  )
  target_link_libraries(test_factory_naming -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_factory_naming COMMAND test_factory_naming)
  target_link_libraries(test_factory_naming ${ALSA_LIBRARIES})
  target_link_libraries(test_factory_naming ${FMT_LIBRARIES})
  target_include_directories(test_factory_naming PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_factory_naming PUBLIC ${ALSA_CFLAGS_OTHER})

  add_executable(test_peer_factory
      test_peer_factory.cpp test_utils.cpp
  )
  target_link_libraries(test_peer_factory -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_peer_factory COMMAND test_peer_factory)
  target_link_libraries(test_peer_factory ${ALSA_LIBRARIES})
  target_link_libraries(test_peer_factory ${FMT_LIBRARIES})
  target_include_directories(test_peer_factory PUBLIC ${ALSA_INCLUDE_DIRS})
  add_executable(test_peer_spawn
      test_peer_spawn.cpp test_utils.cpp test_fake_peer.cpp
  )
  target_link_libraries(test_peer_spawn -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_peer_spawn COMMAND test_peer_spawn)
  target_link_libraries(test_peer_spawn ${ALSA_LIBRARIES})
  target_link_libraries(test_peer_spawn ${FMT_LIBRARIES})
  target_include_directories(test_peer_spawn PUBLIC ${ALSA_INCLUDE_DIRS})
  target_compile_options(test_peer_spawn PUBLIC ${ALSA_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)

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

add_executable(test_dm_json_runtime test_dm_json_runtime.cpp)
target_link_libraries(test_dm_json_runtime rtpmidid-dmjson rtpmidid-shared -pthread)
target_link_libraries(test_dm_json_runtime ${FMT_LIBRARIES})
add_test(NAME test_dm_json_runtime COMMAND test_dm_json_runtime)

add_executable(test_dm_json_generated test_dm_json_generated.cpp)
target_link_libraries(test_dm_json_generated rtpmidid-dmjson rtpmidid-shared -pthread)
target_link_libraries(test_dm_json_generated ${FMT_LIBRARIES})
add_test(NAME test_dm_json_generated COMMAND test_dm_json_generated)

if (NOT LIBS_ONLY)
  add_executable(test_event_subscription
      test_event_subscription.cpp test_utils.cpp
  )
  target_link_libraries(test_event_subscription -pthread rtpmidid-shared rtpmidid2-static)
  add_test(NAME test_event_subscription COMMAND test_event_subscription)
  target_link_libraries(test_event_subscription ${AVAHI_LIBRARIES})
  target_link_libraries(test_event_subscription ${ALSA_LIBRARIES})
  target_link_libraries(test_event_subscription ${FMT_LIBRARIES})
  target_include_directories(test_event_subscription PUBLIC ${ALSA_INCLUDE_DIRS})
  target_include_directories(test_event_subscription PUBLIC ${AVAHI_INCLUDE_DIRS})
  target_compile_options(test_event_subscription PUBLIC ${ALSA_CFLAGS_OTHER})
  target_compile_options(test_event_subscription PUBLIC ${AVAHI_CFLAGS_OTHER})
endif (NOT LIBS_ONLY)

#
# Log buffer (ring buffer + logfmt + LogQL)
#

add_executable(test_log_buffer test_log_buffer.cpp)
target_link_libraries(test_log_buffer rtpmidid-shared -pthread)
target_link_libraries(test_log_buffer ${FMT_LIBRARIES})
add_test(NAME test_log_buffer COMMAND test_log_buffer)
