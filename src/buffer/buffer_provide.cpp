/*************************************************************************
    > File Name: buffer_provide.cpp
    > Author: hsz
    > Brief: Buffer provide implementation
    > Created Time: 2024年12月20日 星期五 18时03分29秒
 ************************************************************************/

#include "buffer_provide.h"
#include "logger_internal.h"
#include "uring_service.h"
#include "inner/uring_op_provide_buffer.h"
#include "fiber.h"
#include "scheduler.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BufferProvide"

namespace eular {

BufferProvide::BufferProvide(const ByteBuffer &buffer, UringService *service, int32_t gid) :
    m_inuse(true),
    m_uringService(service)
{
    size_t bufSize = buffer.size() > 0 ? buffer.size() : buffer.capacity();
    if (bufSize == 0 || service == nullptr) {
        return;
    }

    m_uringOp.reset(new (std::nothrow) UringOpProvideBuffer([this] (int32_t status) {
        if (status < 0) {
            LOGE("provide buffer error: %d", status);
        }

        m_inuse = false;
        if (m_fiber) {
            Scheduler* sched = Scheduler::GetScheduler();
            if (sched) {
                sched->schedule(m_fiber);
            } else {
                m_fiber->resume();
            }
        }
    }));
    if (!m_uringOp) {
        LOGE("Failed to allocate UringOpProvideBuffer");
        return;
    }

    m_bufferId.m_buffer = const_cast<uint8_t *>(buffer.const_data());
    m_bufferId.m_size = bufSize;
    m_bufferId.m_gid = gid;
    m_bufferId.m_bid = 0;
    m_bufferId.m_isRegistrationBuffer = false;

    m_fiber = Fiber::GetThis();
    int32_t status = service->provideBuffers(m_bufferId.m_buffer, bufSize, 1, gid, m_uringOp.get());
    if (status < 0) {
        LOGE("provideBuffers failed, status=%d", status);
        m_fiber = nullptr;
        return;
    }

    if (m_fiber) {
        Fiber::Yield2Hold();
    }
}

BufferProvide::~BufferProvide()
{
    if (m_uringService && m_bufferId.m_gid >= 0) {
        m_uringService->removeBuffers(1, m_bufferId.m_gid);
    }
}

BufferId *BufferProvide::acquire()
{
    if (m_inuse) {
        return nullptr;
    }

    m_inuse = true;
    return &m_bufferId;
}

void BufferProvide::release(const BufferId *id)
{
    if (!id || !m_uringService || m_bufferId.m_gid < 0) {
        return;
    }

    m_fiber = Fiber::GetThis();
    int32_t status = m_uringService->provideBuffers(m_bufferId.m_buffer, m_bufferId.m_size, 1, m_bufferId.m_gid, m_uringOp.get());
    if (status < 0) {
        LOGE("provideBuffers failed in release, status=%d", status);
        m_fiber = nullptr;
        return;
    }
    if (m_fiber) {
        Fiber::Yield2Hold();
    }
}

} // namespace eular
