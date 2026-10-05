# 测试分层入口：unit（默认随 ctest 跑）/ integration（发布前跑）/ e2e（手动，需显式开启）
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

find_package(Boost REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

# 目录作用域，unit/ 与 e2e/ 子目录继承
# 注意顺序：websocket/ 与 strategy_engine/ 必须先于 backtest/——dummy_trade.h 与
# timer_manager_dummy.h 在两侧各有一份同名文件，unit_test 依赖本顺序解析到前者
include_directories(
        ${SRC_DIR}
        ${SRC_DIR}/pch
        ${SRC_DIR}/utilities
        ${SRC_DIR}/3rd
        ${SRC_DIR}/interface
        ${SRC_DIR}/common
        ${SRC_DIR}/websocket
        ${SRC_DIR}/strategy_engine
        ${SRC_DIR}/message_server
        ${SRC_DIR}/query_server
        ${SRC_DIR}/clients
        ${SRC_DIR}/backtest
        ${SRC_DIR}/backtest/event_source
)

# zmq 头文件可见性（strategy_engine.h 链需要；与 src/CMakeLists.txt:27-32 的硬编码路径约定一致，未找到则跳过）
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp PATHS /opt/win/cpp_try/include/cppzmq /opt/win/cpp_code/shm_redirect/include/cppzmq)
find_path(ZMQ_HEADER_DIR zmq.h PATHS /opt/win/cpp_try/3rd/libzmq-4.3.5/include)
if (CPPZMQ_INCLUDE_DIR)
    include_directories(SYSTEM ${CPPZMQ_INCLUDE_DIR})
endif ()
if (ZMQ_HEADER_DIR)
    include_directories(SYSTEM ${ZMQ_HEADER_DIR})
endif ()

add_subdirectory(unit)
add_subdirectory(integration)

# 手动 e2e 程序（联网/demo 性质），默认不构建
option(GTRADE_BUILD_E2E "Build manual e2e test programs under test/e2e/" OFF)
if (GTRADE_BUILD_E2E)
    add_subdirectory(e2e)
endif ()
