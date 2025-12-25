#include "pattern_trie.hpp"
#include <iostream>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <regex>
#include <chrono>
#include <zlib.h>

namespace prefix_match {

// ============================================================================
// String Utilities
// ============================================================================

std::string PatternTrie::to_lower(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

std::string_view PatternTrie::trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> PatternTrie::split(std::string_view s, char delim) {
    std::vector<std::string> result;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            if (i > start) {
                result.emplace_back(s.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    if (start < s.size()) {
        result.emplace_back(s.substr(start));
    }
    return result;
}

std::vector<std::string> PatternTrie::split_whitespace(std::string_view s) {
    std::vector<std::string> result;
    size_t start = 0;
    bool in_word = false;

    for (size_t i = 0; i < s.size(); ++i) {
        bool is_space = std::isspace(static_cast<unsigned char>(s[i]));
        if (is_space) {
            if (in_word) {
                result.emplace_back(s.substr(start, i - start));
                in_word = false;
            }
        } else {
            if (!in_word) {
                start = i;
                in_word = true;
            }
        }
    }
    if (in_word) {
        result.emplace_back(s.substr(start));
    }
    return result;
}

bool PatternTrie::is_prefix_of(std::string_view prefix, std::string_view str) {
    if (prefix.size() > str.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(prefix[i])) !=
            std::tolower(static_cast<unsigned char>(str[i]))) {
            return false;
        }
    }
    return true;
}

std::string PatternTrie::strip_special_chars(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c != '*' && c != '^') {
            result.push_back(c);
        }
    }
    return result;
}

size_t PatternTrie::find_word_boundary(std::string_view s, size_t pos) {
    while (pos < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[pos]);
        if (ASCII_MAP[c] == 0) {
            return pos;
        }
        ++pos;
    }
    return s.size();
}

// ============================================================================
// Sorted Vector Set Operations
// ============================================================================

void PatternTrie::intersect_sorted(
    const std::vector<uint32_t>& a,
    const std::vector<uint32_t>& b,
    std::vector<uint32_t>& result
) {
    result.clear();
    auto it_a = a.begin();
    auto it_b = b.begin();

    while (it_a != a.end() && it_b != b.end()) {
        if (*it_a < *it_b) {
            ++it_a;
        } else if (*it_b < *it_a) {
            ++it_b;
        } else {
            result.push_back(*it_a);
            ++it_a;
            ++it_b;
        }
    }
}

// ============================================================================
// Longest Increasing Subsequence
// ============================================================================

std::vector<int> longest_increasing_subsequence(const std::vector<int>& input) {
    if (input.empty()) return {};

    size_t n = input.size();
    std::vector<int> M(n + 1, 0);  // M[j] = index of smallest ending element of LIS of length j
    std::vector<int> P(n, -1);     // P[i] = predecessor of element at index i

    int L = 0;  // Length of longest LIS found so far

    for (size_t i = 0; i < n; ++i) {
        // Binary search for largest j such that input[M[j]] < input[i]
        int lo = 1, hi = L + 1;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (input[M[mid]] < input[i]) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        int j = lo;  // j is the position where input[i] would extend the LIS

        P[i] = (j > 1) ? M[j - 1] : -1;
        M[j] = static_cast<int>(i);

        if (j > L) {
            L = j;
        }
    }

    // Reconstruct the LIS
    std::vector<int> result(L);
    int k = M[L];
    for (int i = L - 1; i >= 0; --i) {
        result[i] = input[k];
        k = P[k];
    }

    return result;
}

// ============================================================================
// PatternTrie Implementation
// ============================================================================

PatternTrie::PatternTrie() {
    // Allocate initial block (block 0)
    trie_blocks_.resize(NUM_VALID_CHARS, 0);
    block_count_ = 1;
}

uint32_t PatternTrie::allocate_block() {
    uint32_t new_block = block_count_++;
    trie_blocks_.resize(block_count_ * NUM_VALID_CHARS, 0);
    return new_block;
}

