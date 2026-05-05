# PCH library (shared between live and backtest modes)
add_library(pch OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp)
target_precompile_headers(pch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)
