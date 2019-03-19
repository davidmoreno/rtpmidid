add_executable(rtpmidid main.cpp logger.cpp poller.cpp aseq.cpp rtpport.cpp)
target_link_libraries(rtpmidid ${CONAN_LIBS} -lasound)
