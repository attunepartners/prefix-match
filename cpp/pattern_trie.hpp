#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>

namespace prefix_match {

// Constants
constexpr size_t NUM_VALID_CHARS = 37;  // 0=delimiter, 1-10=digits, 11-36=letters

// ASCII classification map: maps byte -> character class (0-36)
// 0 = delimiter/non-alphanumeric
// 1-10 = digits '0'-'9'
// 11-36 = letters 'a'-'z' (case-insensitive)
alignas(64) inline constexpr uint8_t ASCII_MAP[256] = {
    // 0-47: control chars and punctuation -> 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0-15
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 16-31
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 32-47 (space, punctuation)
    // 48-57: '0'-'9' -> 1-10
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    // 58-64: punctuation -> 0
    0, 0, 0, 0, 0, 0, 0,
    // 65-90: 'A'-'Z' -> 11-36
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    // 91-96: punctuation -> 0
    0, 0, 0, 0, 0, 0,
    // 97-122: 'a'-'z' -> 11-36 (same as uppercase)
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    // 123-127: punctuation -> 0
    0, 0, 0, 0, 0,
    // 128-255: extended ASCII -> 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// End-of-path key: combines char_idx and block_no
struct EopKey {
    uint32_t block_no;
    uint8_t char_idx;

    bool operator==(const EopKey& other) const {
        return block_no == other.block_no && char_idx == other.char_idx;
    }
};

struct EopKeyHash {
    size_t operator()(const EopKey& k) const {
        return (static_cast<size_t>(k.block_no) << 8) ^ k.char_idx;
    }
};

// Inner structure: word_position -> sorted vector of pattern_ids
// Using array instead of map since word positions are 1-31
struct EopWordMap {
    std::array<std::vector<uint32_t>, 32> by_pos;
    uint8_t max_pos = 0;  // Track highest used position

    std::vector<uint32_t>& operator[](uint8_t pos) {
        if (pos > max_pos) max_pos = pos;
        return by_pos[pos];
    }

    // Iterator support for range-based for
    struct Iterator {
        const EopWordMap* map;
        uint8_t pos;

        Iterator& operator++() {
            ++pos;
            while (pos <= map->max_pos && map->by_pos[pos].empty()) ++pos;
            return *this;
        }
        bool operator!=(const Iterator& other) const { return pos != other.pos; }
        std::pair<uint8_t, const std::vector<uint32_t>&> operator*() const {
            return {pos, map->by_pos[pos]};
        }
    };

    Iterator begin() const {
        uint8_t pos = 1;
        while (pos <= max_pos && by_pos[pos].empty()) ++pos;
        return {this, pos};
    }
    Iterator end() const { return {this, static_cast<uint8_t>(max_pos + 1)}; }
};

// Caller arguments - matches Python CallerArgs dataclass
struct CallerArgs {
    bool lcssmatch = false;
    bool verify = false;
    bool matching = false;
    bool removestopwords = false;
    bool address_mode = false;
    std::string server;
};

// Match result for a single pattern match
struct MatchResult {
    uint32_t pattern_id;
    std::string pattern_xref;
    std::string pattern_text;
    std::string matching_string;
    size_t match_start;
    size_t match_end;
};

// Thread-local match context - each thread needs its own instance
struct MatchContext {
    std::array<std::unordered_set<uint32_t>, 32> active_patterns;
    uint8_t max_active_pos = 0;
    std::vector<size_t> substring_start;

    void clear() {
        for (uint8_t i = 1; i <= max_active_pos; ++i) {
            active_patterns[i].clear();
        }
        max_active_pos = 0;
    }

    void ensure_capacity(size_t pattern_count) {
        if (substring_start.size() <= pattern_count) {
            substring_start.resize(pattern_count + 1, 0);
        }
    }
};

// Simple logger interface
class Logger {
public:
    virtual ~Logger() = default;
    virtual void info(const std::string& msg) = 0;
    virtual void warning(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
};

// Default stdout logger
class StdoutLogger : public Logger {
public:
    void info(const std::string& msg) override {
        std::cout << "INFO: " << msg << std::endl;
    }
    void warning(const std::string& msg) override {
        std::cerr << "WARNING: " << msg << std::endl;
    }
    void error(const std::string& msg) override {
        std::cerr << "ERROR: " << msg << std::endl;
    }
};

// Null logger for silent operation
class NullLogger : public Logger {
public:
    void info(const std::string&) override {}
    void warning(const std::string&) override {}
    void error(const std::string&) override {}
};

// Main PatternTrie class
class PatternTrie {
public:
    PatternTrie();
    ~PatternTrie() = default;

    // Pattern loading
    void read_stopwords(const std::string& filename, Logger& logger);
    bool process_pattern_file(const std::string& filename, const CallerArgs& args, Logger& logger);
    std::pair<bool, std::string> process_pattern(std::string_view pattern_in, const CallerArgs& args, Logger& logger);

    // Matching
    void prepare_for_matching();  // Call after loading patterns to optimize data structures

    // Thread-safe matching - each thread must have its own MatchContext
    std::vector<MatchResult> match_pattern_to_string(
        std::string_view input,
        const CallerArgs& args,
        MatchContext& ctx
    ) const;

    // Statistics
    size_t get_pattern_count() const { return pattern_count_; }
    size_t get_block_count() const { return block_count_; }
    size_t memory_usage() const;

private:
    // Trie structure - flat array [block_no * NUM_VALID_CHARS + char_idx] -> next_block
    std::vector<uint32_t> trie_blocks_;
    uint32_t block_count_ = 0;

    // End of path data: (char_idx, block_no) -> {word_pos -> sorted vector of pattern_ids}
    std::unordered_map<EopKey, EopWordMap, EopKeyHash> end_of_path_;

    // Pattern metadata - vectors indexed by pattern_id (1-indexed, so index 0 unused)
    uint32_t pattern_count_ = 0;
    std::vector<std::string> pattern_xref_;      // pattern_id -> xref string
    std::vector<std::string> pattern_text_;      // pattern_id -> pattern string
    std::vector<std::vector<std::string>> pattern_words_;  // pattern_id -> word list

    // Word lengths: pattern_id -> word lengths vector
    std::vector<std::vector<uint8_t>> word_lengths_;

    // Pattern word counts - indexed by pattern_id
    std::vector<uint8_t> pattern_wordcount_;

    // Patterns indexed by word count (for final match verification)
    std::array<std::vector<uint32_t>, 32> patterns_by_wordcount_;

    // Must-have words for LCSS matching
    std::unordered_map<uint32_t, std::unordered_set<uint8_t>> must_have_words_;

    // Stopwords
    std::unordered_set<std::string> stopwords_;

    // Internal helper methods
    std::vector<std::string> process_pattern_ref(
        std::string_view pattern,
        std::string_view pattern_ref,
        const CallerArgs& args,
        Logger& logger
    );

    uint32_t allocate_block();
    void insert_word_in_trie(uint32_t pattern_id, const std::string& word, uint8_t word_position);

    // String utilities
    static std::string to_lower(std::string_view s);
    static std::string_view trim(std::string_view s);
    static std::vector<std::string> split(std::string_view s, char delim);
    static std::vector<std::string> split_whitespace(std::string_view s);
    static bool is_prefix_of(std::string_view prefix, std::string_view str);
    static std::string strip_special_chars(std::string_view s);

    // Set operations on sorted vectors
    static void intersect_sorted(
        const std::vector<uint32_t>& a,
        const std::vector<uint32_t>& b,
        std::vector<uint32_t>& result
    );

    // Find first non-alphanumeric character starting at pos
    static size_t find_word_boundary(std::string_view s, size_t pos);
};

// Longest increasing subsequence (for LCSS matching)
std::vector<int> longest_increasing_subsequence(const std::vector<int>& input);

// File reading utilities - supports gzip and plain text
class FileReader {
public:
    virtual ~FileReader() = default;
    virtual bool is_open() const = 0;
    virtual bool getline(std::string& line) = 0;
    virtual void close() = 0;
};

// Create appropriate reader based on file type (gzip or plain)
std::unique_ptr<FileReader> open_file(const std::string& filename);

// Check if file is gzip compressed (checks magic bytes)
bool is_gzip_file(const std::string& filename);

} // namespace prefix_match
