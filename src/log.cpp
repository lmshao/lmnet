//
// Copyright © 2020-2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "log.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace Network {

std::string Time()
{
    thread_local static auto last_sec = std::chrono::seconds{0};
    thread_local static char cached_result[24] = {0};
    thread_local static size_t base_len = 0;

    auto now = std::chrono::system_clock::now();
    auto current_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    if (current_sec != last_sec) {
        auto tt = std::chrono::system_clock::to_time_t(now);

        struct tm tm_buf;

        localtime_r(&tt, &tm_buf);

        base_len =
            snprintf(cached_result, sizeof(cached_result), "%04d-%02d-%02d %02d:%02d:%02d.", tm_buf.tm_year + 1900,
                     tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

        last_sec = current_sec;
    }

    snprintf(cached_result + base_len, 4, "%03d", (int)ms);

    return std::string(cached_result);
}

} // namespace Network