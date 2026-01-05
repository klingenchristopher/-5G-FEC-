#include "fec_encoder.hpp"
#include "path_scheduler.hpp"
#include "buffer_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace mpquic_fec;

/**
 * @brief 演示5G-MPQUIC-FEC系统的基本功能
 */
void demo_fec_encoding() {
    LOG_INFO("========== FEC编码演示 ==========");
    
    // 创建FEC编码器：4个数据块，2个冗余块
    const uint32_t k = 4;
    const uint32_t m = 2;
    const uint32_t block_size = 1024;
    
    FECEncoder encoder(k, m, block_size);
    
    // 准备测试数据
    std::vector<std::vector<uint8_t>> data_blocks;
    for (uint32_t i = 0; i < k; ++i) {
        std::vector<uint8_t> block(block_size);
        // 填充测试数据
        for (uint32_t j = 0; j < block_size; ++j) {
            block[j] = static_cast<uint8_t>((i + 1) * (j + 1) % 256);
        }
        data_blocks.push_back(block);
        LOG_INFO("创建数据块 ", i, ", 大小: ", block.size(), " 字节");
    }
    
    // 进行FEC编码
    auto parity_blocks = encoder.encode(data_blocks);
    LOG_INFO("生成了 ", parity_blocks.size(), " 个冗余块");
    
    // 模拟丢包场景（丢失2个数据块）
    LOG_INFO("模拟丢包：丢失数据块 0 和 2");
    
    FECDecoder decoder(k, m, block_size);
    
    // 准备用于解码的块（保留数据块1,3和冗余块0,1）
    std::vector<std::vector<uint8_t>> received_blocks;
    std::vector<uint32_t> block_ids;
    
    received_blocks.push_back(data_blocks[1]);
    block_ids.push_back(1);
    
    received_blocks.push_back(data_blocks[3]);
    block_ids.push_back(3);
    
    received_blocks.push_back(parity_blocks[0]);
    block_ids.push_back(k + 0);
    
    received_blocks.push_back(parity_blocks[1]);
    block_ids.push_back(k + 1);
    
    // 解码恢复数据
    auto recovered = decoder.decode(received_blocks, block_ids);
    LOG_INFO("成功恢复 ", recovered.size(), " 个数据块");
    
    std::cout << std::endl;
}

void demo_path_scheduling() {
    LOG_INFO("========== 路径调度演示 ==========");
    
    PathScheduler scheduler;
    
    // 模拟3条5G链路
    PathState path1;
    path1.path_id = 0;
    path1.rtt_ms = 20.0;
    path1.loss_rate = 0.01;
    path1.bandwidth_mbps = 100.0;
    
    PathState path2;
    path2.path_id = 1;
    path2.rtt_ms = 50.0;
    path2.loss_rate = 0.05;
    path2.bandwidth_mbps = 50.0;
    
    PathState path3;
    path3.path_id = 2;
    path3.rtt_ms = 100.0;
    path3.loss_rate = 0.15;
    path3.bandwidth_mbps = 20.0;
    
    scheduler.update_path_state(path1);
    scheduler.update_path_state(path2);
    scheduler.update_path_state(path3);
    
    // 显示初始权重
    auto weights = scheduler.get_path_weights();
    LOG_INFO("初始路径权重：");
    for (const auto& [path_id, weight] : weights) {
        LOG_INFO("  路径 ", path_id, ": ", weight * 100, "%");
    }
    
    // 模拟发送100个数据包
    LOG_INFO("\n模拟发送100个数据包...");
    std::map<uint32_t, uint32_t> packet_counts;
    
    for (int i = 0; i < 100; ++i) {
        uint32_t selected_path = scheduler.select_path(1400);
        packet_counts[selected_path]++;
    }
    
    LOG_INFO("\n数据包分配结果：");
    for (const auto& [path_id, count] : packet_counts) {
        LOG_INFO("  路径 ", path_id, ": ", count, " 个数据包 (", count, "%)");
    }
    
    // 模拟路径状态变化
    LOG_INFO("\n模拟路径1质量下降...");
    path1.loss_rate = 0.20;
    path1.rtt_ms = 80.0;
    scheduler.update_path_state(path1);
    
    weights = scheduler.get_path_weights();
    LOG_INFO("更新后的路径权重：");
    for (const auto& [path_id, weight] : weights) {
        LOG_INFO("  路径 ", path_id, ": ", weight * 100, "%");
    }
    
    std::cout << std::endl;
}

