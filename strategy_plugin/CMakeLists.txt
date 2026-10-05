cmake_minimum_required(VERSION 3.10)
project(strategy_demo_plugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GTRADE_ROOT: points to the gtrade project root
# - standalone: cmake -DGTRADE_ROOT=/path/to/gtrade ..
# - as subdirectory: set automatically by parent CMakeLists.txt
if(NOT GTRADE_ROOT)
    get_filename_component(GTRADE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
    message(STATUS "GTRADE_ROOT not set, defaulting to: ${GTRADE_ROOT}")
endif()
set(SRC_DIR ${GTRADE_ROOT}/src)

# ── Dependencies ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

find_package(Boost REQUIRED COMPONENTS system)

# ── Compiler flags (aligned with main project) ──
set(CMAKE_CXX_FLAGS "-O3 -g -ggdb -fPIC -Wl,-Bsymbolic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=haswell")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++20-compat")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic")

# ── Common include paths ──
set(COMMON_INCLUDE_DIRS
    ${SRC_DIR}
    ${SRC_DIR}/pch
    ${SRC_DIR}/interface
    ${SRC_DIR}/common
    ${SRC_DIR}/strategy_engine
    ${SRC_DIR}/utilities
    ${SRC_DIR}/3rd
    ${SRC_DIR}/3rd/jwt-cpp/include
    ${Boost_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIRS}
)

set(COMMON_LINK_DIRS
    ${MYSQL_LIBRARY_DIRS}
    ${SRC_DIR}/3rd/ta-lib-0.6.4/lib
    ${SRC_DIR}/3rd/cityhash-1.1.1/lib
    ${SRC_DIR}/3rd/clickhouse-cpp-2.5.1/lib
)

set(COMMON_SYSTEM_INCLUDE_DIRS
    ${SRC_DIR}/3rd/ta-lib-0.6.4/include
    ${SRC_DIR}/3rd/cityhash-1.1.1/include
    ${SRC_DIR}/3rd/clickhouse-cpp-2.5.1/include
)

# ── Plugin sources ──
set(PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/strategy_demo.cpp
)

# ── Helper function to define a strategy plugin target ──
function(add_strategy_plugin TARGET_NAME BACKTEST_MODE)
    add_library(${TARGET_NAME} SHARED ${PLUGIN_SOURCES})
    target_compile_definitions(${TARGET_NAME} PRIVATE
        GTRADE_IN_BACKTEST_MODE=${BACKTEST_MODE}
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
        _UNIX
        CPPHTTPLIB_OPENSSL_SUPPORT
    )
    target_include_directories(${TARGET_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${COMMON_SYSTEM_INCLUDE_DIRS})
    target_link_directories(${TARGET_NAME} PRIVATE ${COMMON_LINK_DIRS})
    target_link_options(${TARGET_NAME} PRIVATE "LINKER:-Bsymbolic-functions")
    target_link_libraries(${TARGET_NAME} PRIVATE yaml-cpp)
    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/strategy_plugins
        PREFIX "lib"
    )
endfunction()

# ── Live trading plugin ──
add_strategy_plugin(strategy_demo_plugin 0)

# ── Backtest plugin ──
add_strategy_plugin(strategy_demo_plugin_bt 1)
