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
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace alpacaagent {

struct RunControlPolicy {
    bool auto_pause_on_disconnect = true;
    bool auto_resume_on_reconnect = true;
    std::chrono::seconds hold_engage_after_disconnect = std::chrono::seconds(10);
};

class RunRegistry {
public:
    RunRegistry(std::string persistence_path,
                std::chrono::seconds disconnect_threshold,
                RunControlPolicy control_policy = {},
                std::size_t max_events_per_run = 2048);

    // Accepts checkpoints from any client (NINA/SGP/other).
    nlohmann::json accept_checkpoint(const nlohmann::json& payload,
                                     const std::string& route_client_hint = "");

    nlohmann::json list_runs();
    std::optional<nlohmann::json> get_run(const std::string& run_id);
    std::optional<nlohmann::json> get_events(const std::string& run_id, std::optional<std::uint64_t> since_id);
    nlohmann::json apply_run_action(const std::string& run_id, const nlohmann::json& action_payload);

    bool load();

private:
    struct EventRecord {
        std::uint64_t id = 0;
        std::string type;
        std::string timestamp_utc;
        nlohmann::json payload;
    };

    struct RunRecord {
        std::string run_id;
        std::string created_at_utc;
        std::string updated_at_utc;
        std::int64_t last_seen_unix_ms = 0;
        std::int64_t disconnected_at_unix_ms = 0;
        std::string sequence_name;
        std::string state;
        std::uint64_t last_checkpoint_no = 0;
        std::string last_client_timestamp_utc;
        std::string last_client_type;
        std::string control_state = "None";
        bool hold_engaged = false;
        nlohmann::json last_checkpoint;
        bool client_connected = true;
        std::vector<EventRecord> events;
    };

    std::string persistence_path_;
    std::chrono::seconds disconnect_threshold_;
    RunControlPolicy control_policy_;
    std::size_t max_events_per_run_;
    std::uint64_t next_event_id_ = 1;

    std::unordered_map<std::string, RunRecord> runs_;
    mutable std::mutex mutex_;

    static bool extract_checkpoint_fields(const nlohmann::json& payload,
                                          std::string& run_id,
                                          std::string& state,
                                          std::uint64_t& checkpoint_no,
                                          std::string& error);
    static std::string infer_client_type(const nlohmann::json& payload,
                                         const std::string& route_client_hint);

    bool refresh_disconnect_state_locked(std::chrono::system_clock::time_point now);
    bool maybe_engage_hold_locked(RunRecord& run, std::int64_t now_unix_ms, const std::string& now_utc);
    void set_control_state_locked(RunRecord& run,
                                  std::string new_control_state,
                                  std::string reason,
                                  const std::string& timestamp_utc,
                                  nlohmann::json extra_payload = nlohmann::json::object());
    void append_event_locked(RunRecord& run,
                             std::string type,
                             std::string timestamp_utc,
                             nlohmann::json payload);
    nlohmann::json run_summary_json(const RunRecord& run) const;
    nlohmann::json run_detail_json(const RunRecord& run) const;

    bool save_locked() const;
};

} // namespace alpacaagent
