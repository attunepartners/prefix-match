# Sub-Millisecond URL Categorization for Real-Time Bidding

## A Trie-Based Approach to High-Performance Ad Tech

---

### Abstract

In programmatic advertising, Demand-Side Platforms (DSPs) must evaluate bid opportunities and respond within 50-150 milliseconds. URL categorization—determining the content category of a publisher's webpage—is a critical component of this decision pipeline. This paper presents PrefixMatch, a trie-based prefix matching engine that achieves sub-millisecond URL categorization with throughput exceeding 27,000 queries per second on commodity hardware. We demonstrate how deterministic, exact-match algorithms outperform machine learning approaches for this specific use case, and provide production deployment patterns for integration into RTB systems.

---

### 1. Introduction

Every time you visit a webpage with advertisements, an invisible auction takes place. In the 100 milliseconds between your browser requesting the page and the ads appearing, dozens of advertising platforms have received a bid request, evaluated whether to bid, calculated a price, and submitted their response. This is Real-Time Bidding (RTB), and it processes over 500 billion transactions daily.

Within this compressed timeline, Demand-Side Platforms face a fundamental challenge: they must understand *what* they're bidding on. A luxury car manufacturer wants their ads on automotive review sites, not controversial news articles. A children's toy company needs to avoid adult content. A financial services firm seeks placements on business and investing pages.

URL categorization solves this problem. Given a publisher URL like `https://cnn.com/politics/election-2024/analysis`, the system must determine its category (news_politics) with enough speed to leave time for user targeting, bid calculation, and network round-trips.

This paper describes PrefixMatch, a purpose-built URL categorization engine optimized for the unique requirements of RTB:

- **Sub-millisecond latency**: <0.1ms per query
- **High throughput**: 27,000+ queries/second per instance
- **Deterministic matching**: Same input always produces same output
- **Zero training required**: Works immediately with pattern definitions

---

### 2. The RTB Timing Challenge

#### 2.1 The 50-Millisecond Budget

A typical RTB transaction must complete within 50-150ms end-to-end:

| Phase | Time Budget |
|-------|-------------|
| Publisher SSP processing | 10-20ms |
| Network: SSP → DSP | 10-30ms |
| **DSP bid evaluation** | **20-50ms** |
| Network: DSP → SSP | 10-30ms |
| SSP auction resolution | 10-20ms |

DSPs that exceed their timeout are excluded from the auction. At scale, this means lost revenue and reduced campaign reach.

#### 2.2 Inside DSP Bid Evaluation

The 20-50ms DSP budget must accommodate multiple operations:

1. **URL Categorization**: What type of content is this?
2. **Brand Safety Check**: Is this placement acceptable for the advertiser?
3. **User Lookup**: What do we know about this browser/user?
4. **Audience Matching**: Does this user match campaign targeting criteria?
5. **Bid Calculation**: What price should we bid?

Each millisecond consumed by URL categorization is a millisecond unavailable for sophisticated targeting and bidding logic. This creates intense pressure for the categorization layer to be as fast as possible.

#### 2.3 Volume Considerations

A mid-sized DSP might process:

- 100,000+ bid requests per second
- 8.6 billion requests per day
- Peaks of 200,000+ requests/second during high-traffic events

The categorization system must handle this volume while maintaining sub-millisecond latency across the entire traffic range.

---

### 3. Why Not Machine Learning?

The instinctive modern response to any classification problem is to apply machine learning. For URL categorization in RTB, this approach faces significant challenges.

#### 3.1 Latency Constraints

Vector embedding models require:

- **Tokenization**: 0.1-1ms to convert URL to tokens
- **Embedding inference**: 1-10ms on GPU, 10-100ms on CPU
- **Similarity search**: 1-10ms for approximate nearest neighbor

Total latency of 10-100ms consumes half or more of the entire DSP budget.

#### 3.2 Infrastructure Overhead

ML-based classification requires:

- GPU instances ($2-4/hour vs $0.10/hour for CPU)
- Model serving infrastructure (TensorFlow Serving, Triton, etc.)
- Model versioning and deployment pipelines
- Monitoring for model drift and accuracy degradation

