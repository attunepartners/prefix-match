# PrefixMatch

High-performance trie-based prefix matching engine optimized for real-time bidding (RTB) and programmatic advertising URL categorization.

## Overview

PrefixMatch is a C++ library and server designed to match URL strings against a large set of prefix patterns with sub-millisecond latency. Built for integration into Demand-Side Platform (DSP) bid evaluation pipelines where every millisecond counts.

### Key Features

- **Sub-millisecond latency**: ~0.1ms per query, enabling real-time bid decisions
- **High throughput**: 27,000+ queries/sec per server instance
- **Memory efficient**: ~43MB for 100,000 patterns
- **Multiple interfaces**: Batch processing, TCP server, Unix socket server
- **Production ready**: Persistent connections, parallel processing, JSON protocol

### Performance Benchmarks

Tested with 100,000 patterns and 4.6 million URLs on commodity hardware.

#### Batch Processing

| Patterns | URLs | Matches | Throughput |
|----------|------|---------|------------|
| 100,000 | 4,615,231 | 274,553 | 16,402/sec |

#### Server Mode - Unix Socket

| Clients | Batch Size | Queries | Throughput | Latency (p99) |
|---------|------------|---------|------------|---------------|
| 1 | 10 | 2,000 | 12,189/sec | <1ms |
| 4 | 10 | 8,000 | 14,612/sec | <1ms |
| 4 | 100 | 20,000 | **25,104/sec** | <1ms |
| 8 | 100 | 80,000 | 24,102/sec | <1ms |

#### Server Mode - TCP

| Clients | Batch Size | Queries | Throughput | Latency (p99) |
|---------|------------|---------|------------|---------------|
| 4 | 100 | 40,000 | 22,031/sec | <1ms |
| 8 | 100 | 80,000 | 21,651/sec | <1ms |

#### Summary

| Transport | Peak Throughput | Optimal Config |
|-----------|-----------------|----------------|
| Unix Socket | **25,104 q/sec** | 4 clients, batch=100 |
| TCP | 22,031 q/sec | 4 clients, batch=100 |

Unix socket provides ~14% higher throughput by eliminating TCP/IP stack overhead.

Run your own benchmarks with the included script:
```bash
./examples/benchmark_server.py --socket /tmp/pm.sock --clients 4 --batch 100
```

## Quick Start

### Building

```bash
cd cpp
make clean && make
```

Requirements:
- C++17 compatible compiler (GCC 7+ or Clang 5+)
- zlib development headers
- OpenMP support

### Basic Usage

**Batch Mode** - Process a file of strings:
```bash
./prefix_match -p patterns.txt -s strings.txt -m
```

**Server Mode** - TCP on port 9999:
```bash
./prefix_match -p patterns.txt -P 9999
```

**Server Mode** - Unix socket:
```bash
./prefix_match -p patterns.txt -S /tmp/pm.sock
```

### Pattern File Format

Tab-separated format with four fields:
```
pattern_words<TAB>pattern_id<TAB>category<TAB>category_num
```

Example:
```
linux mount command	2784028	SERVERS_MANAGED_HOSTING	7509
travel expense	4821538	SOFTWARE_TRAVEL_EXPENSE	9052
honda crv model year	8425111	AUTO_HONDA	9508
docker monitoring	1454481	DEV_APP_CONTAINERIZATION	7243
```

**Pattern Requirements:**
- Patterns must have at least 2 words after preprocessing
- Adjacent prefix words are automatically shortened (e.g., "pro professional" → "professional")
- Single-character words are removed

### Query Protocol (Server Mode)

**Request:**
```json
{"id": "req-123", "queries": ["https://cnn.com/politics/article1", "https://espn.com/nba"]}
```

**Response:**
```json
{
  "id": "req-123",
  "results": [
    {"query": "https://cnn.com/politics/article1", "matches": [{"category": "news_politics", "pattern_id": "NP001", "pattern": "cnn.com/politics", "matched_text": "cnn.com/politics"}]},
    {"query": "https://espn.com/nba", "matches": [{"category": "news_sports", "pattern_id": "NS001", "pattern": "espn.com", "matched_text": "espn.com"}]}
  ]
}
```

## Documentation

- [User Guide](docs/USER_GUIDE.md) - Complete usage documentation
- [Architecture](docs/ARCHITECTURE.md) - DSP integration and system design
- [White Paper](docs/WHITEPAPER.md) - Technical deep-dive and benchmarks

## Use Cases

- **RTB URL Categorization**: Classify publisher URLs for bid decisioning
- **Brand Safety**: Filter URLs against blocked category patterns
- **Audience Segmentation**: Map browsing history to market segments
- **Log Processing**: Batch categorize historical URL data

## License

MIT License - See [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome. Please read the architecture documentation before submitting PRs.
