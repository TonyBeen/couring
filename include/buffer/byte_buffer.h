/*************************************************************************
    > File Name: buffer/byte_buffer.h
    > Author: hsz
    > Brief: Lightweight, self-contained C++11 byte buffer implementation
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_BYTE_BUFFER_H__
#define __COURING_BYTE_BUFFER_H__

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>

namespace couring {

/**
 * @brief Self-contained byte buffer for I/O operations (C++11 compatible)
 * Separated under couring namespace to prevent symbol conflicts with eular::ByteBuffer.
 */
class ByteBuffer final {
public:
    ByteBuffer() noexcept
        : m_data(nullptr), m_size(0), m_capacity(0) {}

    explicit ByteBuffer(size_t capacity)
        : m_data(nullptr), m_size(0), m_capacity(0) {
        if (capacity > 0) {
            reserve(capacity);
        }
    }

    ByteBuffer(const void* data, size_t size)
        : m_data(nullptr), m_size(0), m_capacity(0) {
        if (data != nullptr && size > 0) {
            reserve(size);
            std::memcpy(m_data, data, size);
            m_size = size;
        }
    }

    ByteBuffer(const ByteBuffer& other)
        : m_data(nullptr), m_size(0), m_capacity(0) {
        if (other.m_capacity > 0) {
            reserve(other.m_capacity);
            if (other.m_size > 0) {
                std::memcpy(m_data, other.m_data, other.m_size);
                m_size = other.m_size;
            }
        }
    }

    ByteBuffer(ByteBuffer&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    ~ByteBuffer() {
        if (m_data != nullptr) {
            std::free(m_data);
            m_data = nullptr;
        }
        m_size = 0;
        m_capacity = 0;
    }

    ByteBuffer& operator=(const ByteBuffer& other) {
        if (this != &other) {
            if (other.m_capacity > m_capacity) {
                uint8_t* new_data = static_cast<uint8_t*>(std::realloc(m_data, other.m_capacity));
                if (new_data != nullptr) {
                    m_data = new_data;
                    m_capacity = other.m_capacity;
                } else {
                    return *this;
                }
            }
            m_size = other.m_size;
            if (m_size > 0 && m_data != nullptr && other.m_data != nullptr) {
                std::memcpy(m_data, other.m_data, m_size);
            }
        }
        return *this;
    }

    ByteBuffer& operator=(ByteBuffer&& other) noexcept {
        if (this != &other) {
            if (m_data != nullptr) {
                std::free(m_data);
            }
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;

            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    uint8_t* data() noexcept { return m_data; }
    const uint8_t* data() const noexcept { return m_data; }
    const uint8_t* const_data() const noexcept { return m_data; }

    const uint8_t* begin() const noexcept { return m_data; }
    const uint8_t* end() const noexcept { return m_data ? (m_data + m_size) : nullptr; }
    uint8_t* begin() noexcept { return m_data; }
    uint8_t* end() noexcept { return m_data ? (m_data + m_size) : nullptr; }

    uint8_t& operator[](size_t index) noexcept { return m_data[index]; }
    const uint8_t& operator[](size_t index) const noexcept { return m_data[index]; }

    size_t size() const noexcept { return m_size; }
    size_t capacity() const noexcept { return m_capacity; }
    bool empty() const noexcept { return m_size == 0; }

    void reserve(size_t new_capacity) {
        if (new_capacity <= m_capacity) {
            return;
        }
        uint8_t* new_data = static_cast<uint8_t*>(std::realloc(m_data, new_capacity));
        if (new_data != nullptr) {
            m_data = new_data;
            m_capacity = new_capacity;
        }
    }

    void resize(size_t new_size) {
        if (new_size > m_capacity) {
            reserve(new_size);
        }
        m_size = new_size;
    }

    void clear() noexcept {
        m_size = 0;
    }

    void append(const void* data, size_t len) {
        if (data == nullptr || len == 0) {
            return;
        }
        if (m_size + len > m_capacity) {
            size_t new_cap = m_capacity == 0 ? 64 : m_capacity * 2;
            while (new_cap < m_size + len) {
                new_cap *= 2;
            }
            reserve(new_cap);
        }
        if (m_data != nullptr) {
            std::memcpy(m_data + m_size, data, len);
            m_size += len;
        }
    }

    void append(const ByteBuffer& other) {
        append(other.data(), other.size());
    }

    void swap(ByteBuffer& other) noexcept {
        std::swap(m_data, other.m_data);
        std::swap(m_size, other.m_size);
        std::swap(m_capacity, other.m_capacity);
    }

private:
    uint8_t*    m_data;
    size_t      m_size;
    size_t      m_capacity;
};

} // namespace couring

#endif // __COURING_BYTE_BUFFER_H__
