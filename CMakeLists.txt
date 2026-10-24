cmake_minimum_required(VERSION 3.15)
project(TCPChatApplication CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warning flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Threading library
find_package(Threads REQUIRED)

# SQLite3 library for persistence
find_package(SQLite3 REQUIRED)

# Platform-specific socket libraries
if(WIN32)
    set(PLATFORM_NETWORK_LIBS ws2_32)
else()
    set(PLATFORM_NETWORK_LIBS "")
endif()

# Shared common protocol and data structures
include_directories(common/include)

# Server executable target
add_executable(chat_server
    server/src/main.cpp
    server/src/server.cpp
    server/src/client_manager.cpp
    server/src/room_manager.cpp
    server/src/message_store.cpp
    server/src/logger.cpp
)
target_include_directories(chat_server PRIVATE server/include common/include)
target_link_libraries(chat_server PRIVATE Threads::Threads ${PLATFORM_NETWORK_LIBS} SQLite::SQLite3)

# Client executable target
add_executable(chat_client
    client/src/main.cpp
    client/src/client.cpp
    client/src/sender.cpp
    client/src/receiver.cpp
)
target_include_directories(chat_client PRIVATE client/include common/include)
target_link_libraries(chat_client PRIVATE Threads::Threads ${PLATFORM_NETWORK_LIBS})
