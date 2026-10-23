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

#include "buffer/byte_buffer.h"
#include "buffer/bitmap.h"
#include "buffer/buffer_id.h"

namespace eular {
class UringService;
} // namespace eular

namespace couring {

class BufferRegistration {
public:
    using SP  = std::shared_ptr<BufferRegistration>;
    using WP  = std::weak_ptr<BufferRegistration>;
    using Ptr = std::unique_ptr<BufferRegistration>;

    BufferRegistration(const BufferRegistration&) = delete;
    BufferRegistration& operator=(const BufferRegistration&) = delete;

    BufferRegistration(const std::vector<ByteBuffer> &bufferVec, eular::UringService *service);
    ~BufferRegistration();

    BufferId *acquire();
    void release(const BufferId *id);

    size_t size() const noexcept { return m_bufferIdVec.size(); }

private:
    couring::BitMap       m_bitmap;
    eular::UringService*  m_uringService;
    std::vector<BufferId> m_bufferIdVec;
};

} // namespace couring

namespace eular {
using BufferRegistration = couring::BufferRegistration;
} // namespace eular

#endif // __COURING_BUFFER_REGISTRATION_H__
