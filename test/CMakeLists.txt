get_filename_component(DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

find_package(Boost REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

include_directories(
        ${SRC_DIR}/pch
        ${SRC_DIR}/utilities
        ${SRC_DIR}/3rd
        ${SRC_DIR}/interface
        ${SRC_DIR}/common
        ${SRC_DIR}/websocket
        ${SRC_DIR}/strategy_engine
)

# OKX WebSocket test target (simple standalone test)
add_executable(okx_ws_test okx_ws_test.cpp)
target_link_libraries(okx_ws_test
    pthread
    ssl
    crypto
    boost_system
)

# Other simple test files
file(GLOB SRC_LIST *.cpp)
list(REMOVE_ITEM SRC_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/okx_ws_test.cpp
)
foreach(cpp_file ${SRC_LIST})
    get_filename_component(target_name ${cpp_file} NAME_WLE)
    message("build ${target_name}: ${cpp_file}")
    add_executable(${target_name} ${cpp_file})
#    target_precompile_headers(${target_name} REUSE_FROM headers)
    target_link_libraries(${target_name}
            pthread
    )
endforeach()

add_subdirectory(unit_test)