cmake_minimum_required(VERSION 3.10)
project(SquidStorage)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Trova SDL2 usando sdl2-config (PkgConfig)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# === INCLUDE PATH ===
include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/imgui/backends
    ${CMAKE_SOURCE_DIR}/common/src
    ${CMAKE_SOURCE_DIR}/common/src/squidprotocol
    ${CMAKE_SOURCE_DIR}/common/src/filesystem
    ${CMAKE_SOURCE_DIR}/common/src/peer
    ${CMAKE_SOURCE_DIR}/server/src
    ${CMAKE_SOURCE_DIR}/client/src
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

# === IMGUI FILES ===
file(GLOB IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/imgui/imgui*.cpp
)

# === LINKER PATH (solo se necessario) ===
link_directories(/usr/local/lib)

# === EXECUTABLE: SquidStorage (GUI) ===
add_executable(SquidStorage
    imgui/src/main.cpp
    imgui/src/Application.cpp
    client/src/client.cpp
    ${COMMON_SOURCES}
    imgui/backends/imgui_impl_sdl2.cpp
    imgui/backends/imgui_impl_opengl2.cpp
    ${IMGUI_SOURCES}
)

# === LINK LIBRARIES ===
if(APPLE)
    target_link_libraries(SquidStorage
        ${SDL2_LIBRARIES}
        "-framework OpenGL"
    )
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(SquidStorage
        ${SDL2_LIBRARIES}
        ${OPENGL_gl_LIBRARY}
    )
endif()

# === EXECUTABLE: SquidStorageServer ===
add_executable(SquidStorageServer
    server/src/main.cpp
    server/src/server.cpp
    ${COMMON_SOURCES}
)