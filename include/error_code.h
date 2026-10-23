/*************************************************************************
    > File Name: error_code.h
    > Author: hsz
    > Brief: Lightweight, zero-exception error code system
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_ERROR_CODE_H__
#define __COURING_ERROR_CODE_H__

#include <cstdint>
#include <cstring>

namespace couring {

enum class Errc : int32_t {
    Success               = 0,
    InvalidArgument       = 1001,
    OutOfMemory           = 1002,
    SubmissionQueueFull   = 1003,
    RingNotInitialized    = 1004,
    FiberCanceled         = 1005,
    TimedOut              = 1006,
    BufferNotFound        = 1007,
    BufferInUse           = 1008,
    OperationNotSupported = 1009,
    SocketAlreadyClosed   = 1010,
    CrossThreadViolation  = 1011,
};

class ErrorCategory {
public:
    virtual ~ErrorCategory() = default;
    virtual const char* name() const noexcept = 0;
    virtual const char* message(int32_t ev) const noexcept = 0;
};

const ErrorCategory& system_category() noexcept;
const ErrorCategory& couring_category() noexcept;

class ErrorCode {
public:
    ErrorCode() noexcept;
    ErrorCode(int32_t val, const ErrorCategory& cat) noexcept : m_val(val), m_cat(&cat) {}
    ErrorCode(Errc e) noexcept;

    // Convert from POSIX errno or negative return value (e.g. -EINVAL or EINVAL)
    static ErrorCode from_errno(int32_t sys_errno) noexcept;

    constexpr int32_t value() const noexcept { return m_val; }
    const ErrorCategory& category() const noexcept { return *m_cat; }
    const char* message() const noexcept { return m_cat->message(m_val); }

    constexpr explicit operator bool() const noexcept { return m_val != 0; }
    constexpr bool operator!() const noexcept { return m_val == 0; }
    constexpr bool operator==(const ErrorCode& rhs) const noexcept {
        return m_val == rhs.m_val && m_cat == rhs.m_cat;
    }
    constexpr bool operator!=(const ErrorCode& rhs) const noexcept {
        return !(*this == rhs);
    }

private:
    int32_t              m_val;
    const ErrorCategory* m_cat;
};

// Fast thread-safe error string lookup
const char* couring_strerror(int32_t err) noexcept;
const char* couring_strerror(ErrorCode ec) noexcept;

} // namespace couring

#endif // __COURING_ERROR_CODE_H__
