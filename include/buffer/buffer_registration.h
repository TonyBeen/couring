/*************************************************************************
    > File Name: buffer/buffer_registration.h
    > Author: hsz
    > Brief: Buffer registration management (io_uring_register_buffers)
    > Created Time: 2024-12-20
 ************************************************************************/

#ifndef __COURING_BUFFER_REGISTRATION_H__
#define __COURING_BUFFER_REGISTRATION_H__

#include <cstdint>
#include <vector>
#include <memory>
#include <sys/uio.h>

#include <utils/buffer.h>
#include <utils/bitmap.h>

#include "buffer/buffer_id.h"

namespace eular {

class UringService;

class BufferRegistration {
public:
    using SP  = std::shared_ptr<BufferRegistration>;
    using WP  = std::weak_ptr<BufferRegistration>;
    using Ptr = std::unique_ptr<BufferRegistration>;

    BufferRegistration(const BufferRegistration&) = delete;
    BufferRegistration& operator=(const BufferRegistration&) = delete;

    BufferRegistration(const std::vector<ByteBuffer> &bufferVec, UringService *service);
    ~BufferRegistration();

    BufferId *acquire();
    void release(const BufferId *id);

    size_t size() const noexcept { return m_bufferIdVec.size(); }

private:
    eular::BitMap         m_bitmap;
    UringService*         m_uringService;
    std::vector<BufferId> m_bufferIdVec;
};

} // namespace eular

namespace couring {
using BufferRegistration = eular::BufferRegistration;
} // namespace couring

#endif // __COURING_BUFFER_REGISTRATION_H__