void demo_buffer_management() {
    LOG_INFO("========== 缓冲区管理演示 ==========");
    
    auto& pool = BufferPool::instance();
    
    // 获取缓冲区
    auto buffer1 = pool.acquire(4096);
    LOG_INFO("获取缓冲区1: 容量=", buffer1.capacity(), " 字节");
    
    // 写入数据
    std::vector<uint8_t> test_data(1024, 0xAB);
    buffer1.write(test_data.data(), test_data.size());
    LOG_INFO("写入 ", buffer1.size(), " 字节数据");
    
    // 移动语义（零拷贝）
    auto buffer2 = std::move(buffer1);
    LOG_INFO("移动缓冲区（零拷贝）: 容量=", buffer2.capacity(), " 字节");
    
    // 归还缓冲区
    pool.release(std::move(buffer2));
    LOG_INFO("缓冲区已归还到池中");
    
    std::cout << std::endl;
}

void demo_integrated_scenario() {
    LOG_INFO("========== 综合场景演示 ==========");
    LOG_INFO("模拟5G多路径传输场景，使用FEC保护数据");
    
    // 初始化组件
    FECEncoder encoder(8, 4, 1024);  // 8个数据块，4个冗余块
    PathScheduler scheduler;
    
    // 配置3条路径（模拟不同的5G基站/链路）
    PathState path1;
    path1.path_id = 0;
    path1.rtt_ms = 15.0;
    path1.loss_rate = 0.01;
    path1.bandwidth_mbps = 150.0;
    
    PathState path2;
    path2.path_id = 1;
    path2.rtt_ms = 40.0;
    path2.loss_rate = 0.08;
    path2.bandwidth_mbps = 80.0;
    
    PathState path3;
    path3.path_id = 2;
    path3.rtt_ms = 90.0;
    path3.loss_rate = 0.20;
    path3.bandwidth_mbps = 30.0;
    
    scheduler.update_path_state(path1);
    scheduler.update_path_state(path2);
    scheduler.update_path_state(path3);
    
    // 准备要传输的数据
    std::vector<std::vector<uint8_t>> data_blocks;
    for (uint32_t i = 0; i < 8; ++i) {
        std::vector<uint8_t> block(1024);
        for (uint32_t j = 0; j < 1024; ++j) {
            block[j] = static_cast<uint8_t>((i * 100 + j) % 256);
        }
        data_blocks.push_back(block);
    }
    
    LOG_INFO("准备传输 8 个数据块，每块 1024 字节");
    
    // FEC编码
    auto parity_blocks = encoder.encode(data_blocks);
    LOG_INFO("生成 ", parity_blocks.size(), " 个FEC冗余块");
    
    // 模拟多路径传输
    LOG_INFO("\n开始多路径传输...");
    std::map<uint32_t, std::vector<uint32_t>> path_assignments;
    
    // 为每个块选择路径
    for (uint32_t i = 0; i < 12; ++i) {  // 8个数据块 + 4个冗余块
        uint32_t path = scheduler.select_path(1024);
        path_assignments[path].push_back(i);
    }
    
    LOG_INFO("\n块分配结果：");
    for (const auto& [path_id, blocks] : path_assignments) {
        LOG_INFO("  路径 ", path_id, ": ", blocks.size(), " 个块");
    }
    
    // 模拟网络丢包
    LOG_INFO("\n模拟路径3发生严重丢包（丢失所有数据）...");
    auto lost_blocks = path_assignments[2].size();
    
    // 计算接收到的块数
    uint32_t received = 12 - lost_blocks;
    LOG_INFO("接收到 ", received, " / 12 个块");
    
    if (received >= 8) {
        LOG_INFO("✓ FEC保护成功！虽然丢失 ", lost_blocks, " 个块，但可以完整恢复数据");
    } else {
        LOG_WARN("✗ 数据丢失过多，无法完全恢复");
    }
    
    std::cout << std::endl;
}

int main() {
    // 设置日志级别
    Logger::instance().set_level(LogLevel::INFO);
    
    LOG_INFO("=================================================");
    LOG_INFO("  5G-MPQUIC-FEC-Fusion 系统演示");
    LOG_INFO("  多路径QUIC + 前向纠错码 (FEC)");
    LOG_INFO("=================================================\n");
    
    // 运行各个演示
    demo_fec_encoding();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    demo_path_scheduling();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    demo_buffer_management();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    demo_integrated_scenario();
    
    LOG_INFO("=================================================");
    LOG_INFO("  演示完成！");
    LOG_INFO("=================================================");
    
    return 0;
}