void PatternTrie::read_stopwords(const std::string& filename, Logger& logger) {
    static const std::unordered_set<std::string> keep = {
        "system", "second", "little", "course", "world",
        "value", "right", "needs", "information", "invention"
    };

    logger.info("Reading stopwords: " + filename);

    std::ifstream file(filename);
    if (!file.is_open()) {
        logger.error("Cannot open stopwords file: " + filename);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Stopwords are comma-delimited
    auto words = split(content, ',');
    for (auto& word : words) {
        std::string trimmed(trim(word));
        std::string lower = to_lower(trimmed);
        if (keep.find(lower) == keep.end()) {
            stopwords_.insert(lower);
        }
    }

    logger.info("Loaded " + std::to_string(stopwords_.size()) + " stopwords");
}

std::vector<std::string> PatternTrie::process_pattern_ref(
    std::string_view pattern,
    std::string_view pattern_ref,
    const CallerArgs& args,
    Logger& logger
) {
    // Split on whitespace and convert to lowercase
    std::string lower_pattern = to_lower(pattern);
    auto words = split_whitespace(lower_pattern);

    size_t original_count = words.size();

    // Remove single-character words
    words.erase(
        std::remove_if(words.begin(), words.end(),
            [](const std::string& w) { return w.size() <= 1; }),
        words.end()
    );

    // Remove stopwords if enabled
    if (args.removestopwords) {
        words.erase(
            std::remove_if(words.begin(), words.end(),
                [this](const std::string& w) {
                    return stopwords_.find(w) != stopwords_.end();
                }),
            words.end()
        );
    }

    // Remove adjacent words where one is prefix of next
    if (original_count != 1 && words.size() > 1) {
        std::vector<std::string> stripped;
        stripped.reserve(words.size());
        for (const auto& w : words) {
            stripped.push_back(strip_special_chars(w));
        }

        std::vector<std::string> filtered;
        for (size_t i = 0; i < words.size(); ++i) {
            bool keep = (i == words.size() - 1) ||
                        !is_prefix_of(stripped[i], stripped[i + 1]);
            if (keep) {
                filtered.push_back(std::move(words[i]));
            }
        }
        words = std::move(filtered);
    }

    // Must have at least 2 words
    if (words.size() < 2) {
        if (original_count != words.size() || words.size() < 2) {
            std::ostringstream oss;
            oss << "Pattern_ref: '" << pattern_ref << "' changed from: '"
                << pattern << "' to: '";
            for (size_t i = 0; i < words.size(); ++i) {
                if (i > 0) oss << " ";
                oss << words[i];
            }
            oss << "'";
            logger.info(oss.str());
        }
        return {};
    }

    return words;
}

void PatternTrie::insert_word_in_trie(uint32_t pattern_id, const std::string& word, uint8_t word_position) {
    uint32_t current_block = 0;
    uint32_t prev_block = 0;
    uint8_t last_char_idx = 0;

    for (unsigned char c : word) {
        uint8_t char_idx = ASCII_MAP[c];
        if (char_idx == 0) continue;  // Skip non-alphanumeric

        prev_block = current_block;
        last_char_idx = char_idx;

        size_t trie_idx = current_block * NUM_VALID_CHARS + char_idx;
        if (trie_blocks_[trie_idx] == 0) {
            trie_blocks_[trie_idx] = allocate_block();
        }
        current_block = trie_blocks_[trie_idx];
    }

    // Record end-of-path information
    if (current_block != 0) {
        EopKey key{prev_block, last_char_idx};
        auto& word_map = end_of_path_[key];
        auto& pattern_list = word_map[word_position];

        // Insert maintaining sorted order
        auto it = std::lower_bound(pattern_list.begin(), pattern_list.end(), pattern_id);
        if (it == pattern_list.end() || *it != pattern_id) {
            pattern_list.insert(it, pattern_id);
        }
    }
}

std::pair<bool, std::string> PatternTrie::process_pattern(
    std::string_view pattern_in,
    const CallerArgs& args,
    Logger& logger
) {
    ++pattern_count_;
    uint32_t pattern_id = pattern_count_;

    // Ensure vectors are large enough (pattern_id is 1-indexed)
    if (pattern_id >= pattern_xref_.size()) {
        size_t new_size = std::max(pattern_id + 1, static_cast<uint32_t>(pattern_xref_.size() * 2));
        pattern_xref_.resize(new_size);
        pattern_text_.resize(new_size);
        pattern_words_.resize(new_size);
        word_lengths_.resize(new_size);
        pattern_wordcount_.resize(new_size, 0);
    }

    std::string_view trimmed = trim(pattern_in);

    // Skip empty lines and comments
    if (trimmed.empty() || trimmed[0] == '#') {
        return {false, "comment"};
    }

    // Skip exception patterns
    if (trimmed.find("_EXCEPTIONS") != std::string_view::npos) {
        return {false, "exception pattern"};
    }

    // Split on tab: pattern \t xref_data
    auto parts = split(trimmed, '\t');
    if (parts.empty()) {
        return {false, "empty pattern"};
    }

    std::string pattern = parts[0];
    std::string pattern_xref;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (i > 1) pattern_xref += "\t";
        pattern_xref += parts[i];
    }

    // Check for non-alphanumeric characters
    bool has_invalid = false;
    for (char c : pattern) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            !std::isspace(static_cast<unsigned char>(c)) &&
            c != '*' && c != '-' && c != '^') {
            has_invalid = true;
            break;
        }
    }

    if (has_invalid) {
        logger.info("Pattern with non alphanumeric char: " + pattern);
        if (!args.address_mode) {
            return {false, "non alphanumeric characters"};
        }
        // In address mode, strip invalid chars
        std::string cleaned;
        for (char c : pattern) {
            if (std::isalnum(static_cast<unsigned char>(c)) ||
                std::isspace(static_cast<unsigned char>(c))) {
                cleaned += c;
            } else {
                cleaned += ' ';
            }
        }
        pattern = cleaned;
    }

    // Process pattern into word list
    auto words = process_pattern_ref(pattern, pattern_xref, args, logger);

    if (words.empty()) {
        return {false, "non-conforming pattern"};
    }

    // Store pattern metadata using direct vector indexing
    pattern_xref_[pattern_id] = pattern_xref;

    std::string pattern_text;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) pattern_text += " ";
        pattern_text += words[i];
    }
    pattern_text_[pattern_id] = pattern_text;
    pattern_words_[pattern_id] = words;

    // Store word lengths
    std::vector<uint8_t> lengths;
    lengths.reserve(words.size());

    // Insert each word into trie
    for (size_t i = 0; i < words.size(); ++i) {
        uint8_t word_pos = static_cast<uint8_t>(i + 1);  // 1-indexed
        std::string word = words[i];

        // Check for special character prefix (* = must-have)
        if (!word.empty() && (word[0] == '*' || word[0] == '^')) {
            must_have_words_[pattern_id].insert(word_pos);
            word = word.substr(1);
        }

        lengths.push_back(static_cast<uint8_t>(word.size()));
        insert_word_in_trie(pattern_id, word, word_pos);
    }

    word_lengths_[pattern_id] = std::move(lengths);

    // Track patterns by word count
    uint8_t word_count = static_cast<uint8_t>(words.size());
    pattern_wordcount_[pattern_id] = word_count;

    if (word_count < 32) {
        auto& pattern_list = patterns_by_wordcount_[word_count];
        auto it = std::lower_bound(pattern_list.begin(), pattern_list.end(), pattern_id);
        pattern_list.insert(it, pattern_id);
    }

    return {true, ""};
}

