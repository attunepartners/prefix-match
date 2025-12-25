# PrefixMatch User Guide

Complete documentation for installing, configuring, and operating PrefixMatch.

## Table of Contents

1. [Installation](#installation)
2. [Pattern Files](#pattern-files)
3. [Command-Line Reference](#command-line-reference)
4. [Batch Mode](#batch-mode)
5. [Server Mode](#server-mode)
6. [Client Integration](#client-integration)
7. [Performance Tuning](#performance-tuning)
8. [Troubleshooting](#troubleshooting)

---

## Installation

### Prerequisites

- **Operating System**: Linux (tested on Ubuntu 20.04+, CentOS 7+)
- **Compiler**: GCC 7+ or Clang 5+ with C++17 support
- **Libraries**: zlib, OpenMP
- **Memory**: Minimum 512MB RAM (scales with pattern count)

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential zlib1g-dev
```

**CentOS/RHEL:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install zlib-devel
```

**macOS:**
```bash
brew install zlib libomp
```

### Building from Source

```bash
git clone <repository-url>
cd prefix_match/cpp
make clean && make
```

Build artifacts:
- `prefix_match` - Main executable
- `pattern_trie.o` - Object file for library integration

### Verifying Installation

```bash
./prefix_match --help
```

Expected output shows available command-line options.

---

## Pattern Files

### Format Specification

Pattern files are plain text with one pattern per line. Each line contains pipe-delimited fields:

```
category|pattern_id|pattern_text
```

| Field | Description | Constraints |
|-------|-------------|-------------|
| `category` | Classification label for matched URLs | Alphanumeric, underscores |
| `pattern_id` | Unique identifier for the pattern | Alphanumeric |
| `pattern_text` | Prefix to match against input strings | Case-insensitive |

### Example Pattern File

```
# URL categorization patterns for RTB
# Lines starting with # are ignored

news_politics|NP001|cnn.com/politics
news_politics|NP002|nytimes.com/politics
news_politics|NP003|washingtonpost.com/politics

news_sports|NS001|espn.com
news_sports|NS002|sports.yahoo.com
news_sports|NS003|bleacherreport.com

auto_luxury|AL001|bmw.com
auto_luxury|AL002|mercedes-benz.com
auto_luxury|AL003|lexus.com
auto_luxury|AL004|audi.com

finance_trading|FT001|bloomberg.com/markets
finance_trading|FT002|cnbc.com/quotes
finance_trading|FT003|finance.yahoo.com

ecommerce_shopping|ES001|amazon.com/dp
ecommerce_shopping|ES002|ebay.com/itm
ecommerce_shopping|ES003|walmart.com/ip
```

### Pattern Matching Rules

1. **Case Insensitive**: "CNN.com" matches pattern "cnn.com"
2. **Prefix Matching**: Pattern "espn.com" matches "espn.com/nba/scores"
3. **Token Boundaries**: Matches occur at word/token boundaries (letters, digits, delimiters)
4. **Multiple Matches**: A single URL may match multiple patterns
5. **Longest Match**: All matching patterns are returned, not just the longest

### Compressed Pattern Files

Gzip-compressed pattern files are supported:

```bash
gzip patterns.txt
./prefix_match -p patterns.txt.gz -i strings.txt
```

---

## Command-Line Reference

### Synopsis

```
prefix_match -p <pattern_file> [options]
```

### Required Options

| Option | Description |
|--------|-------------|
| `-p <file>` | Path to pattern file (required) |

### Mode Selection

| Option | Description |
|--------|-------------|
| `-i <file>` | Input file for batch mode |
| `-P <port>` | Start TCP server on port |
| `-S <path>` | Start Unix socket server at path |

### Output Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o <file>` | Output file (batch mode) | stdout |
| `-x` | Include matched substring in output | off |

### Performance Options

| Option | Description | Default |
|--------|-------------|---------|
| `-t <num>` | Number of worker threads | CPU cores |

### Examples

```bash
# Batch processing with 8 threads
./prefix_match -p patterns.txt -i urls.txt -o results.txt -t 8

# TCP server on port 9999
./prefix_match -p patterns.txt -P 9999 -t 8

# Unix socket server
./prefix_match -p patterns.txt -S /tmp/pm.sock -t 8

# Compressed files
./prefix_match -p patterns.txt.gz -i urls.txt.gz -o results.txt
```

---

## Batch Mode

### Overview

Batch mode processes a file of input strings and writes results to an output file. Optimized for high-throughput offline processing with parallel execution.

### Input File Format

One string per line:
```
https://cnn.com/politics/election-2024
https://espn.com/nba/standings
https://unknown-site.com/page
https://amazon.com/dp/B09XYZ123
```

### Output Format

Tab-separated values with columns:
```
line_number    category    pattern_id    pattern_text    [matched_substring]
```

Example output:
```
1    news_politics    NP001    cnn.com/politics    cnn.com/politics
2    news_sports    NS001    espn.com    espn.com
4    ecommerce_shopping    ES001    amazon.com/dp    amazon.com/dp
```

Lines with no matches produce no output.

### Performance Characteristics

| Patterns | Strings | Threads | Time | Throughput |
|----------|---------|---------|------|------------|
| 100,000 | 4.6M | 1 | 144s | 32,000/sec |
| 100,000 | 4.6M | 8 | 54s | 85,000/sec |

---

## Server Mode

### Overview

Server mode provides a JSON-based query interface over TCP or Unix sockets. Designed for integration into real-time systems requiring low-latency responses.

### Starting the Server

**TCP Server:**
```bash
./prefix_match -p patterns.txt -P 9999 -t 8
```

**Unix Socket Server:**
```bash
./prefix_match -p patterns.txt -S /tmp/pm.sock -t 8
```

Server output:
```
Loaded 100000 patterns in 284ms
Trie blocks: 39419
Memory usage: 42653 KB
Using 8 threads
Server listening on port 9999
Ready to receive queries (using 8 worker threads)
```

### Stopping the Server

Send SIGINT (Ctrl+C) or SIGTERM for graceful shutdown:
```bash
kill -TERM $(pgrep prefix_match)
```

### Connection Management

| Parameter | Value |
|-----------|-------|
| Max concurrent connections | 50 |
| Connection timeout | 5 minutes |
| Keep-alive | Enabled |

### Request Format

JSON object with required fields:

```json
{
  "id": "request-identifier",
  "queries": ["string1", "string2", "string3"]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Request identifier echoed in response |
| `queries` | array | List of strings to match (1 to 10,000) |

### Response Format

**Successful response:**
```json
{
  "id": "request-identifier",
  "results": [
    {
      "query": "https://cnn.com/politics/article",
      "matches": [
        {
          "category": "news_politics",
          "pattern_id": "NP001",
          "pattern": "cnn.com/politics",
          "matched_text": "cnn.com/politics"
        }
      ]
    },
    {
      "query": "https://unknown-site.com",
      "matches": []
    }
  ]
}
```

**Error response:**
```json
{
  "id": "unknown",
  "error": "Invalid JSON: unexpected token"
}
```

### Protocol Details

1. **Framing**: Requests delimited by newline or complete JSON object detection
2. **Encoding**: UTF-8
3. **Pipelining**: Multiple requests can be sent before reading responses
4. **Ordering**: Responses returned in request order

---

## Client Integration

### Python Client Example

```python
import socket
import json

class PrefixMatchClient:
    def __init__(self, host='localhost', port=9999, unix_socket=None):
        if unix_socket:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(unix_socket)
        else:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((host, port))
        self.sock.settimeout(30)
        self.request_id = 0

    def query(self, strings):
        """Query one or more strings, return list of results."""
        if isinstance(strings, str):
            strings = [strings]

        self.request_id += 1
        request = {
            "id": f"req-{self.request_id}",
            "queries": strings
        }

        self.sock.sendall((json.dumps(request) + "\n").encode())

        response = b""
        while b"\n" not in response:
            chunk = self.sock.recv(1048576)
            if not chunk:
                break
            response += chunk

        return json.loads(response.decode().strip())

    def close(self):
        self.sock.close()

# Usage
client = PrefixMatchClient(port=9999)

# Single query
result = client.query("https://cnn.com/politics/2024")
print(result)

# Batch query
urls = [
    "https://espn.com/nba",
    "https://amazon.com/dp/B123",
    "https://unknown.com"
]
results = client.query(urls)
for r in results["results"]:
    print(f"{r['query']}: {len(r['matches'])} matches")

client.close()
```

### Java Client Example

```java
import java.io.*;
import java.net.*;
import org.json.*;

public class PrefixMatchClient {
    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;
    private int requestId = 0;

    public PrefixMatchClient(String host, int port) throws IOException {
        socket = new Socket(host, port);
        out = new PrintWriter(socket.getOutputStream(), true);
        in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
    }

    public JSONObject query(String[] urls) throws IOException {
        JSONObject request = new JSONObject();
        request.put("id", "req-" + (++requestId));
        request.put("queries", new JSONArray(urls));

        out.println(request.toString());
        String response = in.readLine();
        return new JSONObject(response);
    }

    public void close() throws IOException {
        socket.close();
    }
}
```

### Go Client Example

```go
package main

import (
    "bufio"
    "encoding/json"
    "net"
)

type Request struct {
    ID      string   `json:"id"`
    Queries []string `json:"queries"`
}

type Match struct {
    Category    string `json:"category"`
    PatternID   string `json:"pattern_id"`
    Pattern     string `json:"pattern"`
    MatchedText string `json:"matched_text"`
}

type Result struct {
    Query   string  `json:"query"`
    Matches []Match `json:"matches"`
}

type Response struct {
    ID      string   `json:"id"`
    Results []Result `json:"results"`
    Error   string   `json:"error,omitempty"`
}

func Query(conn net.Conn, urls []string) (*Response, error) {
    req := Request{ID: "req-1", Queries: urls}
    data, _ := json.Marshal(req)
    data = append(data, '\n')

    conn.Write(data)

    reader := bufio.NewReader(conn)
    line, _ := reader.ReadBytes('\n')

    var resp Response
    json.Unmarshal(line, &resp)
    return &resp, nil
}
```

### Connection Pooling Recommendations

For production deployments:

1. **Maintain persistent connections**: Avoid connect/disconnect overhead
2. **Use connection pools**: 4-8 connections per server instance
3. **Implement health checks**: Periodic ping to detect stale connections
4. **Handle reconnection**: Auto-reconnect on connection failure

---

## Performance Tuning

### Thread Configuration

Match thread count to CPU cores for optimal throughput:

```bash
# Auto-detect (default)
./prefix_match -p patterns.txt -P 9999

# Explicit thread count
./prefix_match -p patterns.txt -P 9999 -t 8
```

| Threads | Throughput (queries/sec) |
|---------|-------------------------|
| 1 | 8,500 |
| 2 | 15,200 |
| 4 | 26,400 |
| 8 | 27,100 |

Returns diminish beyond physical core count.

### Unix Socket vs TCP

For local connections, Unix sockets provide 70% higher throughput:

| Transport | Throughput | Use Case |
|-----------|------------|----------|
| Unix Socket | 26,800/sec | Same-host clients |
| TCP | 15,700/sec | Remote clients |

### Batch Size Optimization

For server mode, batch multiple queries per request:

| Batch Size | Per-Query Latency | Throughput |
|------------|-------------------|------------|
| 1 | 0.13ms | 7,800/sec |
| 10 | 0.05ms | 20,000/sec |
| 100 | 0.03ms | 33,000/sec |
| 1000 | 0.03ms | 33,000/sec |

Optimal batch size: 100-500 queries per request.

### Memory Optimization

Memory usage scales linearly with pattern count:

| Patterns | Memory |
|----------|--------|
| 10,000 | ~5 MB |
| 100,000 | ~43 MB |
| 1,000,000 | ~400 MB |

### System Tuning

**Increase file descriptor limits:**
```bash
ulimit -n 65536
```

**Optimize network stack (TCP):**
```bash
# /etc/sysctl.conf
net.core.somaxconn = 1024
net.ipv4.tcp_max_syn_backlog = 1024
net.ipv4.tcp_fin_timeout = 15
```

---

## Troubleshooting

### Common Issues

**"Address already in use" error:**
```bash
# Find process using the port
lsof -i :9999
# Kill if necessary
kill -9 <pid>
```

**"Permission denied" for Unix socket:**
```bash
# Check socket file permissions
ls -la /tmp/pm.sock
# Remove stale socket
rm /tmp/pm.sock
```

**High memory usage:**
- Reduce pattern count
- Check for duplicate patterns
- Verify pattern file format

**Slow performance:**
- Verify thread count matches CPU cores
- Use Unix socket for local connections
- Increase batch size
- Check system load from other processes

### Logging

Server logs to stderr:
```bash
./prefix_match -p patterns.txt -P 9999 2>&1 | tee server.log
```

### Health Check

Simple connectivity test:
```bash
echo '{"id":"ping","queries":["test"]}' | nc localhost 9999
```

Expected response:
```json
{"id":"ping","results":[{"query":"test","matches":[]}]}
```

---

## Appendix: Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid arguments |
| 2 | Pattern file not found |
| 3 | Input file not found |
| 4 | Server bind failure |
| 5 | Runtime error |
