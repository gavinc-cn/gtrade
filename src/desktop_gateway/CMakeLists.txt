cmake_minimum_required(VERSION 3.10)

# Find required packages
find_package(Protobuf REQUIRED)

# Find gRPC using PkgConfig (for apt-installed gRPC)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(GRPCPP REQUIRED grpc++)

# Find gRPC C++ plugin
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# Include ProtoUtils for automatic proto generation
include(${CMAKE_SOURCE_DIR}/cmake/ProtoUtils.cmake)

## Generate gRPC C++ code from all .proto files
#generate_grpc_cpp(
#    PROTO_DIR "${CMAKE_SOURCE_DIR}/proto"
#    GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated"
#    OUT_SRCS PROTO_GENERATED_SRCS
#    OUT_HDRS PROTO_GENERATED_HDRS
#    PLUGIN ${GRPC_CPP_PLUGIN}
#)

# Desktop Gateway Library (gRPC implementation only)
# Collect proto generated files
file(GLOB PROTO_SRC_LIST
    ${CMAKE_SOURCE_DIR}/src/proto/*.cc
)

add_library(desktop_gateway
    desktop_gateway_grpc_service.cpp
    ${PROTO_SRC_LIST}
    ${PROTO_GENERATED_SRCS}
)

target_include_directories(desktop_gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/proto
    ${CMAKE_SOURCE_DIR}/src/interface
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/clients
    ${CMAKE_SOURCE_DIR}/src/strategy_engine
    ${CMAKE_SOURCE_DIR}/src/utilities
    ${CMAKE_SOURCE_DIR}/src/3rd/jwt-cpp/include
    ${Protobuf_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
    ${GRPCPP_INCLUDE_DIRS}
)

target_link_libraries(desktop_gateway
        common
        utilities
        ${Protobuf_LIBRARIES}
        ${GRPC_LIBRARIES}
        ${GRPCPP_LIBRARIES}
        protobuf
        grpc++
)

# Tests (if enabled)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
