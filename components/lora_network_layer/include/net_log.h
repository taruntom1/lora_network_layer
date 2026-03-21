#pragma once

/**
 * @file net_log.h
 * @brief Portable logging macros for the network layer.
 *
 * On ESP32 (ESP-IDF) these delegate to esp_log.h.
 * On all other platforms a simple printf-based fallback is used, with the
 * verbosity controlled by NET_LOG_LEVEL (default 3 = INFO):
 *   0 = silent
 *   1 = errors only
 *   2 = warnings + errors
 *   3 = info + warnings + errors  (default)
 *   4 = debug + all above
 *
 * @note Format strings passed to these macros must not contain a trailing
 *       newline; one is appended automatically on non-ESP platforms.
 */

#ifdef ESP_PLATFORM
#  include "esp_log.h"
#  define NET_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#  define NET_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#  define NET_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  define NET_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#else
#  include <cstdio>
#  ifndef NET_LOG_LEVEL
#    define NET_LOG_LEVEL 3
#  endif
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#  define NET_LOGE(tag, fmt, ...) \
    do { if (NET_LOG_LEVEL >= 1) fprintf(stderr, "[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#  define NET_LOGW(tag, fmt, ...) \
    do { if (NET_LOG_LEVEL >= 2) fprintf(stderr, "[WARN][%s] "  fmt "\n", tag, ##__VA_ARGS__); } while (0)
#  define NET_LOGI(tag, fmt, ...) \
    do { if (NET_LOG_LEVEL >= 3) fprintf(stdout, "[INFO][%s] "  fmt "\n", tag, ##__VA_ARGS__); } while (0)
#  define NET_LOGD(tag, fmt, ...) \
    do { if (NET_LOG_LEVEL >= 4) fprintf(stdout, "[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)
#endif
