#pragma once

#include "quic_connection.hpp"
#include "path_scheduler.hpp"
#include "fec_encoder.hpp"
#include <memory>
#include <map>
#include <vector>

namespace mpquic_fec {

/**
 * @brief 多路径QUIC管理器
 * 
 * 整合QUIC连接、路径调度和FEC编码，提供统一的多路径传输接口
 */
class MPQUICManager {
public:
    /**
     * @brief 构造函数
     * @param use_real_quic 是否使用真实QUIC实现
     */
    explicit MPQUICManager(bool use_real_quic = false);
    
    ~MPQUICManager() = default;

    /**
     * @brief 初始化客户端连接
     */
    bool connect_as_client(const std::string& host, uint16_t port);

    /**
     * @brief 初始化服务器
     */
    bool start_as_server(const std::string& bind_addr, uint16_t port);

    /**
     * @brief 添加额外的传输路径
     * @param local_addr 本地地址
     * @param local_port 本地端口  
     * @param remote_addr 远程地址
     * @param remote_port 远程端口
     * @return 路径ID
     */
    PathID add_path(const std::string& local_addr, uint16_t local_port,
                   const std::string& remote_addr, uint16_t remote_port);

    /**
     * @brief 发送数据（自动选择最优路径）
     * @param data 要发送的数据
     * @param use_fec 是否使用FEC保护
     * @return 成功返回true
     */
    bool send_data(const std::vector<uint8_t>& data, bool use_fec = true);

    /**
     * @brief 在指定路径上发送数据
     */
    bool send_data_on_path(PathID path_id, const std::vector<uint8_t>& data);

    /**
     * @brief 配置FEC参数
     * @param k 数据块数量
     * @param m 冗余块数量
     * @param block_size 块大小
     */
    void configure_fec(uint32_t k, uint32_t m, uint32_t block_size);

    /**
     * @brief 启用/禁用FEC
     */
    void enable_fec(bool enable);

    /**
     * @brief 更新路径状态（供调度器使用）
     */
    void update_path_metrics();

    /**
     * @brief 设置数据接收回调
     */
    void set_data_received_callback(
        std::function<void(const std::vector<uint8_t>&)> callback);

    /**
     * @brief 获取连接统计信息
     */
    std::string get_statistics() const;

    /**
     * @brief 关闭连接
     */
    void close();

    /**
     * @brief 处理事件（需要在主循环中调用）
     */
    void process_events(int timeout_ms = 10);

private:
    /**
     * @brief 使用FEC编码并发送数据
     */
    bool send_with_fec(const std::vector<uint8_t>& data);

    /**
     * @brief 直接发送数据（不使用FEC）
     */
    bool send_without_fec(const std::vector<uint8_t>& data);

    /**
     * @brief 处理接收到的数据
     */
    void handle_received_data(StreamID stream_id, 
                            const std::vector<uint8_t>& data,
                            bool fin);

    /**
     * @brief 从路径信息更新调度器状态
     */
    void sync_path_states();

    std::unique_ptr<IQUICConnection> quic_conn_;
    std::unique_ptr<PathScheduler> scheduler_;
    std::unique_ptr<FECEncoder> fec_encoder_;
    std::unique_ptr<FECDecoder> fec_decoder_;

    StreamID data_stream_;
    bool fec_enabled_;
    
    // FEC配置
    uint32_t fec_k_;
    uint32_t fec_m_;
    uint32_t fec_block_size_;

    // 接收缓冲区
    std::map<uint32_t, std::vector<uint8_t>> recv_buffer_;
    std::function<void(const std::vector<uint8_t>&)> data_received_callback_;
    
    // 统计信息
    uint64_t total_bytes_sent_;
    uint64_t total_bytes_received_;
    uint64_t fec_blocks_sent_;
    uint64_t fec_blocks_recovered_;
};

} // namespace mpquic_fec
