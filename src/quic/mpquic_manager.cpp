#include "mpquic_manager.hpp"
#include "logger.hpp"
#include <algorithm>

namespace mpquic_fec {

MPQUICManager::MPQUICManager(bool use_real_quic)
    : quic_conn_(create_quic_connection(use_real_quic)),
      scheduler_(std::make_unique<PathScheduler>()),
      data_stream_(0),
      fec_enabled_(true),
      fec_k_(4),
      fec_m_(2),
      fec_block_size_(1024),
      total_bytes_sent_(0),
      total_bytes_received_(0),
      fec_blocks_sent_(0),
      fec_blocks_recovered_(0) {
    
    // 初始化FEC编码器/解码器
    fec_encoder_ = std::make_unique<FECEncoder>(fec_k_, fec_m_, fec_block_size_);
    fec_decoder_ = std::make_unique<FECDecoder>(fec_k_, fec_m_, fec_block_size_);
    
    // 设置QUIC回调
    quic_conn_->set_data_recv_callback(
        [this](StreamID stream_id, const std::vector<uint8_t>& data, bool fin) {
            handle_received_data(stream_id, data, fin);
        }
    );
    
    LOG_INFO("MPQUICManager initialized with FEC(k=", fec_k_, ", m=", fec_m_, ")");
}

bool MPQUICManager::connect_as_client(const std::string& host, uint16_t port) {
    LOG_INFO("Connecting to ", host, ":", port);
    
    if (!quic_conn_->connect(host, port)) {
        LOG_ERROR("Failed to connect");
        return false;
    }

    // 创建数据流
    data_stream_ = quic_conn_->create_stream();
    
    // 同步路径状态到调度器
    sync_path_states();
    
    LOG_INFO("Connected successfully, stream_id=", data_stream_);
    return true;
}

bool MPQUICManager::start_as_server(const std::string& bind_addr, uint16_t port) {
    LOG_INFO("Starting server on ", bind_addr, ":", port);
    
    if (!quic_conn_->listen(bind_addr, port)) {
        LOG_ERROR("Failed to start server");
        return false;
    }

    LOG_INFO("Server started successfully");
    return true;
}

PathID MPQUICManager::add_path(const std::string& local_addr, uint16_t local_port,
                              const std::string& remote_addr, uint16_t remote_port) {
    PathID path_id = quic_conn_->add_path(local_addr, local_port, 
                                          remote_addr, remote_port);
    
    if (path_id == static_cast<PathID>(-1)) {
        LOG_ERROR("Failed to add path");
        return path_id;
    }

    // 更新调度器
    sync_path_states();
    
    LOG_INFO("Added path ", path_id, ": ", local_addr, ":", local_port,
            " -> ", remote_addr, ":", remote_port);
    
    return path_id;
}

bool MPQUICManager::send_data(const std::vector<uint8_t>& data, bool use_fec) {
    if (data.empty()) {
        LOG_WARN("Attempted to send empty data");
        return false;
    }

    if (use_fec && fec_enabled_) {
        return send_with_fec(data);
    } else {
        return send_without_fec(data);
    }
}

bool MPQUICManager::send_data_on_path(PathID path_id, 
                                      const std::vector<uint8_t>& data) {
    size_t sent = quic_conn_->send_on_path(path_id, data_stream_, data, false);
    
    if (sent > 0) {
        total_bytes_sent_ += sent;
        LOG_DEBUG("Sent ", sent, " bytes on path ", path_id);
        return true;
    }
    
    return false;
}

void MPQUICManager::configure_fec(uint32_t k, uint32_t m, uint32_t block_size) {
    fec_k_ = k;
    fec_m_ = m;
    fec_block_size_ = block_size;
    
    fec_encoder_ = std::make_unique<FECEncoder>(k, m, block_size);
    fec_decoder_ = std::make_unique<FECDecoder>(k, m, block_size);
    
    LOG_INFO("FEC reconfigured: k=", k, ", m=", m, ", block_size=", block_size);
}

void MPQUICManager::enable_fec(bool enable) {
    fec_enabled_ = enable;
    LOG_INFO("FEC ", (enable ? "enabled" : "disabled"));
}

void MPQUICManager::update_path_metrics() {
    sync_path_states();
}

void MPQUICManager::set_data_received_callback(
    std::function<void(const std::vector<uint8_t>&)> callback) {
    data_received_callback_ = callback;
}

std::string MPQUICManager::get_statistics() const {
    std::ostringstream oss;
    oss << "=== MPQUIC Manager Statistics ===\n";
    oss << "Total bytes sent: " << total_bytes_sent_ << "\n";
    oss << "Total bytes received: " << total_bytes_received_ << "\n";
    oss << "FEC blocks sent: " << fec_blocks_sent_ << "\n";
    oss << "FEC blocks recovered: " << fec_blocks_recovered_ << "\n";
    oss << "FEC enabled: " << (fec_enabled_ ? "Yes" : "No") << "\n";
    oss << "\n" << quic_conn_->get_stats();
    
    return oss.str();
}

void MPQUICManager::close() {
    LOG_INFO("Closing MPQUIC connection");
    quic_conn_->close();
}

void MPQUICManager::process_events(int timeout_ms) {
    quic_conn_->process_events(timeout_ms);
    
    // 定期更新路径指标
    static int update_counter = 0;
    if (++update_counter >= 10) {  // 每10次更新一次
        update_path_metrics();
        update_counter = 0;
    }
}

bool MPQUICManager::send_with_fec(const std::vector<uint8_t>& data) {
    LOG_DEBUG("Sending ", data.size(), " bytes with FEC protection");
    
    // 将数据分块
    std::vector<std::vector<uint8_t>> data_blocks;
    size_t offset = 0;
    
    while (offset < data.size()) {
        size_t chunk_size = std::min(fec_block_size_, 
                                    static_cast<uint32_t>(data.size() - offset));
        std::vector<uint8_t> block(data.begin() + offset, 
                                  data.begin() + offset + chunk_size);
        
        // 如果块不足大小，填充零
        if (block.size() < fec_block_size_) {
            block.resize(fec_block_size_, 0);
        }
        
        data_blocks.push_back(block);
        offset += chunk_size;
        
        if (data_blocks.size() >= fec_k_) {
            break;  // 凑够一组进行编码
        }
    }
    
    // 如果数据块不足k个，填充空块
    while (data_blocks.size() < fec_k_) {
        data_blocks.push_back(std::vector<uint8_t>(fec_block_size_, 0));
    }
    
    // FEC编码
    auto parity_blocks = fec_encoder_->encode(data_blocks);
    LOG_DEBUG("Generated ", parity_blocks.size(), " FEC parity blocks");
    
    // 发送数据块（选择最优路径）
    for (size_t i = 0; i < data_blocks.size(); ++i) {
        PathID path_id = scheduler_->select_source_path(data_blocks[i].size());
        
        if (!send_data_on_path(path_id, data_blocks[i])) {
            LOG_ERROR("Failed to send data block ", i);
            return false;
        }
    }
    
    // 发送冗余块（选择不同的路径以提高可靠性）
    for (size_t i = 0; i < parity_blocks.size(); ++i) {
        PathID source_path = scheduler_->select_source_path(parity_blocks[i].size());
        PathID repair_path = scheduler_->select_repair_path(source_path, 
                                                            parity_blocks[i].size());
        
        if (!send_data_on_path(repair_path, parity_blocks[i])) {
            LOG_WARN("Failed to send parity block ", i);
            // 冗余块发送失败不算致命错误
        } else {
            fec_blocks_sent_++;
        }
    }
    
    LOG_INFO("Sent data with FEC: ", data_blocks.size(), " data blocks + ",
            parity_blocks.size(), " parity blocks");
    
    return true;
}

bool MPQUICManager::send_without_fec(const std::vector<uint8_t>& data) {
    LOG_DEBUG("Sending ", data.size(), " bytes without FEC");
    
    // 选择最优路径
    PathID path_id = scheduler_->select_path(data.size());
    
    return send_data_on_path(path_id, data);
}

void MPQUICManager::handle_received_data(StreamID stream_id,
                                        const std::vector<uint8_t>& data,
                                        bool fin) {
    total_bytes_received_ += data.size();
    
    LOG_DEBUG("Received ", data.size(), " bytes on stream ", stream_id,
             (fin ? " (FIN)" : ""));
    
    // 如果启用了FEC，这里应该进行FEC解码
    // 为简化演示，直接传递数据
    if (data_received_callback_) {
        data_received_callback_(data);
    }
}

void MPQUICManager::sync_path_states() {
    auto paths = quic_conn_->get_paths();
    
    for (const auto& path_info : paths) {
        PathState state;
        state.path_id = path_info.path_id;
        state.rtt_ms = path_info.rtt_ms;
        state.loss_rate = path_info.loss_rate;
        state.bandwidth_mbps = 100.0;  // 默认值，实际应从QUIC获取
        state.bytes_sent = path_info.bytes_sent;
        state.bytes_acked = path_info.bytes_received;
        
        scheduler_->update_path_state(state);
    }
}

} // namespace mpquic_fec
