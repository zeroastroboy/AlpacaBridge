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

#include <alpacaagent/time_utils.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>

namespace alpacaagent {

namespace {

constexpr std::size_t kMaxRequestBytes = 2 * 1024 * 1024;
constexpr int kRequestBufferBytes = 8192;

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void trim_ascii_whitespace_in_place(std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    const auto last = value.find_last_not_of(" \t");
    value = value.substr(first, last - first + 1);
}

std::optional<std::size_t> parse_content_length_from_headers(std::string_view headers_blob) {
    std::istringstream stream{std::string(headers_blob)};
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        std::string key = to_lower_copy(line.substr(0, colon_pos));
        std::string value = line.substr(colon_pos + 1);
        trim_ascii_whitespace_in_place(value);
        if (key == "content-length") {
            try {
                std::size_t pos = 0;
                const auto parsed = std::stoull(value, &pos);
                if (pos == value.size()) {
                    return static_cast<std::size_t>(parsed);
                }
            } catch (...) {
                return std::nullopt;
            }
            return std::nullopt;
        }
    }

    return std::size_t{0};
}

std::string reason_for_status(std::uint16_t status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

} // namespace

HttpServer::HttpServer(ServerConfig config, RunRegistry& registry)
    : config_(config)
    , registry_(registry)
{}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    run_server();
}

void HttpServer::start_async() {
    if (running_) {
        return;
    }
    running_ = true;
    server_thread_ = std::thread(&HttpServer::run_server, this);
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        shutdown_workers_ = true;
    }
    queue_condition_.notify_all();

    auto fd = server_fd_.exchange(util::kInvalidSocket);
    if (fd != util::kInvalidSocket) {
        util::socket_shutdown(fd);
        util::socket_close(fd);
    }

    const auto current_thread = std::this_thread::get_id();

    for (auto& worker : worker_threads_) {
        if (!worker.joinable()) {
            continue;
        }
        if (worker.get_id() == current_thread) {
            worker.detach();
            continue;
        }
        worker.join();
    }
    worker_threads_.clear();

    if (server_thread_.joinable()) {
        if (server_thread_.get_id() == current_thread) {
            server_thread_.detach();
        } else {
            server_thread_.join();
        }
    }
}

void HttpServer::wait() {
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::run_server() {
    util::ensure_winsock();

    util::SocketHandle server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == util::kInvalidSocket) {
        running_ = false;
        return;
    }

    server_fd_.store(server_fd);

    int opt = 1;
    const char* opt_ptr = reinterpret_cast<const char*>(&opt);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, opt_ptr, sizeof(opt));

    if (config_.port < 0 || config_.port > static_cast<int>(std::numeric_limits<u_short>::max())) {
        util::socket_close(server_fd);
        server_fd_.store(util::kInvalidSocket);
        running_ = false;
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<u_short>(config_.port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        util::socket_close(server_fd);
        server_fd_.store(util::kInvalidSocket);
        running_ = false;
        return;
    }

    if (listen(server_fd, 16) < 0) {
        util::socket_close(server_fd);
        server_fd_.store(util::kInvalidSocket);
        running_ = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        shutdown_workers_ = false;
        while (!connection_queue_.empty()) {
            connection_queue_.pop();
        }
    }

    worker_threads_.reserve(config_.thread_pool_size);
    for (std::size_t i = 0; i < config_.thread_pool_size; ++i) {
        worker_threads_.emplace_back(&HttpServer::worker_thread, this);
    }

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;

        const int select_result = util::socket_select(server_fd, &read_fds, nullptr, nullptr, &timeout);
        if (select_result < 0) {
            const int err = util::socket_get_last_error();
            if (util::socket_interrupted(err)) {
                continue;
            }
            if (util::socket_bad_descriptor(err) || util::socket_not_socket(err)) {
                break;
            }
            continue;
        }

        if (select_result == 0) {
            continue;
        }

        if (!FD_ISSET(server_fd, &read_fds)) {
            continue;
        }

        sockaddr_in client_address{};
        util::SocketLen client_len = sizeof(client_address);
        const util::SocketHandle client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_len);
        if (client_fd == util::kInvalidSocket) {
            const int err = util::socket_get_last_error();
            if (util::socket_interrupted(err) || util::socket_would_block(err)) {
                continue;
            }
            if (util::socket_bad_descriptor(err) || util::socket_not_socket(err)) {
                break;
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            connection_queue_.push(client_fd);
        }
        queue_condition_.notify_one();
    }

    auto fd = server_fd_.exchange(util::kInvalidSocket);
    if (fd != util::kInvalidSocket) {
        util::socket_close(fd);
    }

    running_ = false;
}

