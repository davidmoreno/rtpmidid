add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp)
target_link_libraries(test_rtppeer rtpmidid-shared -lfmt)
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_rtpserver test_rtpserver.cpp test_utils.cpp)
target_link_libraries(test_rtpserver rtpmidid-shared -lfmt -pthread)
add_test(NAME test_rtpserver COMMAND test_rtpserver)

add_executable(test_poller test_poller.cpp test_utils.cpp)
target_link_libraries(test_poller rtpmidid-shared -lfmt -pthread)
add_test(NAME test_poller COMMAND test_poller)

add_executable(test_rtpmidid 
    test_rtpmidid.cpp test_utils.cpp 
    ../src/aseq.cpp  ../src/config.cpp ../src/control_socket.cpp ../src/rtpmidid.cpp ../src/stringpp.cpp
)
target_link_libraries(test_rtpmidid rtpmidid-shared -lfmt -pthread)

target_link_libraries(test_rtpmidid ${AVAHI_LIBRARIES})
target_include_directories(test_rtpmidid PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(test_rtpmidid PUBLIC ${AVAHI_CFLAGS_OTHER})

target_link_libraries(test_rtpmidid ${FMT_LIBRARIES})
target_include_directories(test_rtpmidid PUBLIC ${FMT_INCLUDE_DIRS})
target_compile_options(test_rtpmidid PUBLIC ${FMT_CFLAGS_OTHER})

target_link_libraries(test_rtpmidid ${ALSA_LIBRARIES})
target_include_directories(test_rtpmidid PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(test_rtpmidid PUBLIC ${ALSA_CFLAGS_OTHER})

add_test(NAME test_rtpmidid COMMAND test_rtpmidid)
