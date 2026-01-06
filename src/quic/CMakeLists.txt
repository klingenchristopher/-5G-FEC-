cmake_minimum_required(VERSION 3.16)

# QUIC集成层库
add_library(mpquic_integration
    mock_quic_connection.cpp
    mpquic_manager.cpp
)

target_include_directories(mpquic_integration
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(mpquic_integration
    PUBLIC
        mpquic_fec_core
)

# C++17标准
target_compile_features(mpquic_integration PUBLIC cxx_std_17)

# 编译选项
target_compile_options(mpquic_integration PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# 如果找到liblsquic，可以添加真实实现
# find_package(lsquic)
# if(lsquic_FOUND)
#     target_sources(mpquic_integration PRIVATE
#         lsquic_connection.cpp  # 真实实现
#     )
#     target_link_libraries(mpquic_integration PRIVATE lsquic::lsquic)
#     target_compile_definitions(mpquic_integration PRIVATE USE_REAL_LSQUIC)
# endif()
