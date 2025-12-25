#pragma once

#include "pattern_trie.hpp"
#include "json_utils.hpp"

#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace prefix_match {

class Server {
public:
    Server(PatternTrie& trie, const CallerArgs& args, int num_threads)
        : trie_(trie), args_(args), num_threads_(num_threads), running_(false),
          listen_fd_(-1), is_unix_socket_(false) {}

    ~Server() {
        stop();
    }

    // Start server on TCP port
    bool start_tcp(int port) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "Error creating TCP socket\n";
            return false;
        }

        // Allow address reuse
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Error binding to port " << port << "\n";
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (listen(listen_fd_, 50) < 0) {
            std::cerr << "Error listening on socket\n";
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        is_unix_socket_ = false;
        std::cerr << "Server listening on TCP port " << port << "\n";
        return start_accept_loop();
    }

    // Start server on Unix domain socket
    bool start_unix(const std::string& path) {
        // Remove existing socket file
        unlink(path.c_str());

        listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "Error creating Unix socket\n";
            return false;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Error binding to " << path << "\n";
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (listen(listen_fd_, 50) < 0) {
            std::cerr << "Error listening on socket\n";
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        is_unix_socket_ = true;
        socket_path_ = path;
        std::cerr << "Server listening on Unix socket " << path << "\n";
        return start_accept_loop();
    }

    void stop() {
        running_ = false;
        if (listen_fd_ >= 0) {
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (is_unix_socket_ && !socket_path_.empty()) {
            unlink(socket_path_.c_str());
        }

        // Wait for all connection threads
        for (auto& t : connection_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        connection_threads_.clear();
    }

private:
    PatternTrie& trie_;
    CallerArgs args_;
    int num_threads_;
    std::atomic<bool> running_;
    int listen_fd_;
    bool is_unix_socket_;
    std::string socket_path_;
    std::vector<std::thread> connection_threads_;
    std::mutex connections_mutex_;

    bool start_accept_loop() {
        running_ = true;

        std::cerr << "Ready to receive queries (using " << num_threads_ << " worker threads)\n";

        while (running_) {
            // Use poll with timeout to allow checking running_ flag
            struct pollfd pfd{};
            pfd.fd = listen_fd_;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, 1000);  // 1 second timeout
            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cerr << "Poll error\n";
                break;
            }
            if (ret == 0) continue;  // Timeout, check running_ flag

            int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                std::cerr << "Accept error\n";
                continue;
            }

            // Clean up finished connection threads
            cleanup_finished_threads();

            // Check connection limit
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                if (connection_threads_.size() >= 50) {
                    std::cerr << "Connection limit reached, rejecting\n";
                    close(client_fd);
                    continue;
                }

                // Start new connection handler thread
                connection_threads_.emplace_back(&Server::handle_connection, this, client_fd);
            }
        }

        return true;
    }

    void cleanup_finished_threads() {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connection_threads_.erase(
            std::remove_if(connection_threads_.begin(), connection_threads_.end(),
                [](std::thread& t) {
                    if (t.joinable()) {
                        // Check if thread is done - this is a simplification
                        // In production, use a different mechanism
                        return false;
                    }
                    return true;
                }),
            connection_threads_.end()
        );
    }

    void handle_connection(int client_fd) {
        // Set socket timeout
        struct timeval tv{};
        tv.tv_sec = 300;  // 5 minute timeout
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Create per-connection match context
        MatchContext ctx;
        ctx.ensure_capacity(trie_.get_pattern_count());

        std::string buffer;
        buffer.reserve(65536);
        char read_buf[8192];

        while (running_) {
            // Read data
            ssize_t n = recv(client_fd, read_buf, sizeof(read_buf), 0);
            if (n <= 0) {
                break;  // Connection closed or error
            }

            buffer.append(read_buf, n);

            // Try to extract complete JSON objects
            // We look for matching braces to find complete objects
            size_t start = 0;
            while (start < buffer.size()) {
                // Skip leading whitespace
                while (start < buffer.size() &&
                       (buffer[start] == ' ' || buffer[start] == '\t' ||
                        buffer[start] == '\n' || buffer[start] == '\r')) {
                    ++start;
                }
                if (start >= buffer.size()) break;

                if (buffer[start] != '{') {
                    // Invalid data - skip to next line or brace
                    size_t next = buffer.find('{', start);
                    if (next == std::string::npos) {
                        buffer.clear();
                        break;
                    }
                    start = next;
                }

                // Find matching closing brace
                size_t end = find_json_end(buffer, start);
                if (end == std::string::npos) {
                    // Incomplete JSON, wait for more data
                    break;
                }

                // Extract and process the JSON object
                std::string json = buffer.substr(start, end - start + 1);
                std::string response = process_request(json, ctx);

                // Send response with newline delimiter
                response += "\n";
                send(client_fd, response.data(), response.size(), 0);

                start = end + 1;
            }

            // Remove processed data from buffer
            if (start > 0 && start <= buffer.size()) {
                buffer.erase(0, start);
            }
        }

        close(client_fd);
    }

    // Find the end of a JSON object (matching closing brace)
    size_t find_json_end(const std::string& s, size_t start) {
        if (start >= s.size() || s[start] != '{') {
            return std::string::npos;
        }

        int depth = 0;
        bool in_string = false;
        bool escape = false;

        for (size_t i = start; i < s.size(); ++i) {
            char c = s[i];

            if (escape) {
                escape = false;
                continue;
            }

            if (c == '\\' && in_string) {
                escape = true;
                continue;
            }

            if (c == '"') {
                in_string = !in_string;
                continue;
            }

            if (in_string) continue;

            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }

        return std::string::npos;  // Incomplete
    }

    std::string process_request(const std::string& json, MatchContext& ctx) {
        JsonRequest req = parse_request(json);

        if (!req.valid) {
            return build_error_response(req.id, 400, req.error);
        }

        if (req.queries.empty()) {
            return build_error_response(req.id, 400, "No queries provided");
        }

        bool is_batch = (req.queries.size() > 1);

        if (is_batch) {
            // Batch query - process in parallel
            std::vector<QueryResult> results(req.queries.size());

            #pragma omp parallel for schedule(dynamic) num_threads(num_threads_)
            for (size_t i = 0; i < req.queries.size(); ++i) {
                // Each thread needs its own context
                MatchContext local_ctx;
                local_ctx.ensure_capacity(trie_.get_pattern_count());

                auto matches = trie_.match_pattern_to_string(req.queries[i], args_, local_ctx);

                results[i].index = static_cast<int>(i);
                for (const auto& m : matches) {
                    MatchOutput out;
                    // Parse xref to get category and id
                    // Format: "id\tcategory\t..."
                    size_t tab1 = m.pattern_xref.find('\t');
                    if (tab1 != std::string::npos) {
                        out.id = m.pattern_xref.substr(0, tab1);
                        size_t tab2 = m.pattern_xref.find('\t', tab1 + 1);
                        if (tab2 != std::string::npos) {
                            out.category = m.pattern_xref.substr(tab1 + 1, tab2 - tab1 - 1);
                        } else {
                            out.category = m.pattern_xref.substr(tab1 + 1);
                        }
                    } else {
                        out.id = m.pattern_xref;
                        out.category = "";
                    }
                    out.pattern = m.pattern_text;
                    out.match = m.matching_string;
                    results[i].matches.push_back(out);
                }
            }

            // Determine overall status - 200 if any matches, 404 if none
            bool any_matches = false;
            for (const auto& r : results) {
                if (!r.matches.empty()) {
                    any_matches = true;
                    break;
                }
            }

            return build_batch_response(req.id, any_matches ? 200 : 404, results);

        } else {
            // Single query
            auto matches = trie_.match_pattern_to_string(req.queries[0], args_, ctx);

            std::vector<MatchOutput> output;
            for (const auto& m : matches) {
                MatchOutput out;
                // Parse xref to get category and id
                size_t tab1 = m.pattern_xref.find('\t');
                if (tab1 != std::string::npos) {
                    out.id = m.pattern_xref.substr(0, tab1);
                    size_t tab2 = m.pattern_xref.find('\t', tab1 + 1);
                    if (tab2 != std::string::npos) {
                        out.category = m.pattern_xref.substr(tab1 + 1, tab2 - tab1 - 1);
                    } else {
                        out.category = m.pattern_xref.substr(tab1 + 1);
                    }
                } else {
                    out.id = m.pattern_xref;
                    out.category = "";
                }
                out.pattern = m.pattern_text;
                out.match = m.matching_string;
                output.push_back(out);
            }

            int status = output.empty() ? 404 : 200;
            return build_response(req.id, status, output);
        }
    }
};

} // namespace prefix_match
