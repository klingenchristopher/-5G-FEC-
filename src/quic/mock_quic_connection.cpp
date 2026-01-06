#include "quic_connection.hpp"
#include "logger.hpp"
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <mutex>
#include <map>

namespace mpquic_fec {

/**
 * @brief 模拟QUIC连接实现
 * 
 * 用于在没有真实liblsquic库的情况下进行开发和测试
 * 模拟多路径、丢包、延迟等网络特性
 */
class MockQUICConnection : public IQUICConnection {
private:
    mutable std::mutex mutex_;
    QUICState state_;
    StreamID next_stream_id_;
    PathID next_path_id_;
    
    std::map<PathID, QUICPathInfo> paths_;
    
    DataRecvCallback data_recv_callback_;
    StateChangeCallback state_change_callback_;
    
    void change_state(QUICState new_state) {
        QUICState old_state = state_;
        state_ = new_state;
        
        if (state_change_callback_) {
            state_change_callback_(old_state, new_state);
        }
    }

public:
    MockQUICConnection() 
        : state_(QUICState::IDLE),
          next_stream_id_(0),
          next_path_id_(0) {
        LOG_INFO("MockQUICConnection created (simulated QUIC)");
    }

    ~MockQUICConnection() override {
        if (state_ != QUICState::CLOSED) {
            close(0, "");
        }
    }

    bool connect(const std::string& host, uint16_t port) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ != QUICState::IDLE) {
            LOG_ERROR("Cannot connect: connection not in IDLE state");
            return false;
        }

        LOG_INFO("Connecting to ", host, ":", port, " (simulated)");
        
        change_state(QUICState::CONNECTING);
        
        // 模拟连接延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 创建默认路径
        QUICPathInfo path;
        path.path_id = next_path_id_++;
        path.local_addr = "0.0.0.0";
        path.local_port = 12345;
        path.remote_addr = host;
        path.remote_port = port;
        path.is_active = true;
        path.rtt_ms = 20.0;
        path.loss_rate = 0.01;
        
        paths_[path.path_id] = path;
        
        change_state(QUICState::CONNECTED);
        LOG_INFO("Connected successfully (simulated), path_id=", path.path_id);
        
