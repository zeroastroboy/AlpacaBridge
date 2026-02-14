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

#pragma once

#include <chrono>
#include <string>

namespace alpacaagent {

std::string to_utc_iso8601(std::chrono::system_clock::time_point time_point);
std::int64_t to_unix_millis(std::chrono::system_clock::time_point time_point);

} // namespace alpacaagent