void HttpServer::worker_thread() {
    while (true) {
        util::SocketHandle client_fd = util::kInvalidSocket;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_condition_.wait(lock, [this] {
                return !connection_queue_.empty() || shutdown_workers_;
            });

            if (shutdown_workers_ && connection_queue_.empty()) {
                break;
            }

            if (!connection_queue_.empty()) {
                client_fd = connection_queue_.front();
                connection_queue_.pop();
            }
        }

        if (client_fd == util::kInvalidSocket) {
            continue;
        }

        handle_connection(client_fd);
        util::socket_close(client_fd);
    }
}

void HttpServer::handle_connection(util::SocketHandle socket_fd) {
    ParsedRequest request;
    if (!read_request(socket_fd, request)) {
        const auto response = json_response(400, "Bad Request", nlohmann::json{{"error", "invalid_http_request"}});
        const auto response_str = build_http_response(response);
        util::socket_send(socket_fd, response_str.c_str(), static_cast<int>(response_str.size()));
        return;
    }

    const auto response = route(request);
    const auto response_str = build_http_response(response);
    util::socket_send(socket_fd, response_str.c_str(), static_cast<int>(response_str.size()));
}

bool HttpServer::read_request(util::SocketHandle socket_fd, ParsedRequest& request) {
    std::string raw;
    raw.reserve(4096);

    std::size_t header_end_pos = std::string::npos;
    std::size_t content_length = 0;

    while (true) {
        char buffer[kRequestBufferBytes]{};
        const int bytes_read = util::socket_recv(socket_fd, buffer, kRequestBufferBytes);
        if (bytes_read <= 0) {
            return false;
        }

        raw.append(buffer, static_cast<std::size_t>(bytes_read));
        if (raw.size() > kMaxRequestBytes) {
            return false;
        }

        if (header_end_pos == std::string::npos) {
            header_end_pos = raw.find("\r\n\r\n");
            if (header_end_pos != std::string::npos) {
                const auto headers_blob = std::string_view(raw.data(), header_end_pos + 4);
                const auto parsed_length = parse_content_length_from_headers(headers_blob);
                if (!parsed_length.has_value()) {
                    return false;
                }
                content_length = parsed_length.value();
            }
        }

        if (header_end_pos != std::string::npos) {
            const std::size_t total_required = header_end_pos + 4 + content_length;
            if (raw.size() >= total_required) {
                raw.resize(total_required);
                break;
            }
        }
    }

    return parse_request(raw, request);
}

bool HttpServer::parse_request(std::string_view raw, ParsedRequest& request) {
    const std::size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return false;
    }

    std::string request_line(raw.substr(0, line_end));
    std::istringstream line_stream(request_line);

    std::string method;
    std::string path_and_query;
    std::string version;
    if (!(line_stream >> method >> path_and_query >> version)) {
        return false;
    }

    request.method = method;

    const std::size_t query_pos = path_and_query.find('?');
    if (query_pos == std::string::npos) {
        request.path = path_and_query;
    } else {
        request.path = path_and_query.substr(0, query_pos);
        request.query = path_and_query.substr(query_pos + 1);
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    std::size_t cursor = line_end + 2;
    while (cursor < header_end) {
        const std::size_t next = raw.find("\r\n", cursor);
        if (next == std::string::npos || next > header_end) {
            break;
        }

        if (next == cursor) {
            break;
        }

        std::string header_line(raw.substr(cursor, next - cursor));
        const auto colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = to_lower_copy(header_line.substr(0, colon_pos));
            std::string value = header_line.substr(colon_pos + 1);
            trim_ascii_whitespace_in_place(value);
            request.headers[key] = value;
        }

        cursor = next + 2;
    }

    const std::size_t body_start = header_end + 4;
    if (body_start < raw.size()) {
        request.body = std::string(raw.substr(body_start));
    } else {
        request.body.clear();
    }

    return true;
}

