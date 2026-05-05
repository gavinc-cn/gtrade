get_filename_component(DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

find_package(Boost REQUIRED COMPONENTS system stacktrace_backtrace)
message(STATUS "Boost_FOUND=${Boost_FOUND}")
message(STATUS "Boost_INCLUDE_DIRS=${Boost_INCLUDE_DIRS}")
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

# ZMQ（远程策略部署通信层）
# 依赖：apt-get install -y libzmq3-dev（Ubuntu/Debian）
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZMQ libzmq)
endif()
if(NOT ZMQ_FOUND)
    # 回退：直接查找 libzmq（跳过 pkg-config）
    find_library(ZMQ_LIBRARIES NAMES zmq libzmq)
    find_path(ZMQ_INCLUDE_DIRS zmq.h)
    if(ZMQ_LIBRARIES AND ZMQ_INCLUDE_DIRS)
        set(ZMQ_FOUND TRUE)
    endif()
endif()
if(ZMQ_FOUND)
    message(STATUS "ZMQ found: ${ZMQ_LIBRARIES}")
    include_directories(SYSTEM ${ZMQ_INCLUDE_DIRS})
else()
    message(WARNING "ZMQ not found. Remote strategy deployment mode will not be available. "
                    "Install: apt-get install -y libzmq3-dev")
    # 定义空占位，使后续 target_link_libraries 不报错
    set(ZMQ_LIBRARIES "")
endif()


# 第三方库使用SYSTEM标记，忽略其编译警告
include_directories(SYSTEM ${SRC_DIR}/3rd/ta-lib-0.6.4/include)
link_directories(${SRC_DIR}/3rd/ta-lib-0.6.4/lib)

include_directories(SYSTEM ${SRC_DIR}/3rd/cityhash-1.1.1/include)
link_directories(${SRC_DIR}/3rd/cityhash-1.1.1/lib)

include_directories(SYSTEM ${SRC_DIR}/3rd/clickhouse-cpp-2.5.1/include)
link_directories(${SRC_DIR}/3rd/clickhouse-cpp-2.5.1/lib)
#include_directories(/opt/clickhouse-cpp-2.5.1/include)
#link_directories(/opt/clickhouse-cpp-2.5.1/lib)

include_directories(SYSTEM /opt/mongo-cxx-driver-r3.8.1/include/mongocxx/v_noabi)
include_directories(SYSTEM /opt/mongo-cxx-driver-r3.8.1/include/bsoncxx/v_noabi)
link_directories(/opt/mongo-cxx-driver-r3.8.1/lib64)

#include_directories(/opt/mongo-c-driver-1.24.3/include)
#link_directories(/opt/mongo-c-driver-1.24.3/lib64)

# 项目源码目录（会检查警告）
include_directories(
        ${SRC_DIR}
        ${SRC_DIR}/pch
        ${SRC_DIR}/utilities
        ${SRC_DIR}/interface
        ${SRC_DIR}/common
        ${SRC_DIR}/backtest
        ${SRC_DIR}/backtest/event_source
        ${SRC_DIR}/websocket
        ${SRC_DIR}/strategy_engine
        ${SRC_DIR}/query_server
        ${SRC_DIR}/clients
        ${SRC_DIR}/message_server
        ${SRC_DIR}/db_server
        ${SRC_DIR}/http_server
        ${SRC_DIR}/desktop_gateway
        ${SRC_DIR}/services
        ${SRC_DIR}/3rd/jwt-cpp/include
)

# 第三方库目录使用SYSTEM标记，忽略警告
include_directories(SYSTEM ${SRC_DIR}/3rd)

# Build common libraries (shared between live and backtest)
add_subdirectory(pch)
add_subdirectory(utilities)
add_subdirectory(interface)
add_subdirectory(common)
add_subdirectory(desktop_gateway)

if (GTRADE_ENABLE_CLIENT_TEST)
    add_subdirectory(clients/test)
endif ()

# Collect main source files
file(GLOB MAIN_SRC_LIST *.cpp)

