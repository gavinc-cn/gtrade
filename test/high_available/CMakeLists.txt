cmake_minimum_required(VERSION 3.16)
project(ha_demo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${HIREDIS_INCLUDE_DIRS}
)

# 链接目录
link_directories(${HIREDIS_LIBRARY_DIRS})

# HA 核心库
add_library(ha_core STATIC
    src/redis_client.cpp
    src/leader_election.cpp
    src/state_store.cpp
    src/message_broker.cpp
    src/local_state.cpp
    src/ha_node.cpp
)
target_link_libraries(ha_core
    ${HIREDIS_LIBRARIES}
    Threads::Threads
)

# Demo: HA节点
add_executable(ha_node_demo demo/ha_node_demo.cpp)
target_link_libraries(ha_node_demo ha_core)

# Demo: 上游模拟器
add_executable(upstream_sim demo/upstream_sim.cpp)
target_link_libraries(upstream_sim ha_core)

# Demo: 状态查看器
add_executable(state_viewer demo/state_viewer.cpp)
target_link_libraries(state_viewer ha_core)

# 安装
install(TARGETS ha_node_demo upstream_sim state_viewer
    RUNTIME DESTINATION bin
)
