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
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-result")
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
    set(GTRADE_ENABLE_CLIENT_TEST ON)
    add_compile_definitions(GTRADE_ENABLE_CLIENT_TEST)
endif ()
if (GTRADE_ENABLE_LATENCY_TEST)
    add_compile_definitions(GTRADE_ENABLE_LATENCY_TEST)
endif ()
# MCP HTTP 交易接口（place_order/cancel_order，见 src/http_server/http_gateway.cpp），
# 默认 OFF；开启后引擎开放 :46012 /api/trade/* 下单/撤单路由
option(GTRADE_ENABLE_HTTP_TRADE "Enable MCP HTTP trade endpoints (place/cancel order via :46012)" OFF)
if (GTRADE_ENABLE_HTTP_TRADE)
    add_compile_definitions(GTRADE_ENABLE_HTTP_TRADE)
endif ()

# 主线 wire2wire 延时探测（详见 doc_ai/20260629_1401_主线wire2wire延时探测_DPDK整合方案.md）
# 两个宏相互正交，均默认 OFF（OFF 时主线行为完全不变）：
#   GTRADE_ENABLE_DPDK_PROBE     —— 启用 src/latency_probe/ 的 DPDK 行情/交易模块，
#                                    平级替换 OkxWs/OkxTrade 的 NIC 边界（需系统已装 DPDK）。
#   GTRADE_LATENCY_SINGLE_THREAD —— 方案C：核心链路（Engine+策略+DpdkTradeSink）全绑 SyncThread，
#                                    0 跨线程 0 队列往返，整条链在 DPDK busy-poll 核同步串行。
option(GTRADE_ENABLE_DPDK_PROBE "Enable DPDK wire2wire latency probe (replace OkxWs/OkxTrade NIC boundary)" OFF)
option(GTRADE_LATENCY_SINGLE_THREAD "Plan C: single-thread core link for latency probe (Engine+strategy+sink on SyncThread)" OFF)
if (GTRADE_ENABLE_DPDK_PROBE)
    add_compile_definitions(GTRADE_ENABLE_DPDK_PROBE)
endif ()
if (GTRADE_LATENCY_SINGLE_THREAD)
    add_compile_definitions(GTRADE_LATENCY_SINGLE_THREAD)
endif ()

set(GTRADE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(src)

# 独立最小低延时交易系统（单线程，无网络），按需开启
# cmake .. -DGTRADE_BUILD_LATENCY_BENCH=ON
option(GTRADE_BUILD_LATENCY_BENCH "Build standalone low-latency trading bench (single-thread, no network)" OFF)
if(GTRADE_BUILD_LATENCY_BENCH)
    add_subdirectory(src/latency_bench)
endif()

#add_subdirectory(strategy_plugin)

# 测试构建开关：Debug 默认 ON；其余（含未设 build type 的 -O3 构建）默认 OFF
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(GTRADE_BUILD_TESTING_DEFAULT ON)
else ()
    set(GTRADE_BUILD_TESTING_DEFAULT OFF)
endif ()
option(GTRADE_BUILD_TESTING "Build unit/integration tests under test/" ${GTRADE_BUILD_TESTING_DEFAULT})

if (GTRADE_BUILD_TESTING)
    enable_testing()    # 必须在顶层调用，否则构建根目录下 ctest 发现不了用例
    add_subdirectory(test)
endif ()

#set(PERMISION_755
#        OWNER_READ OWNER_WRITE OWNER_EXECUTE
#        GROUP_READ GROUP_EXECUTE
#        WORLD_READ WORLD_EXECUTE)
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/start.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/stop.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/release/status.sh DESTINATION ${CMAKE_INSTALL_PREFIX} PERMISSIONS ${PERMISION_755})
#install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deploy DESTINATION ${CMAKE_INSTALL_PREFIX})