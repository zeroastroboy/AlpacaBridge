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

#include <alpacaagent/run_registry.h>

#include <alpacaagent/time_utils.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace alpacaagent {

namespace {

std::uint64_t json_uint64_or_zero(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        return signed_value < 0 ? 0 : static_cast<std::uint64_t>(signed_value);
    }
    if (value.is_string()) {
        try {
            std::size_t pos = 0;
            const auto parsed = std::stoull(value.get<std::string>(), &pos);
            if (pos > 0) {
                return parsed;
            }
        } catch (...) {
        }
    }
    return 0;
}

std::int64_t json_int64_or_zero(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_unsigned()) {
        const auto unsigned_value = value.get<std::uint64_t>();
        if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::numeric_limits<std::int64_t>::max();
        }
        return static_cast<std::int64_t>(unsigned_value);
    }
    return 0;
}

std::string json_string_or_empty(const nlohmann::json& value) {
    return value.is_string() ? value.get<std::string>() : std::string();
}

} // namespace

RunRegistry::RunRegistry(std::string persistence_path,
                         std::chrono::seconds disconnect_threshold,
                         std::size_t max_events_per_run)
    : persistence_path_(std::move(persistence_path))
    , disconnect_threshold_(disconnect_threshold)
    , max_events_per_run_(max_events_per_run)
{}

bool RunRegistry::extract_checkpoint_fields(const nlohmann::json& payload,
                                            std::string& run_id,
                                            std::string& state,
                                            std::uint64_t& checkpoint_no,
                                            std::string& error) {
    if (!payload.is_object()) {
        error = "Checkpoint payload must be a JSON object";
        return false;
    }

    if (!payload.contains("runId") || !payload.at("runId").is_string()) {
        error = "Missing required field: runId";
        return false;
    }
    run_id = payload.at("runId").get<std::string>();
    if (run_id.empty()) {
        error = "runId cannot be empty";
        return false;
    }

    if (!payload.contains("checkpointNo")) {
        error = "Missing required field: checkpointNo";
        return false;
    }
    checkpoint_no = json_uint64_or_zero(payload.at("checkpointNo"));
    if (checkpoint_no == 0) {
        error = "checkpointNo must be a positive integer";
        return false;
    }

    if (!payload.contains("state") || !payload.at("state").is_string()) {
        error = "Missing required field: state";
        return false;
    }
    state = payload.at("state").get<std::string>();
    if (state.empty()) {
        error = "state cannot be empty";
        return false;
    }

    return true;
}

std::string RunRegistry::infer_client_type(const nlohmann::json& payload,
                                           const std::string& route_client_hint) {
    const auto environment_it = payload.find("environment");
    if (environment_it != payload.end() && environment_it->is_object()) {
        const auto client_type_it = environment_it->find("clientType");
        if (client_type_it != environment_it->end() && client_type_it->is_string()) {
            const auto value = client_type_it->get<std::string>();
            if (!value.empty()) {
                return value;
            }
        }

        const auto client_name_it = environment_it->find("clientName");
        if (client_name_it != environment_it->end() && client_name_it->is_string()) {
            const auto value = client_name_it->get<std::string>();
            if (!value.empty()) {
                return value;
            }
        }
    }

    if (!route_client_hint.empty()) {
        return route_client_hint;
    }

    return "unknown";
}

void RunRegistry::append_event_locked(RunRecord& run,
                                      std::string type,
                                      std::string timestamp_utc,
                                      nlohmann::json payload) {
    EventRecord record;
    record.id = next_event_id_++;
    record.type = std::move(type);
    record.timestamp_utc = std::move(timestamp_utc);
    record.payload = std::move(payload);

    run.events.push_back(std::move(record));
    if (run.events.size() > max_events_per_run_) {
        run.events.erase(run.events.begin(), run.events.begin() + static_cast<std::ptrdiff_t>(run.events.size() - max_events_per_run_));
    }
}

