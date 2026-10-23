/*************************************************************************
    > File Name: uring_operation.h
    > Author: hsz
    > Brief: Base and synchronous/callback operation classes for io_uring
    > Created Time: 2024年12月22日 星期日 13时35分24秒
 ************************************************************************/

#ifndef __COURING_URING_OPERATION_H__
#define __COURING_URING_OPERATION_H__

#include <cstdint>
#include <memory>
#include <functional>
#include <cerrno>

#include "fiber.h"

struct io_uring_cqe;

namespace eular {

class UringOpBase
{
public:
    using SP  = std::shared_ptr<UringOpBase>;
    using WP  = std::weak_ptr<UringOpBase>;
    using Ptr = std::unique_ptr<UringOpBase>;

    UringOpBase() noexcept = default;
    virtual ~UringOpBase() noexcept = default;

    virtual void onCompleted(struct io_uring_cqe *cqe) noexcept = 0;
};

/**
 * @brief Synchronous operation for coroutine suspension and resumption (zero heap allocation)
 */
class UringOpSync : public UringOpBase
{
public:
    explicit UringOpSync(Fiber::SP fiber) noexcept :
        m_fiber(std::move(fiber)),
        m_res(0),
        m_flags(0),
        m_completed(false),
        m_timedOut(false)
    {
    }

    void onCompleted(struct io_uring_cqe *cqe) noexcept override;

    int32_t  result() const noexcept {
        if (m_timedOut) {
            return -ETIMEDOUT;
        }
        return m_res;
    }
    uint32_t flags() const noexcept { return m_flags; }
    bool     isCompleted() const noexcept { return m_completed; }
    void     setTimedOut(bool timedOut = true) noexcept { m_timedOut = timedOut; }
    bool     isTimedOut() const noexcept { return m_timedOut; }

private:
    Fiber::SP m_fiber;
    int32_t   m_res;
    uint32_t  m_flags;
    bool      m_completed;
    bool      m_timedOut;
};

/**
 * @brief Callback-based operation
 */
class UringOpCallback : public UringOpBase
{
public:
    using Callback = std::function<void(int32_t res, uint32_t flags)>;

    explicit UringOpCallback(Callback cb) noexcept :
        m_cb(std::move(cb))
    {
    }

    void onCompleted(struct io_uring_cqe *cqe) noexcept override;

private:
    Callback m_cb;
};

} // namespace eular

#endif // __COURING_URING_OPERATION_H__
