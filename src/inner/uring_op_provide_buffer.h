/*************************************************************************
    > File Name: uring_op_provide_buffer.h
    > Author: hsz
    > Brief:
    > Created Time: 2024年12月24日 星期二 17时57分17秒
 ************************************************************************/

#ifndef __COURING_INNER_URING_OP_PROVIDE_BUFFER_H__
#define __COURING_INNER_URING_OP_PROVIDE_BUFFER_H__

#include <memory>
#include <functional>

#include "uring_operation.h"

namespace eular {
class UringOpProvideBuffer : public UringOpBase
{
public:
    using SP  = std::shared_ptr<UringOpProvideBuffer>;
    using WP  = std::weak_ptr<UringOpProvideBuffer>;
    using Ptr = std::unique_ptr<UringOpProvideBuffer>;

    using ProcessCommpledtedCB = std::function<void(int32_t)>;

    UringOpProvideBuffer(ProcessCommpledtedCB cb);
    ~UringOpProvideBuffer() = default;

    void onCompleted(io_uring_cqe *cqe) noexcept override;

private:
    ProcessCommpledtedCB    m_cb;
};

} // namespace eular

#endif // __COURING_INNER_URING_OP_PROVIDE_BUFFER_H__
