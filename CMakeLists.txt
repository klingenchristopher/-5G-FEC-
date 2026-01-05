cmake_minimum_required(VERSION 3.16)
project(5G_MPQUIC_FEC_Fusion VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 添加源代码目录
add_subdirectory(src)

# 如果启用测试，添加测试目录
if(ENABLE_TESTS)
  # add_subdirectory(tests)
endif()
