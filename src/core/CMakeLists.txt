cmake_minimum_required(VERSION 3.16)

# 添加核心库
add_library(mpquic_fec_core
    fec/fec_encoder.cpp
    fec/fec_frame.cpp
    fec/packet_hook.cpp
    scheduler/path_scheduler.cpp
    scheduler/oco_controller.cpp
    mpquic_fec_controller.cpp
    ../common/buffer_manager.cpp
)

target_include_directories(mpquic_fec_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# C++17标准
target_compile_features(mpquic_fec_core PUBLIC cxx_std_17)

# 编译选项
target_compile_options(mpquic_fec_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
