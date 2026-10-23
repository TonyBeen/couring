/*************************************************************************
    > File Name: buffer/bitmap.h
    > Author: hsz
    > Brief: Lightweight, compact runtime bitmap (C++11 compatible)
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_BITMAP_H__
#define __COURING_BITMAP_H__

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace couring {

/**
 * @brief Dynamic BitMap managing bits packed in uint64_t words
 * Separated under couring namespace to prevent symbol conflicts with eular::BitMap.
 */
class BitMap final {
public:
    BitMap() noexcept
        : m_size(0), m_count(0) {}

    explicit BitMap(size_t bit_size)
        : m_size(0), m_count(0) {
        resize(bit_size);
    }

    BitMap(const BitMap& other) = default;
    BitMap(BitMap&& other) noexcept = default;
    BitMap& operator=(const BitMap& other) = default;
    BitMap& operator=(BitMap&& other) noexcept = default;
    ~BitMap() = default;

    /**
     * @brief Resize bitmap to hold bit_size bits
     */
    bool resize(size_t bit_size) {
        m_size = bit_size;
        size_t words = (bit_size + 63) / 64;
        m_bits.resize(words, 0);
        // Recount set bits
        m_count = 0;
        for (size_t i = 0; i < words; ++i) {
            if (m_bits[i] != 0) {
                m_count += __builtin_popcountll(m_bits[i]);
            }
        }
        return true;
    }

    /**
     * @brief Set bit at idx to val
     */
    bool set(size_t idx, bool val = true) noexcept {
        if (idx >= m_size) {
            return false;
        }

        size_t word_idx = idx / 64;
        uint64_t mask = 1ULL << (idx % 64);
        bool current = (m_bits[word_idx] & mask) != 0;

        if (val != current) {
            if (val) {
                m_bits[word_idx] |= mask;
                ++m_count;
            } else {
                m_bits[word_idx] &= ~mask;
                --m_count;
            }
        }
        return true;
    }

    /**
     * @brief Test whether bit at idx is set
     */
    bool at(size_t idx) const noexcept {
        if (idx >= m_size) {
            return false;
        }
        return (m_bits[idx / 64] & (1ULL << (idx % 64))) != 0;
    }

    bool test(size_t idx) const noexcept {
        return at(idx);
    }

    /**
     * @brief Reset all bits to 0
     */
    void clear() noexcept {
        std::fill(m_bits.begin(), m_bits.end(), 0ULL);
        m_count = 0;
    }

    /**
     * @brief Total number of bits set to 1
     */
    size_t count() const noexcept {
        return m_count;
    }

    /**
     * @brief Total bit capacity / size
     */
    size_t size() const noexcept {
        return m_size;
    }

    /**
     * @brief Find first unset bit index (returns -1 if full)
     */
    int64_t find_first_unset() const noexcept {
        if (m_count >= m_size) {
            return -1;
        }

        for (size_t i = 0; i < m_bits.size(); ++i) {
            if (m_bits[i] != ~0ULL) {
                uint64_t inverted = ~m_bits[i];
                int bit_pos = __builtin_ctzll(inverted);
                size_t res = i * 64 + bit_pos;
                if (res < m_size) {
                    return static_cast<int64_t>(res);
                }
            }
        }
        return -1;
    }

private:
    size_t                m_size;
    size_t                m_count;
    std::vector<uint64_t> m_bits;
};

} // namespace couring

#endif // __COURING_BITMAP_H__
