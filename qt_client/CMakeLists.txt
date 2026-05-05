cmake_minimum_required(VERSION 3.16)

# Include Conan setup module
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConanSetup.cmake)

# Auto-install Conan dependencies before project configuration
conan_auto_install()

project(GTradeClient VERSION 1.0.0 LANGUAGES CXX)

# Force Release runtime library for all configurations when using MSVC
# This is necessary because we always use Conan Release libraries
if(MSVC)
    # Override Conan's per-config setting to work with all build types
    # Using Release runtime (/MD) for all configs since Conan libs are Release-built
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    message(STATUS "MSVC: Using MultiThreadedDLL runtime for all configurations (Conan Release libs)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

message(CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR})

# Find Qt5 components
find_package(Qt5 REQUIRED COMPONENTS
        Core
        Widgets
        Network
        Gui
)

find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Protobuf_FOUND=${Protobuf_FOUND}")
message(STATUS "Protobuf_INCLUDE_DIRS=${Protobuf_INCLUDE_DIRS}")

find_package(gRPC CONFIG REQUIRED)
message(STATUS "gRPC_FOUND=${gRPC_FOUND}")
#message(STATUS "gRPC_INCLUDE_DIRS=${GRPC_INCLUDE_DIRS}")

find_package(yaml-cpp CONFIG REQUIRED)
message(STATUS "yaml-cpp_FOUND=${yaml-cpp_FOUND}")
#message(STATUS "yaml-cpp_INCLUDE_DIRS=${yaml-cpp_INCLUDE_DIRS}")

