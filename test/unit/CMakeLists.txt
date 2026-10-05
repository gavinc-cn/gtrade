# 单元测试层：单一 unit_test 二进制，gtest_discover_tests 按用例注册进 ctest（标签 unit）
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

# CONFIGURE_DEPENDS：新增/删除测试文件后构建时自动重配，无需手动重跑 cmake
file(GLOB SRC_LIST CONFIGURE_DEPENDS
        *.cpp
        zrtools/*.cpp
        fakes/*.cpp
        stubs/*.cpp
)

# 需要直接测试的生产源文件（这些模块没有独立库目标，随 unit_test 一起编译）
# 后续迁移任务逐个追加
set(GTRADE_PROD_SRC
        ${SRC_DIR}/strategy_engine/order_manager.cpp
        ${SRC_DIR}/websocket/dummy_orderbook.cpp
        ${SRC_DIR}/strategy_engine/timer_manager_dummy.cpp
        ${SRC_DIR}/strategy_engine/instrument_scope.cpp
)

add_executable(unit_test ${SRC_LIST} ${GTRADE_PROD_SRC})
target_link_libraries(unit_test
        ssl
        crypto
        boost_system
        utilities
        interface
        common
        yaml-cpp
        gtest
        gtest_main
        gmock
        gmock_main
        backtrace
        boost_stacktrace_backtrace
)

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST：ctest 运行时才枚举用例（需 CMake >= 3.18，本机 3.28）
# XML_OUTPUT_DIR：每个用例输出 JUnit XML 到 <build>/test_reports/unit/
gtest_discover_tests(unit_test
        PROPERTIES LABELS "unit"
        DISCOVERY_MODE PRE_TEST
        XML_OUTPUT_DIR ${CMAKE_BINARY_DIR}/test_reports/unit
)
