#
# rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
#
# Unlike the main daemon this only uses the shared library (plus ALSA and the
# mDNS client the shared library already needs).
#

add_executable(
  rtpmidid-lite
  main.cpp daemon.cpp alsa_seq.cpp config.cpp sysex.cpp
)

target_link_libraries(rtpmidid-lite rtpmidid-shared -pthread)
target_link_libraries(rtpmidid-lite ${ALSA_LIBRARIES} ${AVAHI_LIBRARIES} ${FMT_LIBRARIES})
target_include_directories(rtpmidid-lite PRIVATE ${ALSA_INCLUDE_DIRS} ${AVAHI_INCLUDE_DIRS} ${FMT_INCLUDE_DIRS})
target_compile_options(rtpmidid-lite PRIVATE ${ALSA_CFLAGS_OTHER} ${AVAHI_CFLAGS_OTHER} ${FMT_CFLAGS_OTHER})
