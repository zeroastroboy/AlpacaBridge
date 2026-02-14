// AlpacaAgent
// Copyright (c) 2026 Joey Troy and contributors
//
// This file is part of AlpacaAgent.
//
// AlpacaAgent is licensed under the Server Side Public License, Version 1 (SSPL v1).
// See the LICENSE file in this repository or the official license at:
// https://www.mongodb.com/legal/licensing/server-side-public-license
//
// If you use this program to provide a network-accessible service, appliance,
// or any commercial offering, you must comply with all SSPL v1 requirements.

#include <alpacaagent/time_utils.h>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace alpacaagent {

std::string to_utc_iso8601(std::chrono::system_clock::time_point time_point) {
    const std::time_t raw = std::chrono::system_clock::to_time_t(time_point);
    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &raw);
#else
    gmtime_r(&raw, &utc_tm);
#endif

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_point.time_since_epoch())
                            .count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis << 'Z';
    return oss.str();
}

std::int64_t to_unix_millis(std::chrono::system_clock::time_point time_point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               time_point.time_since_epoch())
        .count();
}

} // namespace alpacaagent
