get_filename_component(DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

include_directories(
        ${SRC_DIR}/common
        ${SRC_DIR}/utilities
#        ${CMAKE_CURRENT_SOURCE_DIR}/3rd
#        ${CMAKE_CURRENT_SOURCE_DIR}/interface
#        ${CMAKE_CURRENT_SOURCE_DIR}/common
)

file(GLOB SRC_LIST *.cpp)

# Interface library (shared between live and backtest modes)
add_library(${DIR_NAME} ${SRC_LIST})