std::string HttpServer::build_http_response(const HttpResponse& response) {
    std::ostringstream output;
    const std::string reason = response.reason_phrase.empty()
                                   ? reason_for_status(response.status_code)
                                   : response.reason_phrase;

    output << "HTTP/1.1 " << response.status_code << ' ' << reason << "\r\n";
    output << "Content-Type: " << response.content_type << "\r\n";
    output << "Content-Length: " << response.body.size() << "\r\n";
    output << "Connection: close\r\n";
    output << "\r\n";
    output << response.body;
    return output.str();
}

HttpServer::HttpResponse HttpServer::json_response(std::uint16_t status_code,
                                                   std::string reason_phrase,
                                                   nlohmann::json payload) {
    HttpResponse response;
    response.status_code = status_code;
    response.reason_phrase = std::move(reason_phrase);
    response.body = payload.dump();
    response.content_type = "application/json";
    return response;
}

HttpServer::ParsedPath HttpServer::split_path_and_query(const std::string& path, const std::string& query) {
    ParsedPath parsed;

    std::stringstream path_stream(path);
    std::string segment;
    while (std::getline(path_stream, segment, '/')) {
        if (!segment.empty()) {
            parsed.segments.push_back(url_decode(segment));
        }
    }

    std::stringstream query_stream(query);
    std::string item;
    while (std::getline(query_stream, item, '&')) {
        if (item.empty()) {
            continue;
        }

        const std::size_t equal_pos = item.find('=');
        if (equal_pos == std::string::npos) {
            parsed.query[url_decode(item)] = "";
            continue;
        }

        parsed.query[url_decode(item.substr(0, equal_pos))] =
            url_decode(item.substr(equal_pos + 1));
    }

    return parsed;
}

std::string HttpServer::url_decode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            try {
                const char character = static_cast<char>(std::stoi(hex, nullptr, 16));
                decoded.push_back(character);
                i += 2;
            } catch (...) {
                decoded.push_back(value[i]);
            }
            continue;
        }

        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }

        decoded.push_back(value[i]);
    }

    return decoded;
}

