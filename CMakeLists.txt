cmake_minimum_required(VERSION 3.2)
set(PROJECT gatecont)
project(${PROJECT})
set(INCLUDE_DIRECTORIES src)
set(HEADER_FILES src/clock.hpp src/constants.hpp src/endians.hpp src/engine.hpp src/event.hpp src/logger.hpp src/semaphore.hpp src/socket.hpp src/tcplistener.hpp src/tcpstream.hpp src/types.hpp src/udpsocket.hpp src/utils.hpp)
set(SOURCE_FILES src/clock.cpp src/engine.cpp src/event.cpp src/logger.cpp src/main.cpp src/semaphore.cpp src/socket.cpp src/tcplistener.cpp src/tcpstream.cpp src/udpsocket.cpp src/utils.cpp)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++14  -g3 -O0  -ggdb ")
set(CMAKE_EXE_LINKER_FLAGS "${BOTAN_LIB} ${TCM_LIB}")
add_executable(${PROJECT} ${SOURCE_FILES} ${HEADER_FILES})
install(TARGETS ${PROJECT})

