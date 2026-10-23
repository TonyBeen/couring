/*************************************************************************
    > File Name: inner/fd_manager.h
    > Author: hsz
    > Brief: File descriptor context and manager for coroutine hook support
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_INNER_FD_MANAGER_H__
#define __COURING_INNER_FD_MANAGER_H__

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <sys/stat.h>

namespace eular {

class FdContext : public std::enable_shared_from_this<FdContext> {
public:
    using SP  = std::shared_ptr<FdContext>;
    using WP  = std::weak_ptr<FdContext>;
    using Ptr = std::unique_ptr<FdContext>;

    explicit FdContext(int32_t fd) noexcept;
    ~FdContext() noexcept = default;

    bool isSocket() const noexcept { return m_isSocket; }
    bool isUserNonblock() const noexcept { return m_isUserNonblock; }
    void setUserNonblock(bool flag) noexcept { m_isUserNonblock = flag; }

    uint64_t getTimeout(int32_t type) const noexcept;
    void setTimeout(int32_t type, uint64_t ms) noexcept;

    int32_t fd() const noexcept { return m_fd; }

private:
    int32_t  m_fd;
    bool     m_isSocket;
    bool     m_isUserNonblock;
    uint64_t m_recvTimeoutMs;
    uint64_t m_sendTimeoutMs;
};

class FdManager {
public:
    using SP = std::shared_ptr<FdManager>;

    FdManager() noexcept = default;
    ~FdManager() noexcept = default;

    static FdManager* Get() noexcept;

    FdContext::SP get(int32_t fd, bool needCreate = false) noexcept;
    void remove(int32_t fd) noexcept;

private:
    std::unordered_map<int32_t, FdContext::SP> m_contextMap;
};

} // namespace eular

#endif // __COURING_INNER_FD_MANAGER_H__
