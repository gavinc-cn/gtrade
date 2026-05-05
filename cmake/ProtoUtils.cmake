# ProtoUtils.cmake - Utilities for generating protobuf and gRPC code
#
# This module provides functions to automatically generate C++ code from .proto files.
#
# Functions:
#   generate_grpc_cpp(
#       PROTO_DIR <directory>           # Directory containing .proto files
#       GENERATED_DIR <directory>       # Output directory for generated files
#       OUT_SRCS <variable>            # Variable to receive list of generated .cc files
#       OUT_HDRS <variable>            # Variable to receive list of generated .h files
#       [PLUGIN <path>]                # Optional: gRPC C++ plugin path (auto-detected if not provided)
#   )
#
# Example usage:
#   include(cmake/ProtoUtils.cmake)
#   generate_grpc_cpp(
#       PROTO_DIR "${CMAKE_SOURCE_DIR}/proto"
#       GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated"
#       OUT_SRCS PROTO_GENERATED_SRCS
#       OUT_HDRS PROTO_GENERATED_HDRS
#   )

function(generate_grpc_cpp)
    # Parse arguments
    set(options "")
    set(oneValueArgs PROTO_DIR GENERATED_DIR OUT_SRCS OUT_HDRS PLUGIN)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Validate required arguments
    if(NOT ARG_PROTO_DIR)
        message(FATAL_ERROR "generate_grpc_cpp: PROTO_DIR is required")
    endif()
    if(NOT ARG_GENERATED_DIR)
        message(FATAL_ERROR "generate_grpc_cpp: GENERATED_DIR is required")
    endif()
    if(NOT ARG_OUT_SRCS)
        message(FATAL_ERROR "generate_grpc_cpp: OUT_SRCS is required")
    endif()
    if(NOT ARG_OUT_HDRS)
        message(FATAL_ERROR "generate_grpc_cpp: OUT_HDRS is required")
    endif()

    # Auto-detect gRPC C++ plugin if not provided
    if(NOT ARG_PLUGIN)
        if(WIN32)
            # On Windows with vcpkg, find it through gRPC package
            if(TARGET gRPC::grpc_cpp_plugin)
                get_target_property(ARG_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
                if(NOT ARG_PLUGIN)
                    get_target_property(ARG_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_DEBUG)
                endif()
                if(NOT ARG_PLUGIN)
                    get_target_property(ARG_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
                endif()
            endif()
        else()
            # On Linux, find it in PATH
            find_program(ARG_PLUGIN grpc_cpp_plugin)
        endif()

        if(NOT ARG_PLUGIN)
            message(FATAL_ERROR "generate_grpc_cpp: Could not find grpc_cpp_plugin. Please specify PLUGIN argument.")
        endif()
    endif()

    # Auto-discover all .proto files in the proto directory
    file(GLOB PROTO_FILES "${ARG_PROTO_DIR}/*.proto")

    if(NOT PROTO_FILES)
        message(WARNING "generate_grpc_cpp: No .proto files found in ${ARG_PROTO_DIR}")
        return()
    endif()

    # Initialize lists for generated files
    set(ALL_PROTO_SRCS)
    set(ALL_PROTO_HDRS)
    set(ALL_GRPC_SRCS)
    set(ALL_GRPC_HDRS)

    # Generate C++ code for each proto file
    foreach(PROTO_FILE ${PROTO_FILES})
        # Get the filename without extension
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

        # Define generated file paths
        set(PROTO_SRC "${ARG_GENERATED_DIR}/${PROTO_NAME}.pb.cc")
        set(PROTO_HDR "${ARG_GENERATED_DIR}/${PROTO_NAME}.pb.h")
        set(GRPC_SRC "${ARG_GENERATED_DIR}/${PROTO_NAME}.grpc.pb.cc")
        set(GRPC_HDR "${ARG_GENERATED_DIR}/${PROTO_NAME}.grpc.pb.h")

        # Add to the lists
        list(APPEND ALL_PROTO_SRCS ${PROTO_SRC})
        list(APPEND ALL_PROTO_HDRS ${PROTO_HDR})
        list(APPEND ALL_GRPC_SRCS ${GRPC_SRC})
        list(APPEND ALL_GRPC_HDRS ${GRPC_HDR})

        # Custom command to generate protobuf and gRPC code for this proto file
        # Each proto file has its own command, so only modified protos will be regenerated
        add_custom_command(
            OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_GENERATED_DIR}
            COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${ARG_GENERATED_DIR}
                --grpc_out=${ARG_GENERATED_DIR}
                --plugin=protoc-gen-grpc=${ARG_PLUGIN}
                -I=${ARG_PROTO_DIR}
                ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating gRPC C++ code from ${PROTO_NAME}.proto"
            VERBATIM
        )
    endforeach()

    # Print discovered proto files for debugging
    message(STATUS "Auto-discovered proto files in ${ARG_PROTO_DIR}:")
    foreach(PROTO_FILE ${PROTO_FILES})
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME)
        message(STATUS "  - ${PROTO_NAME}")
    endforeach()

    # Combine all generated sources and headers
    set(ALL_GENERATED_SRCS ${ALL_PROTO_SRCS} ${ALL_GRPC_SRCS})
    set(ALL_GENERATED_HDRS ${ALL_PROTO_HDRS} ${ALL_GRPC_HDRS})

    # Return the lists to the caller
    set(${ARG_OUT_SRCS} ${ALL_GENERATED_SRCS} PARENT_SCOPE)
    set(${ARG_OUT_HDRS} ${ALL_GENERATED_HDRS} PARENT_SCOPE)
endfunction()
