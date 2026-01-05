cmake_minimum_required(VERSION 3.16)

# 添加子目录
add_subdirectory(core)

# 创建演示可执行文件
add_executable(demo_example demo_example.cpp)

target_link_libraries(demo_example
    PRIVATE
        mpquic_fec_core
)

target_include_directories(demo_example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# 设置输出目录
set_target_properties(demo_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 创建集成FEC演示可执行文件
add_executable(demo_integrated_fec demo_integrated_fec.cpp)

target_link_libraries(demo_integrated_fec
    PRIVATE
        mpquic_fec_core
)

target_include_directories(demo_integrated_fec
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# 设置输出目录
set_target_properties(demo_integrated_fec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
