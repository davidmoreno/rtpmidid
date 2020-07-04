add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp)
target_link_libraries(test_rtppeer rtpmidid-shared -lfmt)
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_rtpserver test_rtpserver.cpp test_utils.cpp)
target_link_libraries(test_rtpserver rtpmidid-shared -lfmt -pthread)
add_test(NAME test_rtpserver COMMAND test_rtpserver)
