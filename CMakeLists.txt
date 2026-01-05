cmake_minimum_required(VERSION 3.16)
project(5G_MPQUIC_FEC_Fusion VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# TODO: add subdirectories when sources are ready
# add_subdirectory(src)
# if(ENABLE_TESTS)
#   add_subdirectory(tests)
# endif
