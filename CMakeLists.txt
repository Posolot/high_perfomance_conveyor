cmake_minimum_required(VERSION 3.14)
project(Conveyor VERSION 1.0.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Project paths
set(PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/plugins)
set(PLUGIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/plugins)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
message(STATUS "Found OpenCV: ${OpenCV_VERSION}")

# ZeroMQ via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)
message(STATUS "Found ZeroMQ: ${ZMQ_VERSION}")

# Cap'n Proto via pkg-config
pkg_check_modules(CAPNP REQUIRED IMPORTED_TARGET capnp)
message(STATUS "Found Cap'n Proto: ${CAPNP_VERSION}")

# HDF5
# IMPORTANT:
# - project() enables C and CXX so FindHDF5 can run its C-based checks.
# - Use only keyword signatures for target_link_libraries().
set(HDF5_PREFER_PARALLEL OFF)
find_package(HDF5 REQUIRED COMPONENTS C CXX HL)
message(STATUS "Found HDF5: ${HDF5_VERSION}")
message(STATUS "HDF5 include dirs: ${HDF5_INCLUDE_DIRS}")
message(STATUS "HDF5 libraries: ${HDF5_LIBRARIES}")

# nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# System libraries for Linux
set(PLATFORM_LIBS)
if(UNIX AND NOT APPLE)
    list(APPEND PLATFORM_LIBS dl pthread)
endif()

# -----------------------------------------------------------------------------
# Helper for HDF5 linkage
# -----------------------------------------------------------------------------
function(link_hdf5 target_name)
    target_include_directories(${target_name} PRIVATE ${HDF5_INCLUDE_DIRS})

    if(HDF5_DEFINITIONS)
        target_compile_definitions(${target_name} PRIVATE ${HDF5_DEFINITIONS})
    endif()

    if(TARGET HDF5::HDF5)
        target_link_libraries(${target_name} PRIVATE HDF5::HDF5)
    else()
        target_link_libraries(${target_name} PRIVATE ${HDF5_LIBRARIES})
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Cap'n Proto generated sources
# -----------------------------------------------------------------------------
set(CAPNP_SOURCES ${CMAKE_SOURCE_DIR}/src/frame.capnp.c++)
set(CAPNP_HEADERS ${CMAKE_SOURCE_DIR}/src/frame.capnp.h)

# -----------------------------------------------------------------------------
# Executable: pipeline
# -----------------------------------------------------------------------------
add_executable(pipeline
    ${CMAKE_SOURCE_DIR}/src/pipeline.cpp
    ${CAPNP_SOURCES}
)

target_include_directories(pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${CAPNP_INCLUDE_DIRS}
)

target_link_libraries(pipeline PRIVATE
    ${OpenCV_LIBS}
    PkgConfig::ZMQ
    PkgConfig::CAPNP
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
)

link_hdf5(pipeline)

# -----------------------------------------------------------------------------
# Executable: sender
# -----------------------------------------------------------------------------
add_executable(sender
    ${CMAKE_SOURCE_DIR}/src/sender.cpp
    ${CAPNP_SOURCES}
)

target_include_directories(sender PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${ZMQ_INCLUDE_DIRS}
    ${CAPNP_INCLUDE_DIRS}
)

target_link_libraries(sender PRIVATE
    PkgConfig::ZMQ
    PkgConfig::CAPNP
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
)

link_hdf5(sender)

# -----------------------------------------------------------------------------
# Plugins
# -----------------------------------------------------------------------------
set(PLUGINS rgb2gray erode blur merge heavy fft sobel)
set(PLUGIN_TARGETS)

foreach(plugin IN LISTS PLUGINS)
    set(plugin_target ${plugin}_plugin)
    list(APPEND PLUGIN_TARGETS ${plugin_target})

    add_library(${plugin_target} SHARED ${PLUGIN_SRC_DIR}/${plugin}_plugin.cpp)

    target_include_directories(${plugin_target} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(${plugin_target} PRIVATE
        ${OpenCV_LIBS}
        ${PLATFORM_LIBS}
    )

    set_target_properties(${plugin_target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
        PREFIX ""
        SUFFIX ".so"
    )
endforeach()

add_dependencies(pipeline ${PLUGIN_TARGETS})
# -----------------------------------------------------------------------------
# Optional benchmark target
# -----------------------------------------------------------------------------
option(BUILD_BENCHMARK "Enable benchmark target" OFF)
if(BUILD_BENCHMARK)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(benchmark
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/run_benchmark.py
            --pipeline $<TARGET_FILE:pipeline>
            --sender $<TARGET_FILE:sender>
            --data-dir ${CMAKE_SOURCE_DIR}/test_data
            --config-dir ${CMAKE_SOURCE_DIR}/configs
            --duration 30
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark..."
        DEPENDS pipeline sender
    )
endif()

# -----------------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------------
install(TARGETS pipeline sender
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PLUGIN_OUTPUT_DIR}/
    DESTINATION bin/plugins
    FILES_MATCHING PATTERN "*.so"
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/configs
    DESTINATION share/${PROJECT_NAME}/configs
    PATTERN ".gitkeep" EXCLUDE
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/data
    DESTINATION share/${PROJECT_NAME}/data
    PATTERN ".gitkeep" EXCLUDE
)

# -----------------------------------------------------------------------------
# Info
# -----------------------------------------------------------------------------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Outputs:")
message(STATUS "  Pipeline: $<TARGET_FILE:pipeline>")
message(STATUS "  Sender:   $<TARGET_FILE:sender>")
message(STATUS "  Plugins:  ${PLUGIN_OUTPUT_DIR}")
