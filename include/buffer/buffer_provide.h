/*************************************************************************
    > File Name: buffer/buffer_provide.h
    > Author: hsz
    > Brief: Buffer provide management (io_uring_provide_buffers)
    > Created Time: 2024-12-20
 ************************************************************************/

#ifndef __COURING_BUFFER_PROVIDE_H__
#define __COURING_BUFFER_PROVIDE_H__

#include <cstdint>
#include <vector>
#include <memory>

#include <utils/buffer.h>

#include "buffer/buffer_id.h"

namespace eular {

class UringService;
class Fiber;
struct UringOpBase;

class BufferProvide {
public:
    using SP  = std::shared_ptr<BufferProvide>;
    using WP  = std::weak_ptr<BufferProvide>;
    using Ptr = std::unique_ptr<BufferProvide>;

    BufferProvide(const BufferProvide&) = delete;
    BufferProvide& operator=(const BufferProvide&) = delete;

    BufferProvide(const ByteBuffer &buffer, UringService *service, int32_t gid);
    ~BufferProvide();

    BufferId *acquire();
    void release(const BufferId *id);

    int32_t gid() const noexcept { return m_bufferId.gid(); }

private:
    bool                         m_inuse;
    UringService*                m_uringService;
    BufferId                     m_bufferId;
    std::shared_ptr<UringOpBase> m_uringOp;
    std::shared_ptr<Fiber>       m_fiber;
};

} // namespace eular

namespace couring {
using BufferProvide = eular::BufferProvide;
} // namespace couring

#endif // __COURING_BUFFER_PROVIDE_H__
