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

#include <alpacaagent/http_server.h>
#include <alpacaagent/run_registry.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

namespace {

std::atomic<bool> g_should_stop{false};

void handle_signal(int) {
    g_should_stop.store(true);
}

struct AppConfig {
    int port = 6810;
    std::size_t thread_pool_size = 4;
    std::chrono::seconds disconnect_threshold = std::chrono::seconds(15);
    std::string persistence_path = "config/agent_state.json";
};

bool load_config_file(const std::string& path, AppConfig& config) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (...) {
        return false;
    }

    if (!json.is_object()) {
        return false;
    }

    if (json.contains("port") && json.at("port").is_number_integer()) {
        config.port = json.at("port").get<int>();
    }
    if (json.contains("threadPoolSize") && json.at("threadPoolSize").is_number_unsigned()) {
        config.thread_pool_size = json.at("threadPoolSize").get<std::size_t>();
    }
    if (json.contains("disconnectThresholdSeconds") && json.at("disconnectThresholdSeconds").is_number_integer()) {
        config.disconnect_threshold = std::chrono::seconds(json.at("disconnectThresholdSeconds").get<int>());
    }
    if (json.contains("persistencePath") && json.at("persistencePath").is_string()) {
        config.persistence_path = json.at("persistencePath").get<std::string>();
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    AppConfig app_config;
    std::string config_path = "agent_config.json";

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            continue;
        }
        if (arg == "--port" && i + 1 < argc) {
            app_config.port = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "--state" && i + 1 < argc) {
            app_config.persistence_path = argv[++i];
            continue;
        }
        if (arg == "--threads" && i + 1 < argc) {
            app_config.thread_pool_size = static_cast<std::size_t>(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--disconnect-threshold" && i + 1 < argc) {
            app_config.disconnect_threshold = std::chrono::seconds(std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--help") {
            std::cout << "AlpacaAgent options:\n"
                      << "  --config <path>               JSON config file (default: agent_config.json)\n"
                      << "  --port <port>                 HTTP port (default: 6810)\n"
                      << "  --state <path>                Persistence file path (default: config/agent_state.json)\n"
                      << "  --threads <count>             Worker thread count (default: 4)\n"
                      << "  --disconnect-threshold <sec>  Disconnect threshold in seconds (default: 15)\n";
            return 0;
        }
    }

    if (std::filesystem::exists(config_path)) {
        load_config_file(config_path, app_config);
    }

    if (app_config.thread_pool_size == 0) {
        app_config.thread_pool_size = 1;
    }

    alpacaagent::RunRegistry registry(app_config.persistence_path, app_config.disconnect_threshold);
    registry.load();

    alpacaagent::HttpServer server(alpacaagent::ServerConfig{app_config.port, app_config.thread_pool_size}, registry);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    server.start_async();

    std::cout << "AlpacaAgent listening on http://localhost:" << app_config.port << "\n";
    std::cout << "Checkpoint endpoint: POST /agent/v1/checkpoints\n";
    std::cout << "NINA compatibility endpoint: POST /agent/nina/checkpoint\n";

    while (!g_should_stop.load() && server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    server.wait();

    return 0;
}
