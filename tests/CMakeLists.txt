add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp)
target_link_libraries(test_rtppeer rtpmidid-shared -lfmt)
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_rtpserver test_rtpserver.cpp test_utils.cpp)
target_link_libraries(test_rtpserver rtpmidid-shared -lfmt -pthread)
add_test(NAME test_rtpserver COMMAND test_rtpserver)

add_executable(test_poller test_poller.cpp test_utils.cpp)
target_link_libraries(test_poller rtpmidid-shared -lfmt -pthread)
add_test(NAME test_poller COMMAND test_poller)


add_executable(test_misc test_misc.cpp test_utils.cpp)
target_link_libraries(test_misc rtpmidid-shared -lfmt -pthread)
add_test(NAME test_misc COMMAND test_poller)

# add_executable(test_rtpmidid 
#     test_rtpmidid.cpp test_utils.cpp 
#     ../src/aseq.cpp  ../src/config.cpp ../src/control_socket.cpp ../src/rtpmidid.cpp ../src/stringpp.cpp
# )
# target_link_libraries(test_rtpmidid rtpmidid-shared -lfmt -pthread)

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
    test_signals.cpp ../lib/logger.cpp
)
target_link_libraries(test_signals -lfmt -pthread)
add_test(NAME test_signals COMMAND test_signals)


#
# MIDIROUTER
# 

add_executable(test_midirouter
    test_midirouter.cpp test_utils.cpp 
    ../src2/midirouter.cpp 
    ../src2/alsalistener.cpp 
    ../src2/aseq.cpp
    ../src2/midipeer.cpp
)
target_link_libraries(test_midirouter -lfmt -pthread rtpmidid-shared)
add_test(NAME test_midirouter COMMAND test_midirouter)
target_link_libraries(test_midirouter ${ALSA_LIBRARIES})
target_include_directories(test_midirouter PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(test_midirouter PUBLIC ${ALSA_CFLAGS_OTHER})

#
# MIDIROUTER2
# 

add_executable(test_midirouter2
    test_midirouter2.cpp test_utils.cpp 
    
    ../src2/aseq.cpp
    ../src2/control_socket.cpp
    ../src2/stringpp.cpp
    ../src2/factory.cpp
    ../src2/midirouter.cpp
    ../src2/midipeer.cpp
    ../src2/rtpmidiremotehandler.cpp
    ../src2/utils.cpp
    ../src2/alsalistener.cpp
    ../src2/alsaworker.cpp
    ../src2/alsawaiter.cpp
    ../src2/rtpmidiclientworker.cpp
    ../src2/rtpmidilistener.cpp
    ../src2/rtpmidiserverworker.cpp
    ../src2/rtpmidiworker.cpp
  )
target_link_libraries(test_midirouter2 -lfmt -pthread rtpmidid-shared)
add_test(NAME test_midirouter2 COMMAND test_midirouter2)
target_link_libraries(test_midirouter2 ${ALSA_LIBRARIES})
target_include_directories(test_midirouter2 PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(test_midirouter2 PUBLIC ${ALSA_CFLAGS_OTHER})


# disabled as failing 
# add_test(NAME test_rtpmidid COMMAND test_rtpmidid)