#### 3.3 The Nature of URLs

URLs in RTB have characteristics that favor exact matching:

| Property | Implication |
|----------|-------------|
| **Canonical format** | SSPs normalize URLs before bid requests—no typos |
| **Hierarchical structure** | `domain/section/article` naturally forms prefix patterns |
| **Known publishers** | Top 10,000 sites represent 80%+ of traffic |
| **Determinism required** | Billing and reporting need consistent categorization |

The semantic understanding that ML provides—handling typos, understanding synonyms—adds no value when URLs are already standardized.

#### 3.4 When to Use ML

Machine learning remains valuable for:

- **User interest modeling**: Embedding browsing history for audience targeting
- **Content classification**: Analyzing actual page content for brand safety
- **New/unknown URLs**: Fallback classification for never-seen domains

The optimal architecture combines fast exact matching (PrefixMatch) for known patterns with ML-based classification for edge cases.

---

### 4. Trie-Based Prefix Matching

#### 4.1 Core Data Structure

A trie (prefix tree) stores strings as paths from root to leaves. Strings sharing common prefixes share path segments, providing natural compression and efficient lookup.

For URL categorization, we adapt the traditional trie with block-based indexing:

```
Traditional character trie:
  root → c → n → n → . → c → o → m → / → p → o → l ...

Block-based trie:
  root → [cnn] → [.com] → [/] → [politics] → EOP
```

Each "block" represents a contiguous sequence of similar characters (letters, digits, or delimiters). This reduces tree depth and improves cache efficiency.

#### 4.2 Character Classification

URLs contain three character types:

| Type | Characters | Code |
|------|------------|------|
| Delimiter | `.` `/` `:` `-` `_` `?` `&` `=` | 0 |
| Digit | `0-9` | 1-10 |
| Letter | `a-z` `A-Z` | 11-36 |

When character type changes, the current block completes and lookup proceeds to the next trie level.

#### 4.3 Matching Algorithm

```
Input: "https://cnn.com/politics/article-123"

Traversal:
  [https]     → trie lookup → no pattern starts here
  [://]       → trie lookup → no pattern starts here
  [cnn]       → trie lookup → found: continue to children
  [.com]      → trie lookup → found: "cnn.com" is EOP → MATCH
  [/politics] → trie lookup → found: "cnn.com/politics" is EOP → MATCH
  [/article]  → trie lookup → not found
  ...

Result:
  - Category: news_general (pattern: cnn.com)
  - Category: news_politics (pattern: cnn.com/politics)
```

#### 4.4 Complexity Analysis

| Operation | Complexity |
|-----------|------------|
| Pattern insertion | O(m) where m = pattern length in blocks |
| Query lookup | O(n) where n = query length in blocks |
| Memory | O(P × L) where P = patterns, L = avg pattern length |

Critically, lookup time is independent of pattern count. A trie with 1,000 patterns has the same query latency as one with 1,000,000 patterns (assuming similar pattern lengths).

#### 4.5 Pattern Preprocessing

Raw patterns undergo preprocessing before trie insertion to optimize match quality and reduce false positives.

**Preprocessing Pipeline:**

1. **Single-character word removal**: Words like "a", "I" provide no discriminative value
2. **Stopword removal** (optional): Common words ("the", "and", "of") can be filtered
3. **Adjacent prefix shortening**: If word N is a prefix of word N+1, word N is removed
4. **Minimum word count**: Patterns must have at least 2 words after preprocessing

**Adjacent Prefix Shortening Example:**

```
Input:  "pro professional services"
Output: "professional services"
```

Without this rule, "pro" would match as a prefix of "professional", causing the pattern to match any URL containing just "professional"—a false positive. The preprocessing ensures the pattern only matches when "professional" and "services" appear together.

**Impact on Pattern Set:**

| Stage | Patterns | Reduction |
|-------|----------|-----------|
| Raw file | 100,000 | — |
| After single-char removal | 99,500 | -0.5% |
| After prefix shortening | 97,000 | -2.5% |
| After min-word check | 85,000 | -12.4% |

The ~15% reduction improves both memory efficiency and match precision.

