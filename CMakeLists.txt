cmake_minimum_required(VERSION 3.10)
project(gtrade)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release)

include(functions.cmake)

if (GTRADE_ENABLE_DISTRIBUTED_COMPILATION)
    find_program(ICECC icecc)
    if(ICECC)
        set(CMAKE_C_COMPILER_LAUNCHER   ${ICECC})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${ICECC})
        message(STATUS "Using icecc for distributed compilation")
    else()
        message(STATUS "Local compilation")
    endif()
endif ()

create_version(TARGET_VERSION)
message(TARGET_VERSION=${TARGET_VERSION})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INSTALL_PREFIX  ${CMAKE_BINARY_DIR}/output)
message(CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX})

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS "-O0 -g -ggdb -fPIC -Wl,-Bsymbolic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
else ()
    set(CMAKE_CXX_FLAGS "-O3 -g -ggdb -fPIC -Wl,-Bsymbolic")
endif ()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic")
message(CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

add_compile_definitions(_UNIX)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)
add_compile_definitions(BOOST_STACKTRACE_USE_BACKTRACE)

# Find MySQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
include_directories(${MYSQL_INCLUDE_DIRS})
link_directories(${MYSQL_LIBRARY_DIRS})

# 忽略一些检查
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=haswell")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++20-compat")

# 增加一些检查
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=non-virtual-dtor")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=uninitialized")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=format=2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-nonliteral")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=float-equal")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=array-bounds")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=reorder")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=implicit-fallthrough")

message(CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS})

if (GTRADE_ENABLE_TEST_CODE)
    add_compile_definitions(GTRADE_ENABLE_TEST_CODE)
    add_compile_definitions(GTRADE_ENABLE_CLIENT_TEST)
endif ()
if (GTRADE_ENABLE_LATENCY_TEST)
    add_compile_definitions(GTRADE_ENABLE_LATENCY_TEST)
endif ()

set(GTRADE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(src)
#add_subdirectory(strategy_plugin)
add_subdirectory(test)

#set(PERMISION_755
#        OWNER_READ OWNER_WRITE OWNER_EXECUTE
#        GROUP_READ GROUP_EXECUTE
#        WORLD_READ WORLD_EXECUTE)
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/start.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/stop.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/status.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deploy DESTINATION ${CMAKE_INSTALL_PREFIX})