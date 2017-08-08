cmake_minimum_required(VERSION 3.5)
project(libqfakeclient)

option(BUILD_SHARED_LIB OFF)
option(BUILD_TEST_APP OFF)

set(CMAKE_CXX_STANDARD 11)

find_package(ZLIB REQUIRED)

if (NOT ZLIB_FOUND)
    message(FATAL_ERROR "Cannot find zlib")
endif()

include_directories("./include")
include_directories(${ZLIB_INCLUDE_DIRS})

set(SOURCE_FILES
    include/channel.h
    include/client.h
    include/common.h
    include/command_buffer.h
    include/command_parser.h
    include/console.h
    include/message_parser.h
    include/network_address.h
    include/protocol_executor.h
    include/server_list.h
    include/socket.h
    include/system.h
    src/channel.cpp
    src/client.cpp
    src/command_buffer.cpp
    src/command_parser.cpp
    src/console.cpp
    src/message_parser.cpp
    src/network_address.cpp
    src/protocol_executor.cpp
    src/server_list.cpp
    src/socket.cpp
    src/system.cpp)

if (BUILD_SHARED_LIB)
    add_library(qfakeclient SHARED ${SOURCE_FILES})
else()
    add_library(qfakeclient ${SOURCE_FILES})
endif()

target_link_libraries(qfakeclient ${ZLIB_LIBRARIES})

if (BUILD_TEST_APP)
    add_custom_target(qfakeclient_executable)
    add_executable(testqfakeclient main.cpp)
    target_link_libraries(testqfakeclient qfakeclient)
    add_dependencies(testqfakeclient qfakeclient)
    add_dependencies(qfakeclient_executable testqfakeclient)
endif()
