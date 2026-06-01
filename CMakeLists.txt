cmake_minimum_required(VERSION 3.10)
project(SquidStorage)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

# === INCLUDE PATH ===
include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/common/src
    ${CMAKE_SOURCE_DIR}/common/src/squidprotocol
    ${CMAKE_SOURCE_DIR}/common/src/filesystem
    ${CMAKE_SOURCE_DIR}/common/src/peer
    ${CMAKE_SOURCE_DIR}/server/src
    ${CMAKE_SOURCE_DIR}/client/src
    ${CMAKE_SOURCE_DIR}/datanode/src
)

# === FILES COMUNI ===
set(COMMON_SOURCES
    common/src/squidprotocol/squidprotocol.cpp
    common/src/squidprotocol/squidProtocolFormatter.cpp
    common/src/filesystem/filemanager.cpp
    common/src/filesystem/filelock.cpp
    common/src/peer/peer.cpp
    common/src/filesystem/filetransfer.cpp
)

link_directories(/usr/local/lib)

# === EXECUTABLE: Client ===
add_executable(SquidStorageClient
    client/src/main.cpp
    client/src/client.cpp
    ${COMMON_SOURCES}
)

# === EXECUTABLE: Server ===
add_executable(SquidStorageServer
    server/src/main.cpp
    server/src/server.cpp
    ${COMMON_SOURCES}
)

# === EXECUTABLE: DataNode ===
add_executable(DataNode
    datanode/src/datanode.cpp
    datanode/src/main.cpp
    ${COMMON_SOURCES}
)
