/*************************************************************************
    > File Name: buffer/buffer_id.h
    > Author: hsz
    > Brief: Buffer identifier representing a registered or provided buffer
    > Created Time: 2024-12-20
 ************************************************************************/

#ifndef __COURING_BUFFER_ID_H__
#define __COURING_BUFFER_ID_H__

#include <cstdint>
#include <memory>

namespace eular {

class BufferRegistration;
class BufferProvide;

class BufferId {
    friend class BufferRegistration;
    friend class BufferProvide;
public:
    BufferId() noexcept :
        m_isRegistrationBuffer(false),
        m_gid(-1),
        m_bid(-1),
        m_size(0),
        m_buffer(nullptr) {}

    ~BufferId() = default;

    BufferId(const BufferId &other) noexcept :
        m_isRegistrationBuffer(other.m_isRegistrationBuffer),
        m_gid(other.m_gid),
        m_bid(other.m_bid),
        m_size(other.m_size),
        m_buffer(other.m_buffer) {}

    BufferId &operator=(const BufferId &other) noexcept {
        if (this != std::addressof(other)) {
            m_isRegistrationBuffer = other.m_isRegistrationBuffer;
            m_gid = other.m_gid;
            m_bid = other.m_bid;
            m_size = other.m_size;
            m_buffer = other.m_buffer;
        }
        return *this;
    }

    void *buffer() const noexcept { return m_buffer; }
    int32_t gid() const noexcept { return m_gid; }
    int32_t bid() const noexcept { return m_bid; }
    uint32_t size() const noexcept { return m_size; }
    bool isRegistrationBuffer() const noexcept { return m_isRegistrationBuffer; }

private:
    bool     m_isRegistrationBuffer;
    int32_t  m_gid; // Group ID for provided buffer
    int32_t  m_bid; // Buffer ID / Index
    uint32_t m_size;
    void*    m_buffer;
};

} // namespace eular

namespace couring {
using BufferId = eular::BufferId;
} // namespace couring

#endif // __COURING_BUFFER_ID_H__