bool PatternTrie::process_pattern_file(
    const std::string& filename,
    const CallerArgs& args,
    Logger& logger
) {
    auto start = std::chrono::high_resolution_clock::now();

    auto reader = open_file(filename);
    if (!reader || !reader->is_open()) {
        logger.error("Cannot open pattern file: " + filename);
        return false;
    }

    std::string line;
    size_t line_no = 0;
    size_t loaded = 0;

    while (reader->getline(line)) {
        ++line_no;
        auto [success, reason] = process_pattern(line, args, logger);
        if (success) {
            ++loaded;
        } else if (!reason.empty() && reason != "comment") {
            std::ostringstream oss;
            oss << "Pattern not processed: " << filename << " line " << line_no
                << " '" << trim(line) << "': " << reason;
            logger.info(oss.str());
        }
    }

    reader->close();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    logger.info("Loaded " + std::to_string(loaded) + " patterns from " + filename +
                " in " + std::to_string(duration.count()) + "ms");
    logger.info("Total blocks: " + std::to_string(block_count_));

    return true;
}

void PatternTrie::prepare_for_matching() {
    // No longer needed - MatchContext handles per-thread state
    // Kept for API compatibility
}

std::vector<MatchResult> PatternTrie::match_pattern_to_string(
    std::string_view input,
    const CallerArgs& args,
    MatchContext& ctx
) const {
    std::vector<MatchResult> results;

    // Cache frequently used values
    const bool do_lcss = args.lcssmatch;
    const bool do_matching = args.matching;

    // Trim input
    std::string_view trimmed = trim(input);
    if (trimmed.empty()) return results;

    // LCSS tracking (only allocate if needed)
    std::unordered_map<uint32_t, std::unordered_map<uint8_t, size_t>> lcss_dict;
    std::unordered_set<uint32_t> patterns_found;

    // Clear context from previous call
    ctx.clear();

    // Ensure substring_start is large enough
    if (do_matching) {
        ctx.ensure_capacity(pattern_count_);
    }

    uint32_t current_block = 0;
    bool at_word_start = true;  // Track if we're at the start of a word
    const uint8_t* input_ptr = reinterpret_cast<const uint8_t*>(trimmed.data());
    const size_t input_len = trimmed.size();

    for (size_t char_idx = 0; char_idx < input_len; ++char_idx) {
        uint8_t c = input_ptr[char_idx];
        uint8_t char_class = ASCII_MAP[c];

        if (__builtin_expect(char_class == 0, 0)) {
            // Delimiter - reset trie traversal
            current_block = 0;
            at_word_start = true;
            continue;
        }

        if (__builtin_expect(at_word_start, 0)) {
            // First character of a word - just advance trie, no EOP check needed
            at_word_start = false;
            current_block = trie_blocks_[char_class];
            continue;
        }

        // If we hit a dead-end in previous char, skip until next word
        if (__builtin_expect(current_block == 0, 0)) {
            continue;
        }

        // Prefetch next trie block while we do EOP check
        size_t next_trie_idx = current_block * NUM_VALID_CHARS + char_class;
        __builtin_prefetch(&trie_blocks_[next_trie_idx], 0, 3);

        // Check end-of-path - single lookup, then iterate only existing word positions
        EopKey key{current_block, char_class};
        auto eop_it = end_of_path_.find(key);
        if (eop_it != end_of_path_.end()) {
            for (const auto& [word_pos, pattern_set] : eop_it->second) {
                if (word_pos == 0 || word_pos >= 32) continue;

                // LCSS tracking
                if (do_lcss) {
                    for (uint32_t pattern_id : pattern_set) {
                        lcss_dict[pattern_id][word_pos] = char_idx;
                    }
                }

                if (word_pos == 1) {
                    // First word of pattern - add all to active tracking
                    auto& active = ctx.active_patterns[1];
                    if (ctx.max_active_pos < 1) ctx.max_active_pos = 1;

                    for (uint32_t pattern_id : pattern_set) {
                        active.insert(pattern_id);

                        if (do_matching && pattern_id < word_lengths_.size()) {
                            const auto& lengths = word_lengths_[pattern_id];
                            if (!lengths.empty()) {
                                ctx.substring_start[pattern_id] = char_idx - lengths[0] + 1;
                            }
                        }
                    }
                } else {
                    // Check if previous word position has matching patterns
                    auto& prev_active = ctx.active_patterns[word_pos - 1];
                    if (prev_active.empty()) continue;

                    auto& current_active = ctx.active_patterns[word_pos];
                    if (ctx.max_active_pos < word_pos) ctx.max_active_pos = word_pos;

                    for (uint32_t pattern_id : pattern_set) {
                        auto it = prev_active.find(pattern_id);
                        if (it == prev_active.end()) continue;

                        // Found in prev_active - move to current
                        prev_active.erase(it);

                        // Check if this completes the pattern
                        if (pattern_id < pattern_wordcount_.size() &&
                            pattern_wordcount_[pattern_id] == word_pos) {
                            if (do_lcss) {
                                patterns_found.insert(pattern_id);
                            }

                            // Build match result
                            MatchResult result;
                            result.pattern_id = pattern_id;
                            result.pattern_xref = pattern_xref_[pattern_id];
                            result.pattern_text = pattern_text_[pattern_id];

                            if (do_matching) {
                                size_t start = ctx.substring_start[pattern_id];
                                size_t end = find_word_boundary(trimmed, char_idx + 1);
                                result.match_start = start;
                                result.match_end = end;
                                result.matching_string = std::string(trimmed.substr(start, end - start));
                            }

                            results.push_back(std::move(result));
                        } else {
                            // Add to current position
                            current_active.insert(pattern_id);
                        }
                    }
                }
            }
        }

        // Advance trie (reuse already-computed index)
        current_block = trie_blocks_[next_trie_idx];
    }

    // TODO: LCSS matching (if do_lcss && !lcss_dict.empty())
    // This would use longest_increasing_subsequence on the word positions

    return results;
}

