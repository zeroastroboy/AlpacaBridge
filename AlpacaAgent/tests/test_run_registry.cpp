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

#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

int main() {
    const auto state_path = (std::filesystem::temp_directory_path() / "alpacaagent_test_state.json").string();
    {
        std::error_code ec;
        std::filesystem::remove(state_path, ec);
    }

    alpacaagent::RunControlPolicy policy;
    policy.auto_pause_on_disconnect = true;
    policy.auto_resume_on_reconnect = true;
    policy.hold_engage_after_disconnect = std::chrono::seconds(0);

    alpacaagent::RunRegistry registry(state_path, std::chrono::seconds(1), policy, 128);

    const nlohmann::json checkpoint_one{
        {"apiVersion", "1.0"},
        {"runId", "run-1"},
        {"sequenceName", "M42"},
        {"timestampUtc", "2026-02-14T18:12:33Z"},
        {"checkpointNo", 1},
        {"state", "Running"},
        {"environment", {{"clientName", "NINA"}}},
    };

    const auto response_one = registry.accept_checkpoint(checkpoint_one, "nina");
    assert(response_one.value("accepted", false));
    assert(!response_one.value("ignored", true));

    const auto runs_one = registry.list_runs();
    assert(runs_one.value("count", 0) == 1);
    assert(runs_one.at("runs").at(0).value("lastCheckpointNo", 0) == 1);

    const auto response_duplicate = registry.accept_checkpoint(checkpoint_one, "nina");
    assert(response_duplicate.value("accepted", false));
    assert(response_duplicate.value("ignored", false));

    const nlohmann::json checkpoint_two{
        {"apiVersion", "1.0"},
        {"runId", "run-1"},
        {"sequenceName", "M42"},
        {"timestampUtc", "2026-02-14T18:15:00Z"},
        {"checkpointNo", 2},
        {"state", "Running"},
        {"environment", {{"clientName", "SGP Pro"}, {"clientType", "SGP"}}},
    };

    const auto response_two = registry.accept_checkpoint(checkpoint_two, "sgppro");
    assert(response_two.value("accepted", false));
    assert(response_two.at("runState").value("lastCheckpointNo", 0) == 2);
    assert(response_two.at("runState").value("lastClientType", std::string()) == "SGP");

    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    const auto runs_after_timeout = registry.list_runs();
    assert(!runs_after_timeout.at("runs").at(0).value("clientConnected", true));
    assert(runs_after_timeout.at("runs").at(0).value("controlState", std::string()) == "PauseRequested");
    assert(runs_after_timeout.at("runs").at(0).value("holdEngaged", true));

    const auto manual_resume = registry.apply_run_action(
        "run-1",
        nlohmann::json{{"action", "ResumeRequested"}, {"source", "test"}, {"reason", "manual"}});
    assert(manual_resume.value("accepted", false));
    assert(manual_resume.at("runState").value("controlState", std::string()) == "ResumeRequested");

    const auto ack = registry.apply_run_action(
        "run-1",
        nlohmann::json{{"action", "AcknowledgeControl"}, {"source", "test"}});
    assert(ack.value("accepted", false));
    assert(ack.at("runState").value("controlState", std::string()) == "None");

    const auto events = registry.get_events("run-1", std::nullopt);
    assert(events.has_value());
    assert(events->at("events").is_array());
    assert(!events->at("events").empty());

    {
        std::error_code ec;
        std::filesystem::remove(state_path, ec);
    }

    return 0;
}
