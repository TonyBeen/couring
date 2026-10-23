/*************************************************************************
    > File Name: buffer_registration.cpp
    > Author: hsz
    > Brief: Buffer registration implementation
    > Created Time: 2024年12月20日 星期五 18时03分25秒
 ************************************************************************/

#include "buffer_registration.h"
#include "logger_internal.h"
#include "uring_service.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BufferRegistration"

namespace eular {

BufferRegistration::BufferRegistration(const std::vector<ByteBuffer> &bufferVec, UringService *service) :
    m_uringService(service)
{
    if (bufferVec.empty() || service == nullptr) {
        return;
    }

    m_bufferIdVec.resize(bufferVec.size());
    m_bitmap.resize(bufferVec.size());

    std::vector<iovec> iovecs(bufferVec.size());

    int32_t i = 0;
    for (auto it = bufferVec.begin(); it != bufferVec.end(); ++it, ++i) {
        BufferId bid;
        bid.m_buffer = const_cast<uint8_t *>(it->const_data());
        bid.m_bid = i;
        bid.m_size = static_cast<uint32_t>(it->capacity());
        bid.m_isRegistrationBuffer = true;
        m_bufferIdVec[i] = bid;

        iovecs[i].iov_base = bid.buffer();
        iovecs[i].iov_len = it->capacity();
    }

    int32_t status = service->registerBuffers(&iovecs[0], iovecs.size());
    if (status < 0) {
        LOGE("io_uring_register_buffers error: %d", status);
    }
}

BufferRegistration::~BufferRegistration()
{
    if (m_uringService) {
        m_uringService->unregisterBuffers();
    }
}

BufferId *BufferRegistration::acquire()
{
    BufferId *result = nullptr;
    if (m_bitmap.count() >= m_bufferIdVec.size()) {
        return result;
    }

    for (uint32_t i = 0; i < m_bitmap.size(); ++i) {
        if (m_bitmap.at(i)) {
            continue;
        }

        m_bitmap.set(i, true);
        result = &m_bufferIdVec[i];
        break; // Fixed: Break immediately upon finding first free buffer!
    }

    return result;
}

void BufferRegistration::release(const BufferId *id)
{
    if (id == nullptr) {
        return;
    }

    if (id->m_isRegistrationBuffer) {
        if (0 <= id->m_bid && static_cast<size_t>(id->m_bid) < m_bufferIdVec.size()) {
            m_bitmap.set(id->m_bid, false);
        }
    }
}

} // namespace eular
