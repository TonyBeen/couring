/*************************************************************************
    > File Name: error_code.cpp
    > Author: hsz
    > Brief: Lightweight, zero-exception error code implementation
    > Created Time: 2026-10-23
 ************************************************************************/

#include "error_code.h"

#include <cerrno>
#include <cstring>

namespace couring {

namespace {

class SystemErrorCategory final : public ErrorCategory {
public:
    const char* name() const noexcept override {
        return "system";
    }

    const char* message(int32_t ev) const noexcept override {
        if (ev == 0) {
            return "Success";
        }
        int32_t code = ev < 0 ? -ev : ev;
        const char* msg = ::strerror(code);
        return msg ? msg : "Unknown system error";
    }
};

class CouringErrorCategory final : public ErrorCategory {
public:
    const char* name() const noexcept override {
        return "couring";
    }

    const char* message(int32_t ev) const noexcept override {
        int32_t code = ev < 0 ? -ev : ev;
        switch (static_cast<Errc>(code)) {
        case Errc::Success:
            return "Success";
        case Errc::InvalidArgument:
            return "Invalid argument";
        case Errc::OutOfMemory:
            return "Out of memory";
        case Errc::SubmissionQueueFull:
            return "Submission queue is full";
        case Errc::RingNotInitialized:
            return "io_uring ring is not initialized";
        case Errc::FiberCanceled:
            return "Fiber operation was canceled";
        case Errc::TimedOut:
            return "Operation timed out";
        case Errc::BufferNotFound:
            return "Buffer not found or invalid buffer id";
        case Errc::BufferInUse:
            return "Buffer is currently in use";
        case Errc::OperationNotSupported:
            return "Operation is not supported";
        case Errc::SocketAlreadyClosed:
            return "Socket is already closed";
        case Errc::CrossThreadViolation:
            return "Cross-thread operation violation in N:1 model";
        default:
            return "Unknown couring error";
        }
    }
};

const SystemErrorCategory  kSystemCategoryInstance;
const CouringErrorCategory kCouringCategoryInstance;

} // namespace

const ErrorCategory& system_category() noexcept {
    return kSystemCategoryInstance;
}

const ErrorCategory& couring_category() noexcept {
    return kCouringCategoryInstance;
}

ErrorCode::ErrorCode() noexcept :
    m_val(0),
    m_cat(&system_category())
{
}

ErrorCode::ErrorCode(Errc e) noexcept :
    m_val(static_cast<int32_t>(e)),
    m_cat(&couring_category())
{
}

ErrorCode ErrorCode::from_errno(int32_t sys_errno) noexcept
{
    if (sys_errno == 0) {
        return ErrorCode(0, system_category());
    }

    int32_t code = sys_errno < 0 ? -sys_errno : sys_errno;
    if (code >= 1000) {
        return ErrorCode(code, couring_category());
    }
    return ErrorCode(code, system_category());
}

const char* couring_strerror(int32_t err) noexcept
{
    if (err == 0) {
        return "Success";
    }
    int32_t code = err < 0 ? -err : err;
    if (code >= 1000) {
        return couring_category().message(code);
    }
    return system_category().message(code);
}

const char* couring_strerror(ErrorCode ec) noexcept
{
    return ec.message();
}

} // namespace couring