bool RunRegistry::refresh_disconnect_state_locked(std::chrono::system_clock::time_point now) {
    bool changed = false;
    const auto now_ms = to_unix_millis(now);
    const auto threshold_ms = std::chrono::duration_cast<std::chrono::milliseconds>(disconnect_threshold_).count();
    const auto now_utc = to_utc_iso8601(now);

    for (auto& [_, run] : runs_) {
        if (!run.client_connected) {
            continue;
        }
        if (run.last_seen_unix_ms <= 0) {
            continue;
        }
        if ((now_ms - run.last_seen_unix_ms) <= threshold_ms) {
            continue;
        }

        run.client_connected = false;
        run.updated_at_utc = now_utc;

        append_event_locked(run,
                            "ClientDisconnected",
                            now_utc,
                            nlohmann::json{
                                {"lastSeenUnixMs", run.last_seen_unix_ms},
                                {"disconnectThresholdSeconds", disconnect_threshold_.count()},
                                {"lastClientType", run.last_client_type}});
        changed = true;
    }

    return changed;
}

nlohmann::json RunRegistry::run_summary_json(const RunRecord& run) const {
    return nlohmann::json{
        {"runId", run.run_id},
        {"sequenceName", run.sequence_name},
        {"state", run.state},
        {"lastCheckpointNo", run.last_checkpoint_no},
        {"lastSeenUtc", run.updated_at_utc},
        {"lastClientTimestampUtc", run.last_client_timestamp_utc},
        {"lastClientType", run.last_client_type},
        {"clientConnected", run.client_connected}};
}

nlohmann::json RunRegistry::run_detail_json(const RunRecord& run) const {
    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : run.events) {
        events.push_back({
            {"id", event.id},
            {"type", event.type},
            {"timestampUtc", event.timestamp_utc},
            {"payload", event.payload},
        });
    }

    return nlohmann::json{
        {"runId", run.run_id},
        {"sequenceName", run.sequence_name},
        {"state", run.state},
        {"createdAtUtc", run.created_at_utc},
        {"lastSeenUtc", run.updated_at_utc},
        {"lastCheckpointNo", run.last_checkpoint_no},
        {"lastClientTimestampUtc", run.last_client_timestamp_utc},
        {"lastClientType", run.last_client_type},
        {"clientConnected", run.client_connected},
        {"lastCheckpoint", run.last_checkpoint},
        {"events", std::move(events)},
    };
}

nlohmann::json RunRegistry::accept_checkpoint(const nlohmann::json& payload,
                                              const std::string& route_client_hint) {
    std::string run_id;
    std::string state;
    std::uint64_t checkpoint_no = 0;
    std::string validation_error;
    if (!extract_checkpoint_fields(payload, run_id, state, checkpoint_no, validation_error)) {
        return nlohmann::json{
            {"accepted", false},
            {"error", validation_error},
        };
    }

    const auto now = std::chrono::system_clock::now();
    const auto now_utc = to_utc_iso8601(now);
    const auto now_unix_ms = to_unix_millis(now);

    std::lock_guard<std::mutex> lock(mutex_);

    refresh_disconnect_state_locked(now);

    auto [it, inserted] = runs_.try_emplace(run_id);
    RunRecord& run = it->second;
    if (inserted) {
        run.run_id = run_id;
        run.created_at_utc = now_utc;
        run.updated_at_utc = now_utc;
        append_event_locked(run,
                            "RunStarted",
                            now_utc,
                            nlohmann::json{{"routeClientHint", route_client_hint}});
    }

    const std::string client_type = infer_client_type(payload, route_client_hint);

    if (!run.client_connected) {
        run.client_connected = true;
        append_event_locked(run,
                            "ClientReconnected",
                            now_utc,
                            nlohmann::json{{"clientType", client_type}});
    }

    run.last_seen_unix_ms = now_unix_ms;
    run.updated_at_utc = now_utc;
    run.last_client_type = client_type;

    if (payload.contains("timestampUtc") && payload.at("timestampUtc").is_string()) {
        run.last_client_timestamp_utc = payload.at("timestampUtc").get<std::string>();
    }

    if (payload.contains("sequenceName") && payload.at("sequenceName").is_string()) {
        run.sequence_name = payload.at("sequenceName").get<std::string>();
    }

    const bool is_out_of_order = checkpoint_no <= run.last_checkpoint_no;
    if (is_out_of_order) {
        append_event_locked(run,
                            "CheckpointIgnoredOutOfOrder",
                            now_utc,
                            nlohmann::json{
                                {"receivedCheckpointNo", checkpoint_no},
                                {"lastCheckpointNo", run.last_checkpoint_no},
                                {"clientType", client_type}});
        save_locked();
        return nlohmann::json{
            {"accepted", true},
            {"ignored", true},
            {"reason", "out_of_order_checkpoint"},
            {"serverTimeUtc", now_utc},
            {"runState", run_summary_json(run)}};
    }

    run.last_checkpoint_no = checkpoint_no;
    run.state = state;
    run.last_checkpoint = payload;

    append_event_locked(run,
                        "CheckpointReceived",
                        now_utc,
                        nlohmann::json{
                            {"checkpointNo", checkpoint_no},
                            {"state", state},
                            {"clientType", client_type}});

    save_locked();

    return nlohmann::json{
        {"accepted", true},
        {"ignored", false},
        {"serverTimeUtc", now_utc},
        {"runState", run_summary_json(run)}};
}