#### 4.6 LCSS Matching Mode

For content classification (as opposed to URL categorization), PrefixMatch supports Longest Common Subsequence (LCSS) matching. In LCSS mode:

- Pattern words can appear non-consecutively in the input
- Words must maintain relative order
- Minimum 3 words must match
- "Must-have" words can be specified with `*` prefix

**Use Case Comparison:**

| Mode | Use Case | Precision | Recall |
|------|----------|-----------|--------|
| Exact (default) | URL categorization | High | Medium |
| LCSS | Content classification | Medium | High |

For RTB URL categorization, exact mode is recommended due to the canonical nature of URLs and the need for deterministic billing.

---

### 5. Implementation Details

#### 5.1 Trie Storage

The trie uses a flat array representation with hash-based child lookup:

```cpp
struct TrieEntry {
    uint32_t child_index;   // Index of first child block
    uint8_t child_count;    // Number of children
};

std::vector<TrieEntry> trie_blocks;
std::unordered_map<uint64_t, uint32_t> block_to_index;
```

This provides O(1) child lookup while maintaining memory locality.

#### 5.2 End-of-Pattern (EOP) Handling

Pattern metadata is stored in a separate hash table:

```cpp
struct PatternInfo {
    std::string category;
    std::string pattern_id;
    std::string pattern_text;
};

std::unordered_map<EopKey, std::vector<PatternInfo>> eop_table;
```

The EOP key combines the trie block index with the terminating character type, enabling multiple patterns to share path prefixes.

#### 5.3 Thread Safety

Each query uses a thread-local context to avoid contention:

```cpp
struct MatchContext {
    std::array<std::unordered_set<uint32_t>, 32> active_patterns;
    std::vector<size_t> substring_positions;
};
```

The trie itself is read-only after construction, requiring no synchronization.

#### 5.4 Server Architecture

PrefixMatch operates as a JSON-over-TCP/Unix-socket service:

```
┌─────────────────────────────────────┐
│         PrefixMatch Server          │
│                                     │
│  ┌─────────────────────────────┐   │
│  │      Pattern Trie           │   │
│  │    (read-only, shared)      │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────┐ ┌─────────┐          │
│  │Worker 1 │ │Worker N │  ...     │
│  │ (ctx)   │ │ (ctx)   │          │
│  └─────────┘ └─────────┘          │
│                                     │
│  ┌─────────────────────────────┐   │
│  │    Connection Handler       │   │
│  │   TCP:9999 / Unix Socket    │   │
│  └─────────────────────────────┘   │
└─────────────────────────────────────┘
```

---

### 6. Performance Results

#### 6.1 Test Environment

- **Hardware**: 8-core CPU, 32GB RAM
- **Patterns**: 100,000 URL prefix patterns
- **Test data**: 4.6 million unique URLs
- **Metrics**: Throughput (queries/sec), latency (ms)

#### 6.2 Batch Processing

| Threads | Throughput | Speedup |
|---------|------------|---------|
| 1 | 32,000/sec | 1.0x |
| 2 | 58,000/sec | 1.8x |
| 4 | 76,000/sec | 2.4x |
| 8 | 85,000/sec | 2.7x |

#### 6.3 Server Mode (4 Concurrent Clients)

| Transport | Per-Client | Combined | Latency (p99) |
|-----------|------------|----------|---------------|
| TCP | 3,900/sec | 15,700/sec | <1ms |
| Unix Socket | 6,700/sec | 26,800/sec | <1ms |

Unix sockets provide 70% higher throughput by eliminating TCP/IP stack overhead.

#### 6.4 Latency Distribution

| Batch Size | Mean Latency | p99 Latency |
|------------|--------------|-------------|
| 1 | 0.13ms | 0.5ms |
| 10 | 0.05ms/query | 0.3ms |
| 100 | 0.03ms/query | 0.2ms |

Sub-millisecond latency achieved across all batch sizes.

#### 6.5 Memory Efficiency

| Patterns | Memory Usage | Per-Pattern |
|----------|--------------|-------------|
| 10,000 | 5 MB | 500 bytes |
| 100,000 | 43 MB | 430 bytes |
| 1,000,000 | 400 MB | 400 bytes |

