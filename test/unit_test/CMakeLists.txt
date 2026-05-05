get_filename_component(DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

include_directories()
link_directories()

file(GLOB_RECURSE SRC_LIST *.cpp)
#list(REMOVE_ITEM SRC_LIST
#         ${CMAKE_CURRENT_SOURCE_DIR}/try_ultraq.cpp
#)
file(GLOB EXTRA_SRC )
message("EXTRA_SRC: ${EXTRA_SRC}")
enable_testing()
#foreach(cpp_file ${SRC_LIST})
#        get_filename_component(target_name ${cpp_file} NAME_WE)
#        message("build ${target_name}: ${cpp_file}")
#        add_executable(${target_name} ${cpp_file} ${EXTRA_SRC})
##        target_precompile_headers(${target_name} REUSE_FROM pch)
#        target_link_libraries(${target_name}
#                pthread
#                stdc++fs
#                rt
#                zrtools
#                gtest
#                gtest_main
#                gmock
#                gmock_main
#        )
#        add_test(NAME ${target_name} COMMAND ${target_name})
#endforeach()

file(GLOB SRC_LIST
        *.cpp
        zrtools/*.cpp
)
set(unit_test_name unit_test)
message("build ${unit_test_name}: ${SRC_LIST}")
add_executable(${unit_test_name} ${SRC_LIST})
target_link_libraries(${unit_test_name}
        ssl
        crypto
        boost_system
        utilities
        yaml-cpp
        gtest
        gtest_main
        gmock
        gmock_main
        backtrace
        boost_stacktrace_backtrace
)
set_target_properties(${unit_test_name} PROPERTIES VERSION ${TARGET_VERSION})