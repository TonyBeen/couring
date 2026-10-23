/*************************************************************************
    > File Name: uring_op_provide_buffer.cpp
    > Author: hsz
    > Brief:
    > Created Time: 2024年12月24日 星期二 17时57分20秒
 ************************************************************************/

#include "inner/uring_op_provide_buffer.h"
#include "uring_op_provide_buffer.h"

#include <liburing.h>

namespace eular {
UringOpProvideBuffer::UringOpProvideBuffer(ProcessCommpledtedCB cb) :
    m_cb(cb)
{
}

void UringOpProvideBuffer::onCompleted(io_uring_cqe *cqe) noexcept
{
    if (m_cb && cqe) {
        m_cb(cqe->res);
    }
}
} // namespace eular