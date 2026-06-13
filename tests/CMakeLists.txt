find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

set(TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/common/src/squidprotocol/squidprotocol.cpp
    ${CMAKE_SOURCE_DIR}/common/src/squidprotocol/squidProtocolFormatter.cpp
    ${CMAKE_SOURCE_DIR}/common/src/filesystem/filetransfer.cpp
    ${CMAKE_SOURCE_DIR}/common/src/filesystem/filemanager.cpp
    ${CMAKE_SOURCE_DIR}/common/src/filesystem/filelock.cpp
    ${CMAKE_SOURCE_DIR}/common/src/peer/peer.cpp
    ${CMAKE_SOURCE_DIR}/common/src/networking/TCPConnectorChannel.cpp
    ${CMAKE_SOURCE_DIR}/common/src/networking/TCPListenerChannel.cpp
    ${CMAKE_SOURCE_DIR}/common/src/networking/TCPSquidChannel.cpp
)

add_executable(SquidStorageTests
    test_formatter.cpp
    test_protocol.cpp
    test_config.cpp
    test_syncplanner.cpp
    test_integration.cpp
    ${TEST_SOURCES}
)

target_include_directories(SquidStorageTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(SquidStorageTests
    GTest::gtest
    GTest::gtest_main
    pthread
)

add_test(NAME SquidStorageTests COMMAND SquidStorageTests)
