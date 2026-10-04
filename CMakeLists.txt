cmake_minimum_required(VERSION 3.15)
project(TCPChatApplication CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-specific socket and threading libraries
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Shared common protocol and data structures
include_directories(common/include)

# Server executable target
add_executable(chat_server
    server/src/main.cpp
    server/src/server.cpp
    server/src/client_manager.cpp
    server/src/logger.cpp
)
target_include_directories(chat_server PRIVATE server/include common/include)
if(PLATFORM_LIBS)
    target_link_libraries(chat_server PRIVATE ${PLATFORM_LIBS})
endif()

# Client executable target
add_executable(chat_client
    client/src/main.cpp
    client/src/client.cpp
    client/src/sender.cpp
    client/src/receiver.cpp
)
target_include_directories(chat_client PRIVATE client/include common/include)
if(PLATFORM_LIBS)
    target_link_libraries(chat_client PRIVATE ${PLATFORM_LIBS})
endif()