size_t PatternTrie::memory_usage() const {
    size_t total = 0;

    // Trie blocks
    total += trie_blocks_.capacity() * sizeof(uint32_t);

    // End of path map (estimate)
    total += end_of_path_.size() * (sizeof(EopKey) + sizeof(EopWordMap) + 64);
    for (const auto& [key, word_map] : end_of_path_) {
        for (const auto& [word_pos, pattern_vec] : word_map) {
            total += pattern_vec.capacity() * sizeof(uint32_t) + sizeof(uint8_t);
        }
    }

    // Pattern metadata (vectors)
    for (const auto& str : pattern_xref_) {
        total += str.capacity();
    }
    total += pattern_xref_.capacity() * sizeof(std::string);

    for (const auto& str : pattern_text_) {
        total += str.capacity();
    }
    total += pattern_text_.capacity() * sizeof(std::string);

    for (const auto& vec : word_lengths_) {
        total += vec.capacity();
    }
    total += word_lengths_.capacity() * sizeof(std::vector<uint8_t>);

    total += pattern_wordcount_.capacity() * sizeof(uint8_t);

    // Patterns by wordcount
    for (const auto& vec : patterns_by_wordcount_) {
        total += vec.capacity() * sizeof(uint32_t);
    }

    return total;
}

// ============================================================================
// File Reading Utilities (gzip and plain text support)
// ============================================================================

