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
    ${CMAKE_SOURCE_DIR}/common/src/config
    ${CMAKE_SOURCE_DIR}/common/src/utils
    ${CMAKE_SOURCE_DIR}/common/src/squidprotocol
    ${CMAKE_SOURCE_DIR}/common/src/filesystem
    ${CMAKE_SOURCE_DIR}/common/src/peer
    ${CMAKE_SOURCE_DIR}/common/src/networking
    ${CMAKE_SOURCE_DIR}/server/src
    ${CMAKE_SOURCE_DIR}/client/src
    ${CMAKE_SOURCE_DIR}/datanode/src
)

set(COMMON_SOURCES
    common/src/squidprotocol/squidprotocol.cpp
    common/src/squidprotocol/squidProtocolFormatter.cpp
    common/src/filesystem/filemanager.cpp
    common/src/filesystem/filelock.cpp
    common/src/peer/peer.cpp
    common/src/config/ClusterConfig.hpp
    common/src/utils/logger.hpp
    common/src/filesystem/filetransfer.cpp
    common/src/networking/TCPConnectorChannel.cpp
    common/src/networking/TCPListenerChannel.cpp
    common/src/networking/TCPSquidChannel.cpp
    common/src/networking/ConnectionSession.hpp
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
    server/src/lock_manager.cpp
    server/src/replication_manager.cpp
    server/src/heartbeat_manager.cpp
    server/src/standby_replica_manager.cpp
    server/src/replica_watcher.cpp
    ${COMMON_SOURCES}
)

# === EXECUTABLE: DataNode ===
add_executable(DataNode
    datanode/src/datanode.cpp
    datanode/src/main.cpp
    ${COMMON_SOURCES}
)

# === EXECUTABLE: SquidFS FUSE mount ===
pkg_check_modules(FUSE3 REQUIRED fuse3)

add_executable(SquidFSMount
    client/src/squidfs_main.cpp
    client/src/squidfs.cpp
    client/src/client.cpp
    ${COMMON_SOURCES}
)

target_include_directories(SquidFSMount PRIVATE
    ${FUSE3_INCLUDE_DIRS}
)

target_compile_options(SquidFSMount PRIVATE
    ${FUSE3_CFLAGS_OTHER}
    -D_FILE_OFFSET_BITS=64
)

target_link_libraries(SquidFSMount PRIVATE
    ${FUSE3_LIBRARIES}
    pthread
)

# === TESTS ===
enable_testing()
add_subdirectory(tests)
