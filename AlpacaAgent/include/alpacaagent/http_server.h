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

#include <alpacaagent/run_registry.h>
#include <alpacaagent/socket_utils.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <condition_variable>

namespace alpacaagent {

struct ServerConfig {
    int port = 6810;
    std::size_t thread_pool_size = 4;
};

class HttpServer {
public:
    HttpServer(ServerConfig config, RunRegistry& registry);
    ~HttpServer();

    void start();
    void start_async();
    void stop();
    void wait();

    bool is_running() const { return running_; }

private:
    struct ParsedRequest {
        std::string method;
        std::string path;
        std::string query;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
    };

    struct ParsedPath {
        std::vector<std::string> segments;
        std::unordered_map<std::string, std::string> query;
    };

    struct HttpResponse {
        std::uint16_t status_code = 200;
        std::string reason_phrase = "OK";
        std::string body;
        std::string content_type = "application/json";
    };

    ServerConfig config_;
    RunRegistry& registry_;

    std::atomic<bool> running_{false};
    std::atomic<util::SocketHandle> server_fd_{util::kInvalidSocket};
    std::thread server_thread_;

    std::vector<std::thread> worker_threads_;
    std::queue<util::SocketHandle> connection_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    bool shutdown_workers_ = false;

    void run_server();
    void worker_thread();
    void handle_connection(util::SocketHandle socket_fd);

    static bool read_request(util::SocketHandle socket_fd, ParsedRequest& request);
    static bool parse_request(std::string_view raw, ParsedRequest& request);
    static std::string build_http_response(const HttpResponse& response);

    HttpResponse route(const ParsedRequest& request);

    static ParsedPath split_path_and_query(const std::string& path, const std::string& query);
    static std::string url_decode(const std::string& value);
    static std::optional<std::uint64_t> parse_uint64(const std::string& value);

    static HttpResponse json_response(std::uint16_t status_code,
                                      std::string reason_phrase,
                                      nlohmann::json payload);
};

} // namespace alpacaagent
