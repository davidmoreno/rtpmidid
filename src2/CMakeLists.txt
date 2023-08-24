add_executable(
  rtpmidid2-daemon  
  main.cpp
  midirouter.cpp
  factory.cpp
  alsanetwork.cpp
  aseq.cpp
  alsapeer.cpp
  rtpmidiserverpeer.cpp
  rtpmidinetwork.cpp
)

target_link_libraries(rtpmidid2-daemon ${AVAHI_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${AVAHI_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${AVAHI_CFLAGS_OTHER})

target_link_libraries(rtpmidid2-daemon ${FMT_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${FMT_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${FMT_CFLAGS_OTHER})

target_link_libraries(rtpmidid2-daemon ${ALSA_LIBRARIES})
target_include_directories(rtpmidid2-daemon PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(rtpmidid2-daemon PUBLIC ${ALSA_CFLAGS_OTHER})

target_link_libraries(rtpmidid2-daemon rtpmidid-static)

set_target_properties(rtpmidid2-daemon PROPERTIES OUTPUT_NAME rtpmidid)
