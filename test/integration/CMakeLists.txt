# 集成测试层（P3 填充真实场景）：当前仅冒烟用例，验证 ctest 标签链路
file(GLOB SRC_LIST CONFIGURE_DEPENDS *.cpp)
add_executable(integration_test ${SRC_LIST})
target_link_libraries(integration_test
        gtest
        gtest_main
)
add_test(NAME integration_smoke COMMAND integration_test)
set_tests_properties(integration_smoke PROPERTIES LABELS "integration")
