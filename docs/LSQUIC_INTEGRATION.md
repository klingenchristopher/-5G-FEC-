# liblsquic 集成指南

## 当前状态

项目已经实现了完整的**MPQUIC+FEC集成架构**，包括：

✅ **抽象层设计** - `IQUICConnection` 接口
✅ **模拟实现** - `MockQUICConnection` 类（用于开发和测试）
✅ **多路径管理** - `MPQUICManager` 类
✅ **FEC集成** - 自动在多路径上应用FEC保护
✅ **路径调度** - OCO算法智能选择传输路径
✅ **完整演示** - `demo_mpquic_real` 程序

**当前使用模拟QUIC实现，可以直接演示完整功能。**

---

## 架构设计

### 1. 三层架构

```
┌─────────────────────────────────────┐
│   MPQUICManager (高层API)           │
│   - 多路径管理                       │
│   - FEC自动应用                      │
│   - 路径调度集成                     │
└──────────────┬──────────────────────┘
               │
┌──────────────┴──────────────────────┐
│   IQUICConnection (抽象层)          │
│   - 统一的QUIC操作接口               │
│   - 与具体实现解耦                   │
└──────────────┬──────────────────────┘
               │
       ┌───────┴────────┐
       │                │
┌──────┴──────┐  ┌──────┴───────────┐
│ Mock实现     │  │ liblsquic实现     │
│ (当前)       │  │ (待集成)          │
└─────────────┘  └──────────────────┘
```

### 2. 关键接口

所有QUIC操作都通过 `IQUICConnection` 接口进行：

```cpp
class IQUICConnection {
    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual PathID add_path(...) = 0;
    virtual size_t send_on_path(PathID, StreamID, data) = 0;
    // ... 其他接口
};
```

---

## 集成 liblsquic 的步骤

### 第一步：编译 liblsquic

```bash
# 1. 安装依赖
sudo apt-get install -y golang-go libevent-dev zlib1g-dev

# 2. 编译 BoringSSL
cd /tmp
git clone https://boringssl.googlesource.com/boringssl
cd boringssl
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install

# 3. 编译 liblsquic
cd /tmp
git clone https://github.com/litespeedtech/lsquic.git
cd lsquic
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBORINGSSL_INCLUDE=/usr/local/include \
      -DBORINGSSL_LIB=/usr/local/lib \
      -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

### 第二步：创建真实 QUIC 实现

创建文件 `src/quic/lsquic_connection.cpp`：

```cpp
#include "quic_connection.hpp"
#include <lsquic.h>

namespace mpquic_fec {

class LSQUICConnection : public IQUICConnection {
private:
    lsquic_engine_t* engine_;
    lsquic_conn_t* conn_;
    // ... 其他成员
    
public:
    bool connect(const std::string& host, uint16_t port) override {
        // 使用 lsquic_engine_connect() 实现
        // ...
    }
    
    size_t send_on_path(PathID path_id, StreamID stream_id,
                       const std::vector<uint8_t>& data,
                       bool fin) override {
        // 使用 lsquic_stream_write() 实现
        // 结合 lsquic 的多路径API
        // ...
    }
    
    // 实现所有接口方法...
};

} // namespace mpquic_fec
```

### 第三步：更新工厂函数

修改 `src/quic/mock_quic_connection.cpp` 中的工厂函数：

```cpp
#ifdef USE_REAL_LSQUIC
#include "lsquic_connection.hpp"
#endif

std::unique_ptr<IQUICConnection> create_quic_connection(bool use_real_impl) {
#ifdef USE_REAL_LSQUIC
    if (use_real_impl) {
        return std::make_unique<LSQUICConnection>();
    }
#endif
    return std::make_unique<MockQUICConnection>();
}
```

### 第四步：更新 CMake 配置

修改 `src/quic/CMakeLists.txt`：

```cmake
# 查找 liblsquic
find_library(LSQUIC_LIBRARY NAMES lsquic PATHS /usr/local/lib)
find_path(LSQUIC_INCLUDE_DIR lsquic.h PATHS /usr/local/include)

if(LSQUIC_LIBRARY AND LSQUIC_INCLUDE_DIR)
    message(STATUS "Found liblsquic: ${LSQUIC_LIBRARY}")
    
    # 添加真实实现源文件
    target_sources(mpquic_integration PRIVATE
        lsquic_connection.cpp
    )
    
    # 链接 liblsquic
    target_link_libraries(mpquic_integration PRIVATE
        ${LSQUIC_LIBRARY}
        event
        ssl
        crypto
    )
    
    # 添加头文件路径
    target_include_directories(mpquic_integration PRIVATE
        ${LSQUIC_INCLUDE_DIR}
    )
    
    # 定义宏以启用真实实现
    target_compile_definitions(mpquic_integration PRIVATE
        USE_REAL_LSQUIC
    )
