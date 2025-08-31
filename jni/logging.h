// 日志记录宏定义
#pragma once

#include <android/log.h>

// 日志级别宏定义
#define LOGI(...) (__android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__))     // 信息日志
#define LOGW(...) (__android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__))     // 警告日志
#define LOGE(...) (__android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__))    // 错误日志

#ifndef NDEBUG
// 调试模式下启用详细日志
#define LOGV(...) (__android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__))  // 详细日志
#define LOGD(...) (__android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__))    // 调试日志
#else
// 发布模式下禁用详细日志
#define LOGV(...) ((void)0)
#define LOGD(...) ((void)0)
#endif

// 日志标签
#define TAG "resetprop"
