cmake_minimum_required(VERSION 3.10)
project(SquidStorage)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Trova SDL2 usando sdl2-config (PkgConfig)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Aggiungi include directories
include_directories(
    ${SDL2_INCLUDE_DIRS}               # Include path di SDL2 trovato con pkg-config
    ${CMAKE_SOURCE_DIR}/imgui         # ImGui core
    ${CMAKE_SOURCE_DIR}/imgui/backends # ImGui backends
    ${CMAKE_SOURCE_DIR}/common/src    # Tuoi file .h
)

# Aggiungi i file sorgente di ImGui
file(GLOB IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/imgui/imgui*.cpp
)

# Aggiungi il percorso di libreria specifico per SDL2 se è necessario (per /usr/local/lib)
link_directories(/usr/local/lib)

# Crea l'eseguibile con i sorgenti necessari
add_executable(SquidStorage
    imgui/src/main.cpp
    imgui/src/Application.cpp
    common/src/filesystem/filemanager.cpp
    common/src/filesystem/filelock.cpp
    imgui/backends/imgui_impl_sdl2.cpp
    imgui/backends/imgui_impl_opengl2.cpp
    ${IMGUI_SOURCES}
)

# Collega le librerie SDL2 e OpenGL
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