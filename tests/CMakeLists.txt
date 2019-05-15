add_executable(test_mdns test_mdns.cpp ../src/netutils.cpp)
target_link_libraries(test_mdns -lfmt)

add_test(NAME test_mdns COMMAND test_mdns)
