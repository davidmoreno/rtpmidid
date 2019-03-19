add_executable(rtpmidid main.cpp logger.cpp aseq.cpp)
target_link_libraries(rtpmidid ${CONAN_LIBS} -lasound)
