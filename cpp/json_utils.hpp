#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <sstream>

namespace prefix_match {

// Simple JSON utilities for our specific request/response format
// Not a general-purpose JSON library - just handles our protocol

struct JsonRequest {
    std::string id;
    std::vector<std::string> queries;  // Single query stored as 1-element vector
    bool valid = false;
    std::string error;
};

struct MatchOutput {
    std::string category;
    std::string id;
    std::string pattern;
    std::string match;
};

struct QueryResult {
    int index;
    std::vector<MatchOutput> matches;
};

// JSON string escaping
inline std::string json_escape(std::string_view s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// Skip whitespace
inline size_t skip_ws(std::string_view s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
        ++pos;
    }
    return pos;
}

// Parse a JSON string value, returns the unescaped string and position after closing quote
inline std::pair<std::string, size_t> parse_string(std::string_view s, size_t pos) {
    if (pos >= s.size() || s[pos] != '"') {
        return {"", std::string::npos};
    }
    ++pos;  // skip opening quote

    std::string result;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') {
            return {result, pos + 1};
        }
        if (c == '\\' && pos + 1 < s.size()) {
            ++pos;
            switch (s[pos]) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u':
                    // Unicode escape - simplified handling
                    if (pos + 4 < s.size()) {
                        pos += 4;  // Skip \uXXXX, just drop it for now
                    }
                    break;
                default:
                    result += s[pos];
            }
        } else {
            result += c;
        }
        ++pos;
    }
    return {"", std::string::npos};  // Unterminated string
}

// Parse incoming JSON request
// Format: {"id": "...", "query": "..."} or {"id": "...", "queries": ["...", "..."]}
inline JsonRequest parse_request(std::string_view json) {
    JsonRequest req;

    size_t pos = skip_ws(json, 0);
    if (pos >= json.size() || json[pos] != '{') {
        req.error = "Expected '{'";
        return req;
    }
    ++pos;

    bool has_id = false;
    bool has_query = false;

    while (pos < json.size()) {
        pos = skip_ws(json, pos);
        if (pos >= json.size()) break;

        if (json[pos] == '}') {
            // End of object
            if (has_id && has_query) {
                req.valid = true;
            } else if (!has_id) {
                req.error = "Missing 'id' field";
            } else {
                req.error = "Missing 'query' or 'queries' field";
            }
            return req;
        }

        if (json[pos] == ',') {
            ++pos;
            continue;
        }

        // Parse key
        auto [key, key_end] = parse_string(json, pos);
        if (key_end == std::string::npos) {
            req.error = "Invalid key string";
            return req;
        }
        pos = skip_ws(json, key_end);

        // Expect colon
        if (pos >= json.size() || json[pos] != ':') {
            req.error = "Expected ':'";
            return req;
        }
        ++pos;
        pos = skip_ws(json, pos);

        // Parse value based on key
        if (key == "id") {
            auto [val, val_end] = parse_string(json, pos);
            if (val_end == std::string::npos) {
                req.error = "Invalid 'id' value";
                return req;
            }
            req.id = val;
            has_id = true;
            pos = val_end;
        } else if (key == "query") {
            auto [val, val_end] = parse_string(json, pos);
            if (val_end == std::string::npos) {
                req.error = "Invalid 'query' value";
                return req;
            }
            req.queries.push_back(val);
            has_query = true;
            pos = val_end;
        } else if (key == "queries") {
            // Parse array of strings
            if (pos >= json.size() || json[pos] != '[') {
                req.error = "Expected '[' for queries array";
                return req;
            }
            ++pos;

            while (pos < json.size()) {
                pos = skip_ws(json, pos);
                if (pos >= json.size()) break;

                if (json[pos] == ']') {
                    ++pos;
                    has_query = true;
                    break;
                }

                if (json[pos] == ',') {
                    ++pos;
                    continue;
                }

                auto [val, val_end] = parse_string(json, pos);
                if (val_end == std::string::npos) {
                    req.error = "Invalid string in queries array";
                    return req;
                }
                req.queries.push_back(val);
                pos = val_end;
            }
        } else {
            // Skip unknown field value
            if (json[pos] == '"') {
                auto [_, val_end] = parse_string(json, pos);
                pos = val_end;
            } else if (json[pos] == '[') {
                // Skip array - find matching ]
                int depth = 1;
                ++pos;
                while (pos < json.size() && depth > 0) {
                    if (json[pos] == '[') ++depth;
                    else if (json[pos] == ']') --depth;
                    else if (json[pos] == '"') {
                        auto [_, end] = parse_string(json, pos);
                        if (end == std::string::npos) break;
                        pos = end - 1;
                    }
                    ++pos;
                }
            } else if (json[pos] == '{') {
                // Skip object - find matching }
                int depth = 1;
                ++pos;
                while (pos < json.size() && depth > 0) {
                    if (json[pos] == '{') ++depth;
                    else if (json[pos] == '}') --depth;
                    else if (json[pos] == '"') {
                        auto [_, end] = parse_string(json, pos);
                        if (end == std::string::npos) break;
                        pos = end - 1;
                    }
                    ++pos;
                }
            } else {
                // Skip primitive value (number, true, false, null)
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
                    ++pos;
                }
            }
        }
    }

    req.error = "Unexpected end of JSON";
    return req;
}

// Build JSON response for single query
inline std::string build_response(const std::string& id, int status,
                                  const std::vector<MatchOutput>& matches) {
    std::ostringstream oss;
    oss << "{\"id\":\"" << json_escape(id) << "\",\"status\":" << status << ",\"results\":[";

    for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"category\":\"" << json_escape(matches[i].category) << "\""
            << ",\"id\":\"" << json_escape(matches[i].id) << "\""
            << ",\"pattern\":\"" << json_escape(matches[i].pattern) << "\""
            << ",\"match\":\"" << json_escape(matches[i].match) << "\"}";
    }

    oss << "]}";
    return oss.str();
}

// Build JSON response for batch query
inline std::string build_batch_response(const std::string& id, int status,
                                        const std::vector<QueryResult>& results) {
    std::ostringstream oss;
    oss << "{\"id\":\"" << json_escape(id) << "\",\"status\":" << status << ",\"results\":[";

    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"index\":" << results[i].index << ",\"matches\":[";

        for (size_t j = 0; j < results[i].matches.size(); ++j) {
            if (j > 0) oss << ",";
            const auto& m = results[i].matches[j];
            oss << "{\"category\":\"" << json_escape(m.category) << "\""
                << ",\"id\":\"" << json_escape(m.id) << "\""
                << ",\"pattern\":\"" << json_escape(m.pattern) << "\""
                << ",\"match\":\"" << json_escape(m.match) << "\"}";
        }

        oss << "]}";
    }

    oss << "]}";
    return oss.str();
}

// Build error response
inline std::string build_error_response(const std::string& id, int status, const std::string& error) {
    std::ostringstream oss;
    oss << "{\"id\":\"" << json_escape(id) << "\",\"status\":" << status
        << ",\"error\":\"" << json_escape(error) << "\"}";
    return oss.str();
}

} // namespace prefix_match