else()
    message(STATUS "liblsquic not found, using mock implementation")
endif()
```

### 第五步：重新编译

```bash
cd /workspaces/-5G-FEC-/build
cmake ..
make -j$(nproc)

# 运行真实实现
./bin/demo_mpquic_real
```

---

## liblsquic 关键API映射

| IQUICConnection 方法 | liblsquic API |
|---------------------|---------------|
| `connect()` | `lsquic_engine_connect()` |
| `listen()` | `lsquic_engine_packet_in()` + 事件循环 |
| `create_stream()` | `lsquic_conn_make_stream()` |
| `send_on_path()` | `lsquic_stream_write()` + 路径选择 |
| `add_path()` | 利用 QUIC 的多路径扩展或多个socket |
| `process_events()` | `lsquic_engine_process_conns()` |
| `close()` | `lsquic_conn_close()` |

---

## 多路径 QUIC 实现方案

liblsquic 本身支持多路径，可以通过以下方式实现：

### 方案1：多个本地地址
```cpp
// 为每条路径创建不同的本地socket
PathID add_path(...) {
    struct sockaddr_in local_addr;
    // 绑定到不同的网卡/IP
    int sock = socket(...);
    bind(sock, &local_addr, ...);
    
    // 在该socket上发送QUIC包
    lsquic_engine_send_unsent_packets(engine_);
}
```

### 方案2：QUIC Connection Migration
```cpp
// 使用 QUIC 的连接迁移特性
// 在不同的网络路径间切换
lsquic_conn_migrate(conn_, new_path_cid);
```

### 方案3：多个并发连接
```cpp
// 为每条路径维护独立的 QUIC 连接
std::map<PathID, lsquic_conn_t*> path_connections_;
```

---

## 测试验证

### 1. 单元测试
```cpp
TEST(LSQUICConnection, Connect) {
    auto conn = create_quic_connection(true);
    ASSERT_TRUE(conn->connect("localhost", 4433));
}

TEST(LSQUICConnection, MultiPath) {
    auto conn = create_quic_connection(true);
    conn->connect("localhost", 4433);
    
    PathID path2 = conn->add_path("192.168.1.2", 0, "localhost", 4434);
    ASSERT_NE(path2, static_cast<PathID>(-1));
}
```

### 2. 集成测试
```bash
# 启动服务器
./bin/demo_mpquic_real server

# 启动客户端（另一个终端）
./bin/demo_mpquic_real client
```

---

## 性能优化建议

### 1. 零拷贝
- 使用 `lsquic` 的 zero-copy API
- 集成现有的 `BufferManager`

### 2. 事件循环
- 使用 `libevent` 或 `epoll`
- 异步处理网络事件

### 3. 线程模型
```cpp
// 主线程：处理QUIC事件
while (running) {
    lsquic_engine_process_conns(engine_);
    process_io_events(50);  // 50ms超时
}

// 工作线程：FEC编码
thread_pool_.submit([data] {
    auto parity = fec_encoder_->encode(data);
});
```

---

## 调试技巧

### 1. 启用 liblsquic 日志
```cpp
lsquic_logger_init(&logger_if, nullptr, LLTS_HHMMSSMS);
lsquic_set_log_level("debug");
```

### 2. 网络抓包
```bash
# 捕获 QUIC 流量
sudo tcpdump -i any -w quic_traffic.pcap udp port 4433

# 使用 Wireshark 分析
wireshark quic_traffic.pcap
```

### 3. 性能分析
```bash
# 使用 perf 分析性能
perf record -g ./bin/demo_mpquic_real
perf report
```

---

## 常见问题

### Q1: liblsquic 编译失败
**A:** 确保安装了正确版本的 BoringSSL 和依赖库：
```bash
apt-cache policy golang-go libevent-dev zlib1g-dev
```

### Q2: 多路径不生效
**A:** 检查：
1. 是否有多个可用网卡
2. 路由表配置是否正确
3. 防火墙是否阻止

### Q3: 性能不如预期
**A:** 优化点：
1. 启用 GSO (Generic Segmentation Offload)
2. 调整拥塞控制算法
3. 增大缓冲区大小

---

## 参考资源

- [liblsquic 官方文档](https://lsquic.readthedocs.io/)
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000)
- [Multipath QUIC Draft](https://datatracker.ietf.org/doc/draft-ietf-quic-multipath/)
- 项目源码：`include/quic_connection.hpp`

---

## 下一步计划

- [ ] 实现 `LSQUICConnection` 类
- [ ] 集成 Intel ISA-L 加速 FEC
- [ ] 添加单元测试
- [ ] 性能基准测试
- [ ] 编写用户手册

---

**注意**：当前项目已经完全可用，使用模拟实现可以充分演示所有功能。集成真实的 liblsquic 只是将底层传输替换为真实网络，上层逻辑无需改动。
