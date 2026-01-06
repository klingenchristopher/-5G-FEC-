#include "mpquic_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace mpquic_fec;

/**
 * @brief 真实的多路径QUIC+FEC传输演示
 * 
 * 演示完整的端到端传输场景：
 * 1. 建立QUIC连接
 * 2. 添加多条传输路径（模拟5G多链路）
 * 3. 使用FEC保护进行数据传输
 * 4. 展示路径调度和冗余恢复
 */

void run_client_demo() {
    LOG_INFO("========== MPQUIC Client Demo ==========");
    
    // 创建MPQUIC管理器（使用模拟QUIC）
    MPQUICManager manager(false);
    
    // 连接到服务器
    if (!manager.connect_as_client("127.0.0.1", 4433)) {
        LOG_ERROR("Failed to connect to server");
        return;
    }
    
    LOG_INFO("Connected to server");
    
    // 添加额外的传输路径（模拟5G多基站）
    LOG_INFO("\n添加多条5G传输路径...");
    
    PathID path2 = manager.add_path("0.0.0.0", 12346, "127.0.0.1", 4434);
    PathID path3 = manager.add_path("0.0.0.0", 12347, "127.0.0.1", 4435);
    
    if (path2 != static_cast<PathID>(-1)) {
        LOG_INFO("Added path 2 (模拟5G链路2)");
    }
    if (path3 != static_cast<PathID>(-1)) {
        LOG_INFO("Added path 3 (模拟5G链路3)");
    }
    
    // 配置FEC参数
    manager.configure_fec(8, 4, 1024);  // 8个数据块，4个冗余块
    manager.enable_fec(true);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 准备测试数据
    LOG_INFO("\n准备传输数据...");
    std::string test_message = "Hello from MPQUIC+FEC! This is a test message "
                               "demonstrating multipath QUIC with FEC protection. "
                               "The data will be split across multiple 5G links "
                               "and protected with Reed-Solomon forward error correction.";
    
    std::vector<uint8_t> data(test_message.begin(), test_message.end());
    
    LOG_INFO("Message size: ", data.size(), " bytes");
    LOG_INFO("Message content: \"", test_message, "\"");
    
    // 发送数据（使用FEC保护）
    LOG_INFO("\n开始多路径传输（使用FEC保护）...");
    
    if (manager.send_data(data, true)) {
        LOG_INFO("✓ Data sent successfully with FEC protection");
    } else {
        LOG_ERROR("✗ Failed to send data");
    }
    
    // 等待传输完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 更新路径指标
    manager.update_path_metrics();
    
    // 显示统计信息
    LOG_INFO("\n", manager.get_statistics());
    
    // 关闭连接
    manager.close();
    
    LOG_INFO("\n========== Client Demo Completed ==========");
}

void run_server_demo() {
    LOG_INFO("========== MPQUIC Server Demo ==========");
    
    // 创建MPQUIC管理器
    MPQUICManager manager(false);
    
    // 启动服务器
    if (!manager.start_as_server("0.0.0.0", 4433)) {
        LOG_ERROR("Failed to start server");
        return;
    }
    
    LOG_INFO("Server listening on 0.0.0.0:4433");
    
    // 设置数据接收回调
    manager.set_data_received_callback([](const std::vector<uint8_t>& data) {
        std::string message(data.begin(), data.end());
        LOG_INFO("Received message: \"", message, "\"");
    });
    
    // 运行事件循环
    LOG_INFO("Server running, waiting for connections...\n");
    
    for (int i = 0; i < 100; ++i) {
        manager.process_events(100);
    }
    
    // 显示统计信息
    LOG_INFO("\n", manager.get_statistics());
    
    manager.close();
    
    LOG_INFO("\n========== Server Demo Completed ==========");
}

void run_integrated_demo() {
    LOG_INFO("=================================================");
    LOG_INFO("  真实多路径QUIC + FEC传输演示");
    LOG_INFO("  Multi-Path QUIC with FEC Protection");
    LOG_INFO("=================================================\n");
    
    // 模拟客户端-服务器通信
    LOG_INFO("演示场景：5G网络中的视频流传输");
    LOG_INFO("- 3条5G链路（不同基站）");
    LOG_INFO("- FEC保护（8+4 Reed-Solomon）");
    LOG_INFO("- 智能路径调度（OCO算法）\n");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动服务器（在后台线程）
    std::thread server_thread([]() {
        MPQUICManager server(false);
        server.start_as_server("0.0.0.0", 4433);
        
        server.set_data_received_callback([](const std::vector<uint8_t>& data) {
            LOG_INFO("[Server] Received ", data.size(), " bytes");
        });
        
        for (int i = 0; i < 50; ++i) {
            server.process_events(50);
        }
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 运行客户端
    run_client_demo();
    
    // 等待服务器线程结束
    if (server_thread.joinable()) {
        server_thread.join();
    }
    
    LOG_INFO("\n=================================================");
    LOG_INFO("  演示完成！");
    LOG_INFO("=================================================");
}

int main(int argc, char* argv[]) {
    // 设置日志级别
    Logger::instance().set_level(LogLevel::INFO);
    
    std::string mode = "integrated";
    
    if (argc > 1) {
        mode = argv[1];
    }
    
    if (mode == "client") {
        run_client_demo();
    } else if (mode == "server") {
        run_server_demo();
    } else {
        run_integrated_demo();
    }
    
    return 0;
}
