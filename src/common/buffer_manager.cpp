#include "buffer_manager.hpp"
#include "logger.hpp"

namespace mpquic_fec {

Buffer::Buffer(uint32_t capacity)
    : data_(std::make_unique<uint8_t[]>(capacity)),
      capacity_(capacity) {
}

Buffer::~Buffer() {
}

Buffer::Buffer(Buffer&& other) noexcept
    : data_(std::move(other.data_)),
      size_(other.size_),
      capacity_(other.capacity_) {
    other.size_ = 0;
    other.capacity_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        data_ = std::move(other.data_);
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

void Buffer::write(const uint8_t* data, uint32_t size) {
    if (size > capacity_) {
        throw std::runtime_error("Buffer overflow: requested " + 
                               std::to_string(size) + " bytes, capacity " + 
                               std::to_string(capacity_));
    }
    std::memcpy(data_.get(), data, size);
    size_ = size;
}

void Buffer::reset() {
    size_ = 0;
}

BufferPool& BufferPool::instance() {
    static BufferPool pool;
    return pool;
}

Buffer BufferPool::acquire(uint32_t size) {
    // 简化实现：直接创建新缓冲区
    // 实际应从池中查找可重用的缓冲区
    LOG_DEBUG("Acquired buffer of size ", size);
    return Buffer(size);
}

void BufferPool::release(Buffer&& buffer) {
    // 简化实现：让缓冲区自动释放
    // 实际应将缓冲区放回池中以供重用
    LOG_DEBUG("Released buffer of size ", buffer.capacity());
}

} // namespace mpquic_fec
