/*************************************************************************
    > File Name: io/co_timer.h
    > Author: hsz
    > Brief: Coroutine timer sleep API
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_CO_TIMER_H__
#define __COURING_CO_TIMER_H__

#include <cstdint>

namespace couring {

int32_t co_msleep(uint64_t ms) noexcept;

} // namespace couring

#endif // __COURING_CO_TIMER_H__
