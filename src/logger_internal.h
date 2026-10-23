/*************************************************************************
    > File Name: logger_internal.h
    > Author: hsz
    > Brief: Internal logging helper macros for couring implementation
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_LOGGER_INTERNAL_H__
#define __COURING_LOGGER_INTERNAL_H__

#include "logger.h"

#ifndef LOG_TAG
#define LOG_TAG "couring"
#endif

#define LOGD(fmt, ...) ::couring::log_write(::couring::LogLevel::DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) ::couring::log_write(::couring::LogLevel::INFO,  LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) ::couring::log_write(::couring::LogLevel::WARN,  LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) ::couring::log_write(::couring::LogLevel::ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

#endif // __COURING_LOGGER_INTERNAL_H__