Memory efficiency improves with scale as more patterns share common prefixes.

---

### 7. Comparison with Alternatives

#### 7.1 Exact String Matching (Hash Table)

```cpp
std::unordered_map<std::string, Category> url_to_category;
```

| Aspect | Hash Table | PrefixMatch |
|--------|------------|-------------|
| Prefix matching | No | Yes |
| Memory (100K patterns) | ~50MB | ~43MB |
| Lookup time | O(1) | O(m) |
| Use case | Exact URLs only | URL prefixes |

Hash tables cannot match prefixes—`cnn.com/politics` won't match `cnn.com/politics/article`.

#### 7.2 Regular Expressions

```cpp
std::regex pattern("(cnn\\.com/politics|espn\\.com|...)");
```

| Aspect | Regex | PrefixMatch |
|--------|-------|-------------|
| Latency (100K patterns) | 10-100ms | <0.1ms |
| Pattern flexibility | High | Prefix only |
| Maintainability | Poor at scale | Good |

Regex with 100K alternatives is impractical; matching time grows with pattern count.

#### 7.3 Vector Embeddings

| Aspect | Vector Search | PrefixMatch |
|--------|---------------|-------------|
| Latency | 10-100ms | <0.1ms |
| Infrastructure | GPU servers | CPU only |
| Accuracy | ~95% | 100% (exact) |
| Handles typos | Yes | No |
| Training required | Yes | No |

Vector search excels at semantic similarity but cannot meet RTB latency requirements.

---

### 8. Production Deployment

#### 8.1 Integration Pattern: Sidecar

Deploy PrefixMatch as a sidecar container:

```yaml
containers:
  - name: bid-evaluator
    image: dsp/evaluator:latest
  - name: prefix-match
    image: dsp/prefix-match:latest
    args: ["-p", "/data/patterns.txt", "-S", "/tmp/pm.sock"]
```

Benefits:
- Unix socket communication (lowest latency)
- Shared pod lifecycle
- No network dependencies

#### 8.2 Scaling

Linear horizontal scaling:

| Instances | Combined Throughput |
|-----------|---------------------|
| 4 | 108,000/sec |
| 10 | 270,000/sec |
| 100 | 2,700,000/sec |

#### 8.3 Pattern Updates

Blue-green deployment for zero-downtime updates:

1. Deploy new instances with updated patterns
2. Gradually shift traffic
3. Drain and terminate old instances

---

### 9. Conclusion

PrefixMatch demonstrates that purpose-built algorithms remain superior to general-purpose ML for specific, well-defined problems. By exploiting the unique characteristics of RTB URL categorization—canonical URLs, prefix-meaningful structure, determinism requirements—we achieve:

- **170x faster** than vector embedding approaches
- **1000x faster** than regex alternatives
- **Sub-millisecond latency** enabling real-time bidding
- **Zero training** or ML infrastructure required

The system processes 27,000+ queries per second on a single commodity server, enabling small DSP teams to handle enterprise-scale traffic without GPU infrastructure.

For RTB URL categorization, the answer isn't more sophisticated machine learning—it's the right algorithm for the problem.

---

### References

1. OpenRTB Specification, IAB Tech Lab
2. "Tries: A Data Structure for Fast Retrieval", Fredkin (1960)
3. Real-Time Bidding Protocol, Google Ad Manager
4. "The Anatomy of a Large-Scale Hypertextual Web Search Engine", Brin & Page (1998)

---

### Appendix A: Quick Start

```bash
# Build
cd cpp && make

# Start server
./prefix_match -p patterns.txt -S /tmp/pm.sock -t 8

# Query
echo '{"id":"1","queries":["cnn.com/politics/article"]}' | nc -U /tmp/pm.sock
```

### Appendix B: Pattern File Format

```
category|pattern_id|pattern_text
news_politics|NP001|cnn.com/politics
news_sports|NS001|espn.com
auto_luxury|AL001|bmw.com
```

---

*PrefixMatch is open source software. See GitHub repository for code and documentation.*