# Collect business module source files (compiled directly)
file(GLOB WEBSOCKET_SRC websocket/*.cpp)
list(REMOVE_ITEM WEBSOCKET_SRC ${CMAKE_CURRENT_SOURCE_DIR}/websocket/huobi_ws.cpp)

file(GLOB STRATEGY_ENGINE_SRC
    strategy_engine/*.cpp
    strategy/*.cpp
    strategy_engine/engine_handlers/*.cpp
)

file(GLOB CLIENTS_SRC clients/*.cpp)
list(REMOVE_ITEM CLIENTS_SRC 
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/HuobiClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/mongo_client.cpp
)

file(GLOB QUERY_SERVER_SRC query_server/*.cpp)

file(GLOB MESSAGE_SERVER_SRC message_server/*.cpp)
list(REMOVE_ITEM MESSAGE_SERVER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/message_server/message_server_example.cpp
)

file(GLOB DB_SERVER_SRC db_server/*.cpp)

file(GLOB HTTP_SERVER_SRC http_server/*.cpp)

# Note: desktop_gateway is built as a separate library (see add_subdirectory above)
# and linked below, so we don't collect its source files here

# Combine all source files
file(GLOB ALL_SRC_FILES
        ${MAIN_SRC_LIST}
        ${WEBSOCKET_SRC}
        ${STRATEGY_ENGINE_SRC}
        ${CLIENTS_SRC}
        ${QUERY_SERVER_SRC}
        ${MESSAGE_SERVER_SRC}
        ${DB_SERVER_SRC}
        ${HTTP_SERVER_SRC}
)

# Common libraries and external dependencies
set(COMMON_LIBS
    utilities
    interface
    common
    desktop_gateway
    yaml-cpp
    ssl
    crypto
    boost_system
    curl
    cryptopp
    z
    stdc++fs
    zrtools
    clickhouse-cpp-lib
    lz4
    cityhash.a
    mysqlclient
    dl          # Dynamic linking support (for Boost.Stacktrace)
    backtrace   # libbacktrace (for Boost.Stacktrace with backtrace backend)
    boost_stacktrace_backtrace
    ${ZMQ_LIBRARIES}   # ZMQ（远程策略部署）
)

# Build gtrade executable (live trading mode)
add_executable(${PROJECT_NAME} ${ALL_SRC_FILES})
target_compile_definitions(${PROJECT_NAME} PRIVATE GTRADE_IN_BACKTEST_MODE=0)
target_precompile_headers(${PROJECT_NAME} REUSE_FROM pch)
target_link_libraries(${PROJECT_NAME} ${COMMON_LIBS})
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${TARGET_VERSION})

# Build gtrade_bt executable (backtest mode)
file(GLOB BT_SRC_FILES
        strategy_engine/strategy_base.cpp
        strategy_engine/strategy_factory.cpp
        strategy_engine/order_manager.cpp
        strategy_engine/kline_manager.cpp
        strategy/*.cpp
        websocket/*.cpp
        clients/*.cpp
        query_server/*.cpp
        message_server/*.cpp
        db_server/*.cpp
        http_server/*.cpp
        backtest/*.cpp
        backtest/event_source/*.cpp
)
list(REMOVE_ITEM BT_SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/websocket/huobi_ws.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/websocket/dummy_trade.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/websocket/csv_quote.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/clients/HuobiClient.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/clients/mongo_client.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/message_server/message_server_example.cpp
)
add_executable(${PROJECT_NAME}_bt ${BT_SRC_FILES})
target_compile_definitions(${PROJECT_NAME}_bt PRIVATE GTRADE_IN_BACKTEST_MODE=1)
target_precompile_headers(${PROJECT_NAME}_bt REUSE_FROM pch)
target_link_libraries(${PROJECT_NAME}_bt ${COMMON_LIBS})
set_target_properties(${PROJECT_NAME}_bt PROPERTIES VERSION ${TARGET_VERSION})

# Build gtrade_strategy_runner executable (subprocess strategy runner)
# 子进程策略运行器：由 OutProcessProxy 通过 fork+exec 启动，
# 在独立进程中运行单个策略 .so 插件，崩溃时不影响主进程。
add_executable(${PROJECT_NAME}_strategy_runner strategy_runner/main.cpp
    strategy_engine/strategy_base.cpp
)
target_compile_definitions(${PROJECT_NAME}_strategy_runner PRIVATE
    GTRADE_IN_BACKTEST_MODE=0
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    _UNIX
    CPPHTTPLIB_OPENSSL_SUPPORT
)
target_include_directories(${PROJECT_NAME}_strategy_runner PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/pch
    ${SRC_DIR}/interface
    ${SRC_DIR}/common
    ${SRC_DIR}/strategy_engine
    ${SRC_DIR}/utilities
    ${SRC_DIR}/3rd
    ${SRC_DIR}/3rd/jwt-cpp/include
    ${Boost_INCLUDE_DIRS}
)
target_precompile_headers(${PROJECT_NAME}_strategy_runner REUSE_FROM pch)
target_link_libraries(${PROJECT_NAME}_strategy_runner
    utilities
    interface
    common
    yaml-cpp
    ssl
    crypto
    boost_system
    stdc++fs
    zrtools
    dl
    pthread
    rt
    ${ZMQ_LIBRARIES}   # ZMQ（远程策略部署）
)
set_target_properties(${PROJECT_NAME}_strategy_runner PROPERTIES VERSION ${TARGET_VERSION})

# Build gtrade_repl executable (standalone replication process)
add_executable(${PROJECT_NAME}_repl services/gtrade_repl.cpp)
target_link_libraries(${PROJECT_NAME}_repl
    interface
    utilities
    boost_system
    ssl
    crypto
    pthread
    rt
)
set_target_properties(${PROJECT_NAME}_repl PROPERTIES VERSION ${TARGET_VERSION})

message(CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR})
message(CMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../config DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../strategy_config DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/strategy_param DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/certs DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/start.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/status.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/stop.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/restart.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/clean.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(PROGRAMS ${CMAKE_SOURCE_DIR}/deploy/clean_restart.sh DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS ${PROJECT_NAME}_bt DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS ${PROJECT_NAME}_repl DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS ${PROJECT_NAME}_strategy_runner DESTINATION ${CMAKE_INSTALL_PREFIX})

# ============================================================
# 策略动态插件构建宏
#
# 用法：
#   add_strategy_plugin(<target> <sources...> <build_mode>)
#     target     : 目标名，输出 bin/strategy_plugins/lib<target>.so
#     sources    : 策略 .cpp 源文件（可多个）
#     build_mode : 0 = 实盘（与 gtrade 配套）
#                  1 = 回测（与 gtrade_bt 配套）
#
# 示例（在本文件末尾追加）：
#   add_strategy_plugin(strategy_my_grid_live src/strategy/my_grid.cpp 0)
#   add_strategy_plugin(strategy_my_grid_bt   src/strategy/my_grid.cpp 1)
#
# 注意：
#   插件 .so 不链接 StrategyBase / EnginePool 等主进程对象文件；
#   这些符号在 dlopen(RTLD_GLOBAL) 时从主进程全局符号表解析（-rdynamic 已全局设置）。
# ============================================================
macro(add_strategy_plugin PLUGIN_NAME PLUGIN_SOURCES BUILD_MODE)
    add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})

    # 与主二进制相同的头文件搜索路径
    target_include_directories(${PLUGIN_NAME} PRIVATE
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

    # 与主二进制相同的编译宏
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
        GTRADE_IN_BACKTEST_MODE=${BUILD_MODE}
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
        _UNIX
        CPPHTTPLIB_OPENSSL_SUPPORT
    )

    # 覆盖全局 CMAKE_CXX_FLAGS 中的 -Wl,-Bsymbolic：
    # 插件的未定义符号需在运行时从主进程解析，不能提前绑定到插件自身。
    # -Wl,-Bsymbolic-functions 只对函数指针做局部绑定，比完全去除更安全。
    target_link_options(${PLUGIN_NAME} PRIVATE
        "LINKER:-Bsymbolic-functions"
    )

    # 只链接插件自身实际使用的外部库；
    # StrategyBase / MyHandler / EnginePool 等运行时从主进程解析，不在此链接。
    target_link_libraries(${PLUGIN_NAME} PRIVATE yaml-cpp)

    # 输出到 bin/strategy_plugins/lib<target>.so
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/strategy_plugins
        PREFIX "lib"
    )
endmacro()

# ---- 在此处追加具体的插件目标 ----
# 示例（取消注释即可构建）：
# add_strategy_plugin(strategy_demo_plugin ${SRC_DIR}/strategy/strategy_demo.cpp 0)
# add_strategy_plugin(strategy_demo_plugin_bt ${SRC_DIR}/strategy/strategy_demo.cpp 1)