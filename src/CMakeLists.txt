add_executable(
  rtpmidid-daemon  
  aseq.cpp stringpp.cpp
  main.cpp config.cpp rtpmidid.cpp
  control_socket.cpp
)

target_link_libraries(rtpmidid-daemon rtpmidid-static -lasound -lfmt)

set_target_properties(rtpmidid-daemon PROPERTIES OUTPUT_NAME rtpmidid)
