#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace mpquic_fec {

/**
 * @brief QUIC连接状态
 */
enum class QUICState {
    IDLE,
    CONNECTING,
    CONNECTED,
    CLOSING,
    CLOSED,
    ERROR
};

/**
 * @brief QUIC流ID
 */
using StreamID = uint64_t;

/**
 * @brief QUIC路径ID
 */
using PathID = uint32_t;

/**
 * @brief QUIC路径信息
 */
struct QUICPathInfo {
    PathID path_id;
    std::string local_addr;
    std::string remote_addr;
    uint16_t local_port;
    uint16_t remote_port;
    bool is_active;
    double rtt_ms;
    double loss_rate;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    
    QUICPathInfo() 
        : path_id(0), local_port(0), remote_port(0), 
          is_active(false), rtt_ms(0), loss_rate(0),
          bytes_sent(0), bytes_received(0) {}
};

/**
 * @brief 数据接收回调函数类型
 * @param stream_id 流ID
 * @param data 接收到的数据
 * @param fin 是否为流的最后一块数据
 */
using DataRecvCallback = std::function<void(StreamID stream_id, 
                                            const std::vector<uint8_t>& data, 
                                            bool fin)>;

/**
 * @brief 连接状态变化回调函数类型
 * @param old_state 旧状态
 * @param new_state 新状态
 */
using StateChangeCallback = std::function<void(QUICState old_state, 
                                               QUICState new_state)>;

/**
 * @brief QUIC连接抽象接口
 * 
 * 这是一个抽象层，用于隔离具体的QUIC实现（如liblsquic）
 * 允许使用模拟实现进行开发和测试，后续可替换为真实实现
 */
class IQUICConnection {
public:
    virtual ~IQUICConnection() = default;

    /**
     * @brief 连接到服务器（客户端使用）
     * @param host 服务器地址
     * @param port 服务器端口
     * @return 成功返回true
     */
    virtual bool connect(const std::string& host, uint16_t port) = 0;

    /**
     * @brief 监听连接（服务器使用）
     * @param bind_addr 绑定地址
     * @param port 监听端口
     * @return 成功返回true
     */
    virtual bool listen(const std::string& bind_addr, uint16_t port) = 0;

    /**
     * @brief 创建新的QUIC流
     * @return 流ID
     */
    virtual StreamID create_stream() = 0;

    /**
     * @brief 在指定流上发送数据
     * @param stream_id 流ID
     * @param data 要发送的数据
     * @param fin 是否关闭流（FIN标志）
     * @return 实际发送的字节数
     */
    virtual size_t send(StreamID stream_id, 
                       const std::vector<uint8_t>& data, 
                       bool fin = false) = 0;

    /**
     * @brief 在指定路径上发送数据（多路径支持）
     * @param path_id 路径ID
     * @param stream_id 流ID
     * @param data 要发送的数据
     * @param fin 是否关闭流
     * @return 实际发送的字节数
     */
    virtual size_t send_on_path(PathID path_id,
                                StreamID stream_id,
                                const std::vector<uint8_t>& data,
                                bool fin = false) = 0;

    /**
     * @brief 关闭流
     * @param stream_id 流ID
     */
    virtual void close_stream(StreamID stream_id) = 0;

    /**
     * @brief 关闭连接
     * @param error_code 错误码（0表示正常关闭）
     * @param reason 关闭原因
     */
    virtual void close(uint32_t error_code = 0, 
                      const std::string& reason = "") = 0;

    /**
     * @brief 处理事件（驱动连接状态机）
     * 应该在事件循环中定期调用
     * @param timeout_ms 超时时间（毫秒）
     * @return 处理的事件数
     */
    virtual int process_events(int timeout_ms = 0) = 0;

    /**
     * @brief 添加新的传输路径（多路径QUIC）
     * @param local_addr 本地地址
     * @param local_port 本地端口
     * @param remote_addr 远程地址
     * @param remote_port 远程端口
     * @return 路径ID，失败返回-1
     */
    virtual PathID add_path(const std::string& local_addr,
                           uint16_t local_port,
                           const std::string& remote_addr,
                           uint16_t remote_port) = 0;

    /**
     * @brief 移除路径
     * @param path_id 路径ID
     */
    virtual void remove_path(PathID path_id) = 0;

    /**
     * @brief 获取所有路径信息
     */
    virtual std::vector<QUICPathInfo> get_paths() const = 0;

    /**
     * @brief 获取连接状态
     */
    virtual QUICState get_state() const = 0;

    /**
     * @brief 设置数据接收回调
     */
    virtual void set_data_recv_callback(DataRecvCallback callback) = 0;

    /**
     * @brief 设置状态变化回调
     */
    virtual void set_state_change_callback(StateChangeCallback callback) = 0;

    /**
     * @brief 获取连接统计信息
     */
    virtual std::string get_stats() const = 0;
};

/**
 * @brief 创建QUIC连接的工厂函数
 * @param use_real_impl 是否使用真实的QUIC实现（liblsquic），
 *                      false则使用模拟实现
 */
std::unique_ptr<IQUICConnection> create_quic_connection(bool use_real_impl = false);

} // namespace mpquic_fec
