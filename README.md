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

| Mode | Throughput | Latency |
|------|------------|---------|
| Batch (8 threads) | 85,000 queries/sec | N/A |
| TCP Server (4 clients) | 15,700 queries/sec | <1ms |
| Unix Socket (4 clients) | 26,800 queries/sec | <1ms |

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
./prefix_match -p patterns.txt -i strings.txt -o results.txt
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

One pattern per line with pipe-delimited fields:
```
category|pattern_id|pattern_text
```

Example:
```
news_politics|NP001|cnn.com/politics
news_sports|NS001|espn.com
auto_luxury|AL001|bmw.com
auto_luxury|AL002|mercedes-benz.com
```

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