nlohmann::json RunRegistry::list_runs() {
    const auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = refresh_disconnect_state_locked(now);
    if (changed) {
        save_locked();
    }

    nlohmann::json runs_json = nlohmann::json::array();
    for (const auto& [_, run] : runs_) {
        runs_json.push_back(run_summary_json(run));
    }

    return nlohmann::json{
        {"runs", std::move(runs_json)},
        {"count", runs_.size()},
        {"serverTimeUtc", to_utc_iso8601(now)}};
}

std::optional<nlohmann::json> RunRegistry::get_run(const std::string& run_id) {
    const auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = refresh_disconnect_state_locked(now);
    if (changed) {
        save_locked();
    }

    const auto it = runs_.find(run_id);
    if (it == runs_.end()) {
        return std::nullopt;
    }

    return run_detail_json(it->second);
}

std::optional<nlohmann::json> RunRegistry::get_events(const std::string& run_id,
                                                      std::optional<std::uint64_t> since_id) {
    const auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = refresh_disconnect_state_locked(now);
    if (changed) {
        save_locked();
    }

    const auto it = runs_.find(run_id);
    if (it == runs_.end()) {
        return std::nullopt;
    }

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : it->second.events) {
        if (since_id.has_value() && event.id <= since_id.value()) {
            continue;
        }
        events.push_back({
            {"id", event.id},
            {"type", event.type},
            {"timestampUtc", event.timestamp_utc},
            {"payload", event.payload},
        });
    }

    return nlohmann::json{
        {"runId", run_id},
        {"events", std::move(events)},
        {"serverTimeUtc", to_utc_iso8601(now)},
    };
}

