#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace mpquic_fec {

/**
 * @brief FEC编码器 - 基于Reed-Solomon纠删码
 * 
 * 使用 k 个数据块生成 m 个冗余块，总共 n=k+m 块
 * 可以容忍任意 m 个块丢失
 */
class FECEncoder {
public:
    /**
     * @param k 数据块数量
     * @param m 冗余块数量
     * @param block_size 每个块的大小（字节）
     */
    FECEncoder(uint32_t k, uint32_t m, uint32_t block_size);
    ~FECEncoder();

    /**
     * @brief 编码数据块
     * @param data_blocks k个数据块
     * @return m个冗余块
     */
    std::vector<std::vector<uint8_t>> encode(
        const std::vector<std::vector<uint8_t>>& data_blocks);

    uint32_t get_k() const { return k_; }
    uint32_t get_m() const { return m_; }
    uint32_t get_block_size() const { return block_size_; }

private:
    uint32_t k_;           // 数据块数
    uint32_t m_;           // 冗余块数
    uint32_t block_size_;  // 块大小
    void* encode_matrix_;  // 编码矩阵（ISA-L使用）
};

/**
 * @brief FEC解码器
 */
class FECDecoder {
public:
    FECDecoder(uint32_t k, uint32_t m, uint32_t block_size);
    ~FECDecoder();

    /**
     * @brief 解码恢复丢失的数据块
     * @param received_blocks 接收到的块（可能包含数据块和冗余块）
     * @param block_ids 每个块的ID (0到k-1是数据块, k到n-1是冗余块)
     * @return 恢复的完整数据块
     */
    std::vector<std::vector<uint8_t>> decode(
        const std::vector<std::vector<uint8_t>>& received_blocks,
        const std::vector<uint32_t>& block_ids);

private:
    uint32_t k_;
    uint32_t m_;
    uint32_t block_size_;
    void* decode_matrix_;
};

} // namespace mpquic_fec
