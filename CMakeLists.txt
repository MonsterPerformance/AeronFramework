cmake_minimum_required(VERSION 3.20)
project(AeronCustomStatic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_definitions(_DEFAULT_SOURCE)
add_compile_definitions(HAVE_STRUCT_MMSGHDR)

add_compile_definitions(AERON_VERSION_TXT="1.55.0-SNAPSHOT")
add_compile_definitions(AERON_VERSION_MAJOR=1)
add_compile_definitions(AERON_VERSION_MINOR=55)
add_compile_definitions(AERON_VERSION_PATCH=0)
add_compile_definitions(AERON_VERSION_GITSHA="custom-static-build")

file(GLOB_RECURSE LIB_SOURCES 
    "${PROJECT_SOURCE_DIR}/aeron-client/src/main/c/*.c"
    "${PROJECT_SOURCE_DIR}/aeron-client/src/main/cpp_wrapper/*.cpp"
    "${PROJECT_SOURCE_DIR}/aeron-driver/src/main/c/*.c"
)

list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/aeronmd\\.c$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/aeron_driver_main\\.c$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/aeron_windows\\.c$")

add_library(AeronGateway STATIC ${LIB_SOURCES})

target_include_directories(AeronGateway PUBLIC 
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-client>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-client/src/main/c>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-client/src/main/cpp_wrapper>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-driver>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-driver/src/main/c>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-driver/src/main/c/media>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/aeron-driver/src/main/c/uri>
)

find_package(Threads REQUIRED)
target_link_libraries(AeronGateway PRIVATE Threads::Threads rt)

add_executable(client_example "${PROJECT_SOURCE_DIR}/examples/client.cpp")
target_link_libraries(client_example PRIVATE AeronGateway m)

add_executable(server_example "${PROJECT_SOURCE_DIR}/examples/server.cpp")
target_link_libraries(server_example PRIVATE AeronGateway m)

add_executable(aeron_driver "${PROJECT_SOURCE_DIR}/aeron-driver/src/main/c/aeronmd.c")
target_link_libraries(aeron_driver PRIVATE AeronGateway m)

if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    target_compile_options(AeronGateway PRIVATE -Wno-stringop-overread)
    target_compile_options(aeron_driver PRIVATE -Wno-stringop-overread)
endif()

include(GNUInstallDirs)

install(TARGETS AeronGateway EXPORT AeronTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS aeron_driver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY "${PROJECT_SOURCE_DIR}/aeron-client/src/main/c/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aeron
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/aeron-client/src/main/cpp_wrapper/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aeron
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

add_executable(aeron-stat "${PROJECT_SOURCE_DIR}/aeron-stat/src/main.cpp")
target_link_libraries(aeron-stat PRIVATE AeronGateway m)

include(CMakePackageConfigHelpers)

set(AERON_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Aeron")

install(EXPORT AeronTargets
    FILE AeronTargets.cmake
    NAMESPACE Aeron::
    DESTINATION ${AERON_CMAKE_DIR}
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AeronConfig.cmake" [[
include(CMakeFindDependencyMacro)
find_dependency(Threads)

include("${CMAKE_CURRENT_LIST_DIR}/AeronTargets.cmake")

if(TARGET Aeron::AeronGateway)
    set_target_properties(Aeron::AeronGateway PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include/aeron"
    )
endif()
]])

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AeronConfig.cmake" DESTINATION ${AERON_CMAKE_DIR})
