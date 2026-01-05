#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <cstring>

namespace mpquic_fec {

/**
 * @brief 零拷贝缓冲区管理器
 * 
 * 使用内存池和引用计数，避免不必要的数据拷贝
 */
class Buffer {
public:
    Buffer(uint32_t capacity);
    ~Buffer();

    // 禁止拷贝，只允许移动
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;

    /**
     * @brief 写入数据
     */
    void write(const uint8_t* data, uint32_t size);

    /**
     * @brief 读取数据
     */
    const uint8_t* data() const { return data_.get(); }
    uint32_t size() const { return size_; }
    uint32_t capacity() const { return capacity_; }

    /**
     * @brief 重置缓冲区
     */
    void reset();

private:
    std::unique_ptr<uint8_t[]> data_;
    uint32_t size_ = 0;
    uint32_t capacity_;
};

/**
 * @brief 缓冲区池，用于重用内存
 */
class BufferPool {
public:
    static BufferPool& instance();

    /**
     * @brief 获取一个缓冲区
     */
    Buffer acquire(uint32_t size);

    /**
     * @brief 归还缓冲区
     */
    void release(Buffer&& buffer);

private:
    BufferPool() = default;
    std::vector<Buffer> pool_;
};

} // namespace mpquic_fec