        return true;
    }

    bool listen(const std::string& bind_addr, uint16_t port) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ != QUICState::IDLE) {
            LOG_ERROR("Cannot listen: connection not in IDLE state");
            return false;
        }

        LOG_INFO("Listening on ", bind_addr, ":", port, " (simulated)");
        
        change_state(QUICState::CONNECTED);
        
        return true;
    }

    StreamID create_stream() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ != QUICState::CONNECTED) {
            throw std::runtime_error("Cannot create stream: not connected");
        }

        StreamID stream_id = next_stream_id_++;
        LOG_DEBUG("Created stream ", stream_id, " (simulated)");
        
        return stream_id;
    }

    size_t send(StreamID stream_id, 
                const std::vector<uint8_t>& data, 
                bool fin) override {
        // 使用第一条可用路径
        for (const auto& [path_id, _] : paths_) {
            return send_on_path(path_id, stream_id, data, fin);
        }
        
        LOG_ERROR("No available paths for sending");
        return 0;
    }

    size_t send_on_path(PathID path_id,
                        StreamID stream_id,
                        const std::vector<uint8_t>& data,
                        bool fin) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ != QUICState::CONNECTED) {
            LOG_ERROR("Cannot send: not connected");
            return 0;
        }

        auto it = paths_.find(path_id);
        if (it == paths_.end()) {
            LOG_ERROR("Path ", path_id, " not found");
            return 0;
        }

        // 模拟网络传输
        auto& path = it->second;
        
        // 模拟丢包
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        
        if (dis(gen) < path.loss_rate) {
            LOG_WARN("Packet dropped on path ", path_id, " (simulated loss)");
            return 0;  // 模拟丢包
        }

        // 模拟RTT延迟
        int delay_ms = static_cast<int>(path.rtt_ms / 2);
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        // 更新统计
        path.bytes_sent += data.size();
        
        LOG_DEBUG("Sent ", data.size(), " bytes on stream ", stream_id, 
                 " path ", path_id, " (simulated)");
        
        // 模拟对端接收（触发回调）
        if (data_recv_callback_) {
            // 在真实实现中，这会在接收端触发
            // 这里为了演示，延迟后触发
            std::thread([this, stream_id, data, fin]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (data_recv_callback_) {
                    data_recv_callback_(stream_id, data, fin);
                }
            }).detach();
        }
        
        return data.size();
    }

    void close_stream(StreamID stream_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG_DEBUG("Closed stream ", stream_id, " (simulated)");
    }

    void close(uint32_t error_code, const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == QUICState::CLOSED) {
            return;
        }

        LOG_INFO("Closing connection: error_code=", error_code, 
                ", reason=", reason, " (simulated)");
        
        change_state(QUICState::CLOSING);
        
        // 清理资源
        paths_.clear();
        
        change_state(QUICState::CLOSED);
    }

    int process_events(int timeout_ms) override {
        // 在模拟实现中，事件处理是自动的
        // 真实实现中，这里会调用liblsquic的事件处理函数
        
        if (timeout_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        }
        
        return 0;  // 返回处理的事件数
    }

    PathID add_path(const std::string& local_addr,
                   uint16_t local_port,
                   const std::string& remote_addr,
                   uint16_t remote_port) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ != QUICState::CONNECTED) {
            LOG_ERROR("Cannot add path: not connected");
            return static_cast<PathID>(-1);
        }

        QUICPathInfo path;
        path.path_id = next_path_id_++;
        path.local_addr = local_addr;
        path.local_port = local_port;
        path.remote_addr = remote_addr;
        path.remote_port = remote_port;
        path.is_active = true;
        
        // 模拟不同路径的网络特性
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> rtt_dis(10.0, 100.0);
        std::uniform_real_distribution<> loss_dis(0.0, 0.2);
        
        path.rtt_ms = rtt_dis(gen);
        path.loss_rate = loss_dis(gen);
        
        paths_[path.path_id] = path;
        
        LOG_INFO("Added path ", path.path_id, ": ", local_addr, ":", local_port,
                " -> ", remote_addr, ":", remote_port, " (RTT=", path.rtt_ms, 
                "ms, Loss=", path.loss_rate * 100, "%)");
        
        return path.path_id;
    }

    void remove_path(PathID path_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = paths_.find(path_id);
        if (it != paths_.end()) {
            paths_.erase(it);
            LOG_INFO("Removed path ", path_id);
        }
    }

    std::vector<QUICPathInfo> get_paths() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<QUICPathInfo> result;
        for (const auto& [_, path] : paths_) {
            result.push_back(path);
        }
        return result;
    }

    QUICState get_state() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void set_data_recv_callback(DataRecvCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        data_recv_callback_ = callback;
    }

    void set_state_change_callback(StateChangeCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        state_change_callback_ = callback;
    }

    std::string get_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ostringstream oss;
        oss << "MockQUIC Connection Stats:\n";
        oss << "  State: " << static_cast<int>(state_) << "\n";
        oss << "  Paths: " << paths_.size() << "\n";
        
        for (const auto& [path_id, path] : paths_) {
            oss << "    Path " << path_id << ": "
                << "sent=" << path.bytes_sent << " bytes, "
                << "recv=" << path.bytes_received << " bytes, "
                << "RTT=" << path.rtt_ms << "ms, "
                << "Loss=" << (path.loss_rate * 100) << "%\n";
        }
        
        return oss.str();
    }
};

// 工厂函数实现
std::unique_ptr<IQUICConnection> create_quic_connection(bool use_real_impl) {
    if (use_real_impl) {
        // TODO: 当liblsquic集成完成后，返回真实实现
        LOG_WARN("Real QUIC implementation not yet available, using mock");
        return std::make_unique<MockQUICConnection>();
    }
    
    return std::make_unique<MockQUICConnection>();
}

} // namespace mpquic_fec
