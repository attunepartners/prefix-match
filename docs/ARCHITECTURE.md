# PrefixMatch Architecture

System design and integration patterns for deploying PrefixMatch within a Demand-Side Platform (DSP) advertising technology stack.

PrefixMatch is a C++ implementation based on concepts from pattern matching code licensed from Luther J. Woodrum. The original system was deployed at a medium-sized DSP for bidding on programmatic advertising auctions. Note that the original licensed code considerably outperformed this implementation.

## Table of Contents

1. [RTB Ecosystem Overview](#rtb-ecosystem-overview)
2. [The URL Categorization Challenge](#the-url-categorization-challenge)
3. [System Architecture](#system-architecture)
4. [Pattern Preprocessing Pipeline](#pattern-preprocessing-pipeline)
5. [Data Structures](#data-structures)
6. [Integration Patterns](#integration-patterns)
7. [Deployment Topologies](#deployment-topologies)
8. [Scaling Strategies](#scaling-strategies)

---

## RTB Ecosystem Overview

### Programmatic Advertising Flow

Real-Time Bidding (RTB) is an auction-based system for buying and selling digital advertising impressions in real-time:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Publisher  │     │     SSP      │     │     DSP      │
│   Website    │────▶│   (Seller)   │────▶│   (Buyer)    │
└──────────────┘     └──────────────┘     └──────────────┘
       │                    │                    │
       │ User visits        │ Bid Request        │ Bid Response
       │ webpage            │ (URL, user data)   │ ($X.XX CPM)
       ▼                    ▼                    ▼
   Ad impression      100+ DSPs receive    Highest bidder
   opportunity        bid request          wins impression
```

### Timing Constraints

The entire RTB transaction must complete within strict time limits:

| Phase | Budget |
|-------|--------|
| SSP processing | 10-20ms |
| Network latency (SSP→DSP) | 10-30ms |
| **DSP bid evaluation** | **20-50ms** |
| Network latency (DSP→SSP) | 10-30ms |
| SSP auction resolution | 10-20ms |
| **Total** | **50-150ms** |

DSPs that exceed the timeout receive no impressions—their bids are discarded.

### DSP Decision Components

Within the 20-50ms DSP budget, multiple operations must complete:

```
Bid Request Received
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│                    DSP Bid Evaluation                    │
│                                                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │     URL     │  │    User     │  │   Campaign  │      │
│  │ Categorize  │  │  Targeting  │  │   Matching  │      │
│  │   <1ms      │  │   5-15ms    │  │   2-5ms     │      │
│  └─────────────┘  └─────────────┘  └─────────────┘      │
│         │                │                │              │
│         └────────────────┼────────────────┘              │
│                          ▼                               │
│                 ┌─────────────┐                          │
│                 │  Bid Price  │                          │
│                 │ Calculation │                          │
│                 │   1-5ms     │                          │
│                 └─────────────┘                          │
│                          │                               │
└──────────────────────────┼───────────────────────────────┘
                           ▼
                    Bid Response
```

**PrefixMatch handles URL Categorization**—the fastest component in the stack, enabling more time for complex targeting and bidding logic.

---

## The URL Categorization Challenge

### Why Categorize URLs?

Advertisers need to know the context of ad placements:

1. **Brand Safety**: Avoid placing luxury brand ads on controversial content
2. **Contextual Relevance**: Show auto ads on automotive review sites
3. **Audience Segmentation**: News readers vs. sports fans vs. shoppers
4. **Bid Optimization**: Pay premium for high-value inventory categories

### URL Characteristics in RTB

Publisher URLs in bid requests exhibit specific properties:

| Property | Implication |
|----------|-------------|
| Canonical format | No typos, standardized by SSPs |
| Hierarchical structure | `domain/section/subsection/article` |
| Known publishers | ~80% of volume from top 10,000 sites |
| High repetition | Same URL patterns appear millions of times |
| Prefix-meaningful | Category often determined by URL prefix alone |

### Why Trie-Based Matching?

Given URL characteristics, trie-based prefix matching is optimal:

| Requirement | Trie Capability |
|-------------|-----------------|
| Sub-millisecond latency | O(m) lookup, m = pattern length |
| Exact matching | Deterministic, no false positives |
| Prefix semantics | Native prefix matching |
| Memory efficiency | Shared prefixes stored once |
| No training data | Works immediately with pattern list |
| Auditable decisions | Traceable match path |

---

## System Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────┐
│                    PrefixMatch Server                    │
│                                                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │                  Pattern Trie                    │    │
│  │                                                  │    │
│  │   ┌─────┐     ┌─────┐     ┌─────┐              │    │
│  │   │ cnn │────▶│.com │────▶│/pol │──▶ EOP       │    │
│  │   └─────┘     └─────┘     └─────┘   (news_pol) │    │
│  │      │                                          │    │
│  │      ▼                                          │    │
│  │   ┌─────┐     ┌─────┐                          │    │
│  │   │espn │────▶│.com │──────────────▶ EOP       │    │
│  │   └─────┘     └─────┘               (sports)   │    │
│  │                                                  │    │
│  └─────────────────────────────────────────────────┘    │
│                         │                                │
│  ┌──────────────────────┼──────────────────────────┐    │
│  │                Thread Pool                       │    │
│  │   ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐  │    │
│  │   │Worker 1│ │Worker 2│ │Worker 3│ │Worker N│  │    │
│  │   └────────┘ └────────┘ └────────┘ └────────┘  │    │
│  └─────────────────────────────────────────────────┘    │
│                         │                                │
│  ┌──────────────────────┼──────────────────────────┐    │
│  │            Connection Handler                    │    │
│  │   TCP/9999  ◀───────────────▶  Unix Socket      │    │
│  └─────────────────────────────────────────────────┘    │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

---

## Pattern Preprocessing Pipeline

Before patterns are inserted into the trie, they undergo a preprocessing pipeline that optimizes match quality and reduces the pattern set size.

### Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Pattern Preprocessing Pipeline                │
│                                                                  │
│  Raw Pattern File                                                │
│        │                                                         │
│        ▼                                                         │
│  ┌─────────────────┐                                            │
│  │  Parse Line     │  "pro professional linux\t2784028\t..."    │
│  │  (tab-split)    │  → pattern: "pro professional linux"       │
│  └────────┬────────┘    ref: "2784028\tSERVERS..."              │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Tokenize       │  → ["pro", "professional", "linux"]        │
│  │  (whitespace)   │                                            │
│  └────────┬────────┘                                            │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Remove 1-char  │  → (no change in this example)             │
│  │  words          │                                            │
│  └────────┬────────┘                                            │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Remove         │  → ["pro", "professional", "linux"]        │
│  │  stopwords      │    (if -W enabled)                         │
│  │  (optional)     │                                            │
│  └────────┬────────┘                                            │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Prefix         │  "pro" is prefix of "professional"         │
│  │  Shortening     │  → ["professional", "linux"]               │
│  └────────┬────────┘                                            │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Minimum Word   │  2+ words required                         │
│  │  Check          │  → ACCEPT (2 words)                        │
│  └────────┬────────┘                                            │
│           │                                                      │
│           ▼                                                      │
│  ┌─────────────────┐                                            │
│  │  Insert into    │  Pattern stored in trie                    │
│  │  Trie           │                                            │
│  └─────────────────┘                                            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Preprocessing Rules

#### Rule 1: Single-Character Word Removal

Words with only one character provide no discriminative value and increase false positives:

```
Before: "a great software company"
After:  "great software company"
```

#### Rule 2: Stopword Removal (Optional)

When enabled (`-W`), common words are removed. Protected words (system, information, etc.) are retained:

```
Before: "the best software in the world"
After:  "best software world"
```

#### Rule 3: Adjacent Prefix Shortening

**Critical for URL matching accuracy.** When word N is a prefix of word N+1, word N is removed:

```
Before: "auto automotive dealer"
After:  "automotive dealer"

Before: "pro professional services"
After:  "professional services"
```

**Why this matters:** Without this rule, the pattern "pro professional" would match the single word "professional" twice—once for "pro" (as a prefix) and once for "professional". This creates false positives when the intent is to match the phrase "pro professional".

#### Rule 4: Minimum Word Count

Patterns must have **at least 2 words** after preprocessing. This ensures:

1. Sufficient specificity for categorization
2. Reduced false positive rate
3. Meaningful prefix sequences

### Pattern Rejection Reasons

| Reason | Example | Resolution |
|--------|---------|------------|
| Single word | `"google"` | Add more words: `"google search"` |
| All stopwords | `"the and or"` | Use meaningful words |
| Prefix collapse | `"pro professional"` | Already handled; use longer pattern |
| Non-alphanumeric | `"café latté"` | Use ASCII equivalents |
| Comment line | `"# this is a comment"` | Intentional, not an error |

### Impact on Pattern Set Size

Preprocessing typically reduces the effective pattern count by 5-15%:

| Stage | Patterns | Reduction |
|-------|----------|-----------|
| Raw file | 100,000 | - |
| After parsing | 98,500 | -1.5% (comments, blanks) |
| After stopwords | 97,000 | -1.5% |
| After prefix shortening | 95,000 | -2.0% |
| After min-word check | 85,000 | -10.5% |

The reduction in pattern count improves both memory efficiency and match precision.

---

### Trie Structure

The trie stores patterns as sequences of "blocks"—contiguous runs of similar characters:

```
Pattern: "cnn.com/politics"

Tokenization:
  "cnn" (letters) → ".com" (delim+letters) → "/" (delim) → "politics" (letters)

Block representation:
  Block 0: [c,n,n]      → hash → trie_index_1
  Block 1: [.,c,o,m]    → hash → trie_index_2
  Block 2: [/]          → hash → trie_index_3
  Block 3: [p,o,l,...]  → hash → trie_index_4 → EOP marker
```

### End-of-Pattern (EOP) Storage

Pattern metadata stored in hash table keyed by (block_index, char_type):

```cpp
struct EopKey {
    uint32_t block;      // Trie block index
    uint8_t char_type;   // Character class (0-36)
};

struct EopEntry {
    std::string category;    // "news_politics"
    std::string pattern_id;  // "NP001"
    std::string pattern;     // "cnn.com/politics"
};

std::unordered_map<EopKey, std::vector<EopEntry>> eop_table;
```

### Matching Algorithm

```
Input: "https://cnn.com/politics/article-123"

1. Initialize active_set = {root}

2. For each character position:
   a. Determine char_type (letter/digit/delimiter)
   b. If char_type changed from previous:
      - Complete current block
      - Look up block hash in trie
      - Update active_set with matching children
      - Check EOP table for completed patterns
   c. Continue building current block

3. Return all matched patterns with positions
```

---

## Integration Patterns

### Pattern 1: Sidecar Service

PrefixMatch runs as a sidecar container alongside the bid evaluation service:

```
┌─────────────────────────────────────────────┐
│                    Pod                       │
│                                              │
│  ┌─────────────────┐  ┌─────────────────┐   │
│  │  Bid Evaluator  │  │  PrefixMatch    │   │
│  │    (main)       │◀▶│   (sidecar)     │   │
│  │                 │  │                 │   │
│  │  Port 8080      │  │  /tmp/pm.sock   │   │
│  └─────────────────┘  └─────────────────┘   │
│          ▲                                   │
│          │                                   │
└──────────┼───────────────────────────────────┘
           │
    Bid Requests (HTTP)
```

**Advantages:**
- Unix socket provides lowest latency
- Shared lifecycle management
- No network hops

**Deployment:**
```yaml
# Kubernetes pod spec
containers:
  - name: bid-evaluator
    image: dsp/evaluator:latest
    volumeMounts:
      - name: sock
        mountPath: /tmp
  - name: prefix-match
    image: dsp/prefix-match:latest
    args: ["-p", "/data/patterns.txt", "-S", "/tmp/pm.sock"]
    volumeMounts:
      - name: sock
        mountPath: /tmp
      - name: patterns
        mountPath: /data
volumes:
  - name: sock
    emptyDir: {}
  - name: patterns
    configMap:
      name: url-patterns
```

### Pattern 2: Shared Service

Dedicated PrefixMatch service handles requests from multiple bid evaluators:

```
                    ┌─────────────────────┐
                    │   Load Balancer     │
                    │   (L4/TCP)          │
                    └──────────┬──────────┘
                               │
           ┌───────────────────┼───────────────────┐
           ▼                   ▼                   ▼
   ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
   │ PrefixMatch   │   │ PrefixMatch   │   │ PrefixMatch   │
   │  Instance 1   │   │  Instance 2   │   │  Instance 3   │
   │  Port 9999    │   │  Port 9999    │   │  Port 9999    │
   └───────────────┘   └───────────────┘   └───────────────┘
           ▲                   ▲                   ▲
           │                   │                   │
           └───────────────────┼───────────────────┘
                               │
   ┌───────────────┬───────────┴───────────┬───────────────┐
   ▼               ▼                       ▼               ▼
┌──────┐       ┌──────┐               ┌──────┐       ┌──────┐
│ Bid  │       │ Bid  │               │ Bid  │       │ Bid  │
│Eval 1│       │Eval 2│               │Eval N│       │Eval M│
└──────┘       └──────┘               └──────┘       └──────┘
```

**Advantages:**
- Centralized pattern management
- Independent scaling
- Shared memory efficiency

**Considerations:**
- TCP latency (~1ms vs 0.1ms Unix socket)
- Connection pooling required
- Network reliability dependency

### Pattern 3: Embedded Library

Compile PrefixMatch directly into bid evaluator:

```cpp
#include "pattern_trie.hpp"

class BidEvaluator {
    PatternTrie url_categorizer;
    MatchContext ctx;  // Thread-local

public:
    BidEvaluator(const std::string& pattern_file) {
        url_categorizer.load_patterns(pattern_file);
        ctx.ensure_capacity(url_categorizer.pattern_count());
    }

    BidResponse evaluate(const BidRequest& req) {
        // URL categorization - sub-millisecond
        auto categories = url_categorizer.match_pattern_to_string(
            req.url, args, ctx);

        // Continue with user targeting, bid calculation...
    }
};
```

**Advantages:**
- Zero network latency
- No serialization overhead
- Simplest deployment

**Considerations:**
- Memory duplication across instances
- Pattern updates require restart
- Tighter coupling

---

## Deployment Topologies

### Single Region

```
┌─────────────────────────────────────────────────────────┐
│                     AWS us-east-1                        │
│                                                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │              Bid Evaluator Cluster               │    │
│  │                                                  │    │
│  │   ┌─────────┐ ┌─────────┐ ┌─────────┐          │    │
│  │   │ Pod 1   │ │ Pod 2   │ │ Pod N   │          │    │
│  │   │ +sidecar│ │ +sidecar│ │ +sidecar│          │    │
│  │   └─────────┘ └─────────┘ └─────────┘          │    │
│  │                                                  │    │
│  └─────────────────────────────────────────────────┘    │
│                          │                               │
│                          ▼                               │
│  ┌─────────────────────────────────────────────────┐    │
│  │               Pattern Storage                    │    │
│  │              (S3 / ConfigMap)                    │    │
│  └─────────────────────────────────────────────────┘    │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### Multi-Region

```
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│   us-east-1      │  │   eu-west-1      │  │   ap-northeast-1 │
│                  │  │                  │  │                  │
│ ┌──────────────┐ │  │ ┌──────────────┐ │  │ ┌──────────────┐ │
│ │ PM Cluster   │ │  │ │ PM Cluster   │ │  │ │ PM Cluster   │ │
│ │ (8 pods)     │ │  │ │ (4 pods)     │ │  │ │ (4 pods)     │ │
│ └──────────────┘ │  │ └──────────────┘ │  │ └──────────────┘ │
│        ▲         │  │        ▲         │  │        ▲         │
└────────┼─────────┘  └────────┼─────────┘  └────────┼─────────┘
         │                     │                     │
         └─────────────────────┼─────────────────────┘
                               │
                    ┌──────────▼──────────┐
                    │  Pattern Sync       │
                    │  (S3 Cross-Region)  │
                    └─────────────────────┘
```

---

## Scaling Strategies

### Vertical Scaling

Single instance handles 27K queries/sec. Scale thread count to CPU cores:

| CPU Cores | Threads | Throughput |
|-----------|---------|------------|
| 2 | 2 | 15,000/sec |
| 4 | 4 | 26,000/sec |
| 8 | 8 | 27,000/sec |
| 16 | 8 | 27,000/sec |

Diminishing returns beyond 8 threads due to memory bandwidth.

### Horizontal Scaling

Linear throughput scaling with instances:

| Instances | Combined Throughput |
|-----------|---------------------|
| 1 | 27,000/sec |
| 4 | 108,000/sec |
| 10 | 270,000/sec |
| 100 | 2,700,000/sec |

### Pattern Updates

**Blue-green deployment:**
1. Deploy new instances with updated patterns
2. Shift traffic to new instances
3. Drain and terminate old instances

**Rolling update:**
1. Update patterns on disk/ConfigMap
2. Rolling restart of pods
3. ~30 second full cluster update

**Hot reload (future enhancement):**
1. Signal server to reload patterns
2. Build new trie in background
3. Atomic swap when complete

---

## Monitoring and Observability

### Key Metrics

| Metric | Target | Alert Threshold |
|--------|--------|-----------------|
| Query latency (p99) | <1ms | >5ms |
| Queries/sec | Baseline ±20% | <80% baseline |
| Memory usage | <100MB | >200MB |
| Connection count | <40 | >45 |
| Error rate | 0% | >0.1% |

### Prometheus Metrics (Recommended)

```
# Query latency histogram
prefix_match_query_duration_seconds{quantile="0.99"}

# Query throughput
prefix_match_queries_total

# Active connections
prefix_match_connections_active

# Pattern count
prefix_match_patterns_loaded
```

### Health Endpoints

Implement HTTP health check wrapper:

```bash
#!/bin/bash
# health_check.sh
echo '{"id":"health","queries":["test"]}' | \
  timeout 1 nc -U /tmp/pm.sock | \
  grep -q '"results"' && exit 0 || exit 1
```