bool RunRegistry::save_locked() const {
    try {
        const std::filesystem::path persist_path(persistence_path_);
        if (persist_path.has_parent_path()) {
            std::filesystem::create_directories(persist_path.parent_path());
        }

        nlohmann::json runs_json = nlohmann::json::array();
        for (const auto& [_, run] : runs_) {
            nlohmann::json events_json = nlohmann::json::array();
            for (const auto& event : run.events) {
                events_json.push_back({
                    {"id", event.id},
                    {"type", event.type},
                    {"timestampUtc", event.timestamp_utc},
                    {"payload", event.payload},
                });
            }

            runs_json.push_back({
                {"runId", run.run_id},
                {"createdAtUtc", run.created_at_utc},
                {"updatedAtUtc", run.updated_at_utc},
                {"lastSeenUnixMs", run.last_seen_unix_ms},
                {"sequenceName", run.sequence_name},
                {"state", run.state},
                {"lastCheckpointNo", run.last_checkpoint_no},
                {"lastClientTimestampUtc", run.last_client_timestamp_utc},
                {"lastClientType", run.last_client_type},
                {"clientConnected", run.client_connected},
                {"lastCheckpoint", run.last_checkpoint},
                {"events", std::move(events_json)},
            });
        }

        const nlohmann::json document{
            {"schemaVersion", 1},
            {"savedAtUtc", to_utc_iso8601(std::chrono::system_clock::now())},
            {"disconnectThresholdSeconds", disconnect_threshold_.count()},
            {"nextEventId", next_event_id_},
            {"runs", std::move(runs_json)},
        };

        const std::filesystem::path temp_path = persist_path.string() + ".tmp";
        {
            std::ofstream temp_output(temp_path, std::ios::trunc);
            if (!temp_output.is_open()) {
                return false;
            }
            temp_output << document.dump(2);
        }

        std::error_code rename_error;
        std::filesystem::rename(temp_path, persist_path, rename_error);
        if (rename_error) {
            std::error_code remove_error;
            std::filesystem::remove(persist_path, remove_error);
            rename_error.clear();
            std::filesystem::rename(temp_path, persist_path, rename_error);
            if (rename_error) {
                return false;
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool RunRegistry::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::filesystem::path persist_path(persistence_path_);
    if (!std::filesystem::exists(persist_path)) {
        return true;
    }

    std::ifstream input(persist_path);
    if (!input.is_open()) {
        return false;
    }

    nlohmann::json document;
    try {
        input >> document;
    } catch (...) {
        return false;
    }

    if (!document.is_object() || !document.contains("runs") || !document.at("runs").is_array()) {
        return false;
    }

    runs_.clear();
    next_event_id_ = json_uint64_or_zero(document.value("nextEventId", 1));
    if (next_event_id_ == 0) {
        next_event_id_ = 1;
    }

    for (const auto& run_json : document.at("runs")) {
        if (!run_json.is_object()) {
            continue;
        }

        RunRecord run;
        run.run_id = json_string_or_empty(run_json.value("runId", nlohmann::json("")));
        if (run.run_id.empty()) {
            continue;
        }

        run.created_at_utc = json_string_or_empty(run_json.value("createdAtUtc", nlohmann::json("")));
        run.updated_at_utc = json_string_or_empty(run_json.value("updatedAtUtc", nlohmann::json("")));
        run.last_seen_unix_ms = json_int64_or_zero(run_json.value("lastSeenUnixMs", nlohmann::json(0)));
        run.sequence_name = json_string_or_empty(run_json.value("sequenceName", nlohmann::json("")));
        run.state = json_string_or_empty(run_json.value("state", nlohmann::json("")));
        run.last_checkpoint_no = json_uint64_or_zero(run_json.value("lastCheckpointNo", nlohmann::json(0)));
        run.last_client_timestamp_utc = json_string_or_empty(run_json.value("lastClientTimestampUtc", nlohmann::json("")));
        run.last_client_type = json_string_or_empty(run_json.value("lastClientType", nlohmann::json("")));
        run.client_connected = run_json.value("clientConnected", true);

        if (run_json.contains("lastCheckpoint")) {
            run.last_checkpoint = run_json.at("lastCheckpoint");
        }

        if (run_json.contains("events") && run_json.at("events").is_array()) {
            for (const auto& event_json : run_json.at("events")) {
                if (!event_json.is_object()) {
                    continue;
                }
                EventRecord event;
                event.id = json_uint64_or_zero(event_json.value("id", nlohmann::json(0)));
                event.type = json_string_or_empty(event_json.value("type", nlohmann::json("")));
                event.timestamp_utc = json_string_or_empty(event_json.value("timestampUtc", nlohmann::json("")));
                if (event_json.contains("payload")) {
                    event.payload = event_json.at("payload");
                }
                if (event.id > 0) {
                    run.events.push_back(std::move(event));
                }
            }
        }

        runs_.emplace(run.run_id, std::move(run));
    }

    return true;
}

} // namespace alpacaagent
