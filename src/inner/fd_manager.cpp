/*************************************************************************
    > File Name: inner/fd_manager.cpp
    > Author: hsz
    > Brief: File descriptor context and manager implementation
    > Created Time: 2026-10-23
 ************************************************************************/

#include "inner/fd_manager.h"
#include "hook.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

namespace eular {

FdContext::FdContext(int32_t fd) noexcept :
    m_fd(fd),
    m_isSocket(false),
    m_isUserNonblock(false),
    m_recvTimeoutMs(0),
    m_sendTimeoutMs(0)
{
    struct stat st;
    if (fstat(fd, &st) == 0) {
        m_isSocket = S_ISSOCK(st.st_mode);
    }
}

uint64_t FdContext::getTimeout(int32_t type) const noexcept
{
    if (type == SO_RCVTIMEO) {
        return m_recvTimeoutMs;
    } else if (type == SO_SNDTIMEO) {
        return m_sendTimeoutMs;
    }
    return 0;
}

void FdContext::setTimeout(int32_t type, uint64_t ms) noexcept
{
    if (type == SO_RCVTIMEO) {
        m_recvTimeoutMs = ms;
    } else if (type == SO_SNDTIMEO) {
        m_sendTimeoutMs = ms;
    }
}

FdManager* FdManager::Get() noexcept
{
    static thread_local FdManager s_manager;
    return &s_manager;
}

FdContext::SP FdManager::get(int32_t fd, bool needCreate) noexcept
{
    if (fd < 0) {
        return nullptr;
    }

    auto it = m_contextMap.find(fd);
    if (it != m_contextMap.end()) {
        return it->second;
    }

    if (!needCreate) {
        return nullptr;
    }

    FdContext::SP ctx(new (std::nothrow) FdContext(fd));
    if (!ctx) {
        return nullptr;
    }
    try {
        m_contextMap[fd] = ctx;
        return ctx;
    } catch (...) {
        return nullptr;
    }
}

void FdManager::remove(int32_t fd) noexcept
{
    m_contextMap.erase(fd);
}

} // namespace eular