std::optional<std::uint64_t> HttpServer::parse_uint64(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t pos = 0;
        const auto parsed = std::stoull(value, &pos);
        if (pos != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

HttpServer::HttpResponse HttpServer::route(const ParsedRequest& request) {
    const auto parsed_path = split_path_and_query(request.path, request.query);

    if (request.method == "GET" && request.path == "/") {
        return json_response(
            200,
            "OK",
            nlohmann::json{
                {"service", "AlpacaAgent"},
                {"version", ALPACAAGENT_VERSION},
                {"statusEndpoint", "/health"},
                {"capabilitiesEndpoint", "/agent/v1/capabilities"},
                {"checkpointEndpoint", "/agent/v1/checkpoints"},
                {"runsEndpoint", "/agent/runs"}});
    }

    if (request.method == "GET" && request.path == "/health") {
        return json_response(200,
                             "OK",
                             nlohmann::json{{"ok", true},
                                            {"service", "AlpacaAgent"},
                                            {"version", ALPACAAGENT_VERSION},
                                            {"timeUtc", to_utc_iso8601(std::chrono::system_clock::now())}});
    }

    if (request.method == "GET" && request.path == "/agent/v1/capabilities") {
        return json_response(
            200,
            "OK",
            nlohmann::json{{"checkpointEndpoint", "/agent/v1/checkpoints"},
                           {"checkpointAliases", nlohmann::json::array({"/agent/nina/checkpoint", "/agent/{clientType}/checkpoint"})},
                           {"runActionEndpointTemplate", "/agent/runs/{runId}/action"},
                           {"supportsClientAgnosticRuns", true},
                           {"supportsEvents", true},
                           {"supportsRunControl", true}});
    }

    const bool is_canonical_checkpoint =
        request.method == "POST" && parsed_path.segments.size() == 3 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "v1" &&
        parsed_path.segments[2] == "checkpoints";

    const bool is_nina_checkpoint =
        request.method == "POST" && parsed_path.segments.size() == 3 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "nina" &&
        parsed_path.segments[2] == "checkpoint";

    const bool is_generic_checkpoint_alias =
        request.method == "POST" && parsed_path.segments.size() == 3 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[2] == "checkpoint";

    if (is_canonical_checkpoint || is_nina_checkpoint || is_generic_checkpoint_alias) {
        std::string route_client_hint;
        if (is_nina_checkpoint) {
            route_client_hint = "NINA";
        } else if (is_generic_checkpoint_alias && parsed_path.segments[1] != "v1") {
            route_client_hint = parsed_path.segments[1];
        }

        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(request.body);
        } catch (...) {
            return json_response(400, "Bad Request", nlohmann::json{{"accepted", false}, {"error", "invalid_json"}});
        }

        auto result = registry_.accept_checkpoint(payload, route_client_hint);
        const bool accepted = result.value("accepted", false);
        return json_response(accepted ? 200 : 400, accepted ? "OK" : "Bad Request", std::move(result));
    }

    if (request.method == "GET" && parsed_path.segments.size() == 2 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "runs") {
        return json_response(200, "OK", registry_.list_runs());
    }

    if (request.method == "GET" && parsed_path.segments.size() == 3 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "runs") {
        const auto run_json = registry_.get_run(parsed_path.segments[2]);
        if (!run_json.has_value()) {
            return json_response(404, "Not Found", nlohmann::json{{"error", "run_not_found"}});
        }
        return json_response(200, "OK", run_json.value());
    }

    if (request.method == "GET" && parsed_path.segments.size() == 4 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "runs" &&
        parsed_path.segments[3] == "events") {
        std::optional<std::uint64_t> since;
        if (const auto it = parsed_path.query.find("since"); it != parsed_path.query.end()) {
            since = parse_uint64(it->second);
            if (!since.has_value()) {
                return json_response(400, "Bad Request", nlohmann::json{{"error", "invalid_since"}});
            }
        }

        const auto events_json = registry_.get_events(parsed_path.segments[2], since);
        if (!events_json.has_value()) {
            return json_response(404, "Not Found", nlohmann::json{{"error", "run_not_found"}});
        }

        return json_response(200, "OK", events_json.value());
    }

    if (request.method == "POST" && parsed_path.segments.size() == 4 &&
        parsed_path.segments[0] == "agent" && parsed_path.segments[1] == "runs" &&
        parsed_path.segments[3] == "action") {
        nlohmann::json payload;
        try {
            payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
        } catch (...) {
            return json_response(400, "Bad Request", nlohmann::json{{"accepted", false}, {"error", "invalid_json"}});
        }

        auto result = registry_.apply_run_action(parsed_path.segments[2], payload);
        const bool accepted = result.value("accepted", false);
        const std::string error = result.value("error", std::string{});
        const auto status = accepted ? 200 : (error == "run_not_found" ? 404 : 400);
        return json_response(status, accepted ? "OK" : "Bad Request", std::move(result));
    }

    if (request.method != "GET" && request.method != "POST") {
        return json_response(405, "Method Not Allowed", nlohmann::json{{"error", "method_not_allowed"}});
    }

    return json_response(404, "Not Found", nlohmann::json{{"error", "not_found"}});
}

} // namespace alpacaagent