# Find Boost - try CONFIG first (modern), then MODULE (legacy for vcpkg)
# Set policy CMP0167 to OLD to use legacy FindBoost module on CMake 4.0+
if (POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif ()
find_package(Boost REQUIRED)
message(STATUS "Boost_FOUND=${Boost_FOUND}")
message(STATUS "Boost_INCLUDE_DIRS=${Boost_INCLUDE_DIRS}")

set(GRPC_LIBRARIES gRPC::grpc++)
set(GRPCPP_LIBRARIES gRPC::grpc++)
#set(GRPC_INCLUDE_DIRS "")
#set(GRPCPP_INCLUDE_DIRS "")

# Find gRPC C++ plugin
if (WIN32)
    # On Windows with vcpkg, find it through gRPC package
    get_target_property(GRPC_CPP_PLUGIN_LOCATION gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
    if (NOT GRPC_CPP_PLUGIN_LOCATION)
        get_target_property(GRPC_CPP_PLUGIN_LOCATION gRPC::grpc_cpp_plugin IMPORTED_LOCATION_DEBUG)
    endif ()
    if (NOT GRPC_CPP_PLUGIN_LOCATION)
        get_target_property(GRPC_CPP_PLUGIN_LOCATION gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
    endif ()
else ()
    # On Linux, find it in PATH
    find_program(GRPC_CPP_PLUGIN_LOCATION grpc_cpp_plugin REQUIRED)
endif ()

message(STATUS "Found grpc_cpp_plugin: ${GRPC_CPP_PLUGIN_LOCATION}")

# Include ProtoUtils for automatic proto generation
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ProtoUtils.cmake)

## Generate gRPC C++ code from all .proto files
#generate_grpc_cpp(
#    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
#    GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated"
#    OUT_SRCS PROTO_GENERATED_SRCS
#    OUT_HDRS PROTO_GENERATED_HDRS
#    PLUGIN ${GRPC_CPP_PLUGIN_LOCATION}
#)
#
#generate_grpc_cpp(
#    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
#    GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/desktop_gateway/generated"
#    OUT_SRCS PROTO_GENERATED_SRCS
#    OUT_HDRS PROTO_GENERATED_HDRS
#    PLUGIN ${GRPC_CPP_PLUGIN_LOCATION}
#)

# Include directories
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/models
        ${CMAKE_CURRENT_SOURCE_DIR}/include/services
        ${CMAKE_CURRENT_SOURCE_DIR}/include/views
        ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/include/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/proto
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utilities
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utilities/zrtools
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/3rd
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/3rd/spdlog/include
        ${Protobuf_INCLUDE_DIRS}
        ${GRPC_INCLUDE_DIRS}
        ${GRPCPP_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
)

# Boost include directory (SYSTEM to suppress warnings)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

# Source files
set(SOURCES
        src/main.cpp
        src/utils/NetworkManager.cpp
        src/utils/GrpcNetworkManager.cpp
        src/utils/ConfigManager.cpp
        src/utils/CredentialManager.cpp
        src/utils/DateTimeUtils.cpp
        src/services/DictService.cpp
        src/models/Strategy.cpp
        src/models/Order.cpp
        src/models/Trade.cpp
        src/models/Position.cpp
        src/views/LoginDialog.cpp
        src/views/MainWindow.cpp
        src/views/StrategyManager.cpp
        src/views/OrderManager.cpp
        src/views/TradeManager.cpp
        src/views/PositionManager.cpp
        src/views/PortfolioPositionManager.cpp
        src/widgets/FilterCard.cpp
        src/widgets/PaginationWidget.cpp
        ${PROTO_GENERATED_SRCS}
        # gtrade logger_config
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utilities/logger_config.cpp
)

# Header files
set(HEADERS
        include/utils/NetworkManager.h
        include/utils/GrpcNetworkManager.h
        include/utils/ConfigManager.h
        include/utils/CredentialManager.h
        include/utils/DateTimeUtils.h
        include/services/DictService.h
        include/models/Strategy.h
        include/models/Order.h
        include/models/Trade.h
        include/models/Position.h
        include/views/LoginDialog.h
        include/views/MainWindow.h
        include/views/StrategyManager.h
        include/views/OrderManager.h
        include/views/TradeManager.h
        include/views/PositionManager.h
        include/views/PortfolioPositionManager.h
        include/widgets/FilterCard.h
        include/widgets/PaginationWidget.h
        ${PROTO_GENERATED_HDRS}
)

# Create executable
file(GLOB SRC_LST
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/proto/*.cc
)
add_executable(${PROJECT_NAME} ${SRC_LST} ${SOURCES} ${HEADERS})

# Link Qt5 libraries, gRPC, Protobuf, yaml-cpp, and Boost.Stacktrace dependencies
target_link_libraries(${PROJECT_NAME}
        Qt5::Core
        Qt5::Widgets
        Qt5::Network
        Qt5::Gui
        protobuf::libprotobuf
        gRPC::grpc++
        yaml-cpp
)

# Platform-specific libraries for Boost.Stacktrace
if (WIN32)
    # Windows: Link with dbgeng (for windbg backend) or ole32/dbghelp (for windbg_cached)
    target_link_libraries(${PROJECT_NAME}
            ole32
            dbgeng
    )
elseif (UNIX)
    # Linux: Link with dl and backtrace
    target_link_libraries(${PROJECT_NAME}
            dl
            backtrace
    )
endif ()

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Add Boost compile definitions
if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            BOOST_STACKTRACE_USE_WINDBG_CACHED  # Use windbg_cached backend on Windows
            BOOST_ALL_NO_LIB                    # Disable Boost auto-linking (use CMake/Conan linking)
    )
elseif (UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            BOOST_STACKTRACE_USE_BACKTRACE  # Use backtrace backend on Linux
    )
endif ()

# Compiler options - Different for MSVC and GCC/Clang
if (MSVC)
    # MSVC specific options
    target_compile_options(${PROJECT_NAME} PRIVATE
            /W4                 # Warning level 4
            /permissive-        # Standards conformance mode
            /Zc:__cplusplus     # Enable correct __cplusplus macro
            /MP                 # Multi-processor compilation
            /EHsc               # Exception handling model
            /utf-8              # Set source and execution character sets to UTF-8
    )

    # Use generator expressions for multi-config generators (Visual Studio)
    # NOTE: /RTC1 is removed because it requires debug runtime (/MDd) but we use
    #       release runtime (/MD) for all configs to link with Conan Release libraries
    target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Release>:/O2>        # Maximum optimization (Release)
            $<$<CONFIG:Release>:/GL>        # Whole program optimization (Release)
            $<$<CONFIG:Debug>:/Zi>          # Generate debug information (Debug)
            $<$<CONFIG:Debug>:/Od>          # Disable optimization (Debug)
    )
    # Link-time code generation for Release
    set_target_properties(${PROJECT_NAME} PROPERTIES
            LINK_FLAGS_RELEASE "/LTCG"
    )

    # Disable specific MSVC warnings
    target_compile_options(${PROJECT_NAME} PRIVATE
            /wd4127         # Conditional expression is constant
            /wd4251         # Class needs to have dll-interface (Qt classes)
            /wd4267         # Conversion from size_t to int
            /wd4100         # Unreferenced formal parameter
            /wd4505         # Unreferenced function with internal linkage removed
    )

    # Add Windows subsystem (GUI application, no console) for Release builds
    set_target_properties(${PROJECT_NAME} PROPERTIES
            WIN32_EXECUTABLE $<CONFIG:Release>
    )
else ()
    # GCC/Clang/MinGW options
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${PROJECT_NAME} PRIVATE -O3 -Wall -g -rdynamic)
        target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
    else ()
        target_compile_options(${PROJECT_NAME} PRIVATE -g -Wall -rdynamic)
        target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
    endif ()
endif ()

# Platform-specific settings
if (WIN32)
    # Windows specific
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            WIN32_LEAN_AND_MEAN     # Exclude rarely-used stuff from Windows headers
            NOMINMAX                # Don't define min/max macros
            UNICODE                 # Use Unicode
            _UNICODE
    )
endif ()

# Install rules
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        COMPONENT applications
)

# Install configuration files
# Copy config.yml.template to bin directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.yml.template
        DESTINATION bin
        COMPONENT config
)

# Copy config.yml to bin directory if it exists in source directory
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config.yml)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.yml
            DESTINATION bin
            COMPONENT config
    )
endif ()

# Install resource files (stylesheets)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources
        DESTINATION bin
        COMPONENT resources
)

# Create a custom target to copy resources and config files (always runs)
# For multi-config generators (Visual Studio), files are copied to bin/$<CONFIG>/
add_custom_target(copy_resources_and_configs ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/$<CONFIG>

        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/resources

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.yml.template
        ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/config.yml.template

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.yml
        ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/config.yml

        COMMENT "Copying resources and config files to build directory"
)

# Ensure resources are copied before building the executable
add_dependencies(${PROJECT_NAME} copy_resources_and_configs)

## Windows-specific: Deploy Qt DLLs and dependencies
#if(WIN32)
#    # Find Qt5 bin directory for windeployqt
#    get_target_property(QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
#    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
#
#    # Determine vcpkg bin directory
#    set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed")
#    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#        set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/x64-windows/debug/bin")
#    else()
#        set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/x64-windows/bin")
#    endif()
#
#    # Certificate directory
#    set(CERT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deploy/certs")
#
#    # Use a CMake script for deployment to handle errors gracefully
#    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#        COMMAND ${CMAKE_COMMAND}
#            -DTARGET_FILE=$<TARGET_FILE:${PROJECT_NAME}>
#            -DTARGET_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
#            -DQT_BIN_DIR=${QT_BIN_DIR}
#            -DVCPKG_BIN_DIR=${VCPKG_BIN_DIR}
#            -DCERT_DIR=${CERT_SOURCE_DIR}
#            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake
#        COMMENT "Deploying Windows dependencies"
#    )
#endif()
