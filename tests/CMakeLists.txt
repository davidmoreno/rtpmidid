add_executable(test_mdns test_mdns.cpp test_utils.cpp ../src/poller.cpp)
target_link_libraries(test_mdns rtpmididlib -lfmt)
add_test(NAME test_mdns COMMAND test_mdns)

add_executable(test_rtppeer test_rtppeer.cpp test_utils.cpp ../src/poller.cpp)
target_link_libraries(test_rtppeer rtpmididlib -lfmt)
add_test(NAME test_rtppeer COMMAND test_rtppeer)

add_executable(test_rtpserver test_rtpserver.cpp test_utils.cpp ../src/poller.cpp)
target_link_libraries(test_rtpserver rtpmididlib -lfmt -pthread)
add_test(NAME test_rtpserver COMMAND test_rtpserver)