// Check gzip magic bytes (1f 8b)
bool is_gzip_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    unsigned char magic[2];
    file.read(reinterpret_cast<char*>(magic), 2);
    return file.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
}

// Plain text file reader
class PlainFileReader : public FileReader {
public:
    explicit PlainFileReader(const std::string& filename)
        : file_(filename) {}

    bool is_open() const override { return file_.is_open(); }

    bool getline(std::string& line) override {
        return static_cast<bool>(std::getline(file_, line));
    }

    void close() override { file_.close(); }

private:
    std::ifstream file_;
};

// Gzip file reader using zlib
class GzipFileReader : public FileReader {
public:
    explicit GzipFileReader(const std::string& filename) {
        gz_ = gzopen(filename.c_str(), "rb");
    }

    ~GzipFileReader() override {
        if (gz_) gzclose(gz_);
    }

    bool is_open() const override { return gz_ != nullptr; }

    bool getline(std::string& line) override {
        line.clear();
        char buf[4096];

        while (true) {
            if (gzgets(gz_, buf, sizeof(buf)) == nullptr) {
                return !line.empty();
            }

            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
                line.append(buf);
                return true;
            }
            line.append(buf);
        }
    }

    void close() override {
        if (gz_) {
            gzclose(gz_);
            gz_ = nullptr;
        }
    }

private:
    gzFile gz_ = nullptr;
};

std::unique_ptr<FileReader> open_file(const std::string& filename) {
    if (is_gzip_file(filename)) {
        return std::make_unique<GzipFileReader>(filename);
    }
    return std::make_unique<PlainFileReader>(filename);
}

} // namespace prefix_match
