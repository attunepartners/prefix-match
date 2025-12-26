# PrefixMatch User Guide

Complete documentation for installing, configuring, and operating PrefixMatch.

PrefixMatch is a C++ implementation based on concepts from pattern matching code licensed from Luther J. Woodrum. The original system was deployed at a medium-sized Demand-Side Platform (DSP) for bidding on programmatic advertising auctions. Note that the original licensed code considerably outperformed this implementation.

## Table of Contents

1. [Installation](#installation)
2. [Pattern Files](#pattern-files)
3. [Pattern Preprocessing](#pattern-preprocessing)
4. [Stopwords](#stopwords)
5. [Command-Line Reference](#command-line-reference)
6. [Matching Modes](#matching-modes)
7. [Batch Mode](#batch-mode)
8. [Server Mode](#server-mode)
9. [Client Integration](#client-integration)
10. [Performance Tuning](#performance-tuning)
11. [Troubleshooting](#troubleshooting)

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

Pattern files are plain text with one pattern per line. Each line contains **four tab-separated fields**:

```
pattern_words<TAB>pattern_id<TAB>category<TAB>category_num
```

| Field | Description | Constraints |
|-------|-------------|-------------|
| `pattern_words` | Space-separated prefix words to match | Case-insensitive, alphanumeric |
| `pattern_id` | Unique numeric identifier for the pattern | Numeric |
| `category` | Classification label for matched URLs | Alphanumeric, underscores |
| `category_num` | Category group number | Numeric |

### Example Pattern File

```
# URL categorization patterns for RTB
# Lines starting with # are ignored
# Format: pattern_words<TAB>pattern_id<TAB>category<TAB>category_num

linux mount command	2784028	SERVERS_MANAGED_HOSTING	7509
travel expense	4821538	SOFTWARE_TRAVEL_EXPENSE	9052
honda crv model year	8425111	AUTO_HONDA	9508
ios in app ad	8358845	ADTECH_MOBILE_ADVERTISING	9440

docker monitoring	1454481	DEV_APP_CONTAINERIZATION	7243
vmware financ	8474775	VMWARE_BRANDED_PRODUCT	9579
hypervisor snapshot replic	6313151	SERVERS_VIRTUALIZATION	9164

startup invest fund	8524789	FINANCE_BANKING_VENTURE_CAPITAL	9692
banking consumer banking	8432797	FINANCE_BANKING_TECHNOLOGY	9510
free budget website	4076082	FINANCE_BUDGETING_FORECASTING	8954

system electronic medical record	8551064	HEALTHCARE_EHR_EMR	9442
bottle wine wine bottle	8377135	TMP_WINE_SPIRITS_EXP063018	9468
```

### Special Pattern Characters

| Character | Usage | Description |
|-----------|-------|-------------|
| `*` | Prefix on word | Marks word as "must-have" for LCSS matching |
| `#` | Line start | Comment line (ignored) |

Example with must-have word:
```
technology *software solutions	1234567	TECH_SOFTWARE	9000
```
In LCSS mode, "software" must appear in the input for this pattern to match.

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

## Pattern Preprocessing

When patterns are loaded, they undergo automatic preprocessing to optimize matching quality and reduce false positives.

### Preprocessing Rules

#### 1. Single-Character Word Removal

Words with only one character are automatically removed:

```
Input:  "a b company inc"
Output: "company inc"
```

#### 2. Prefix Shortening (Adjacent Prefix Removal)

When adjacent words share a prefix relationship, the shorter prefix is removed to prevent false matches:

```
Input:  "pro professional services"
Output: "professional services"

Input:  "auto automotive parts"
Output: "automotive parts"
```

This prevents both "pro" and "professional" from matching within the same word "professional".

#### 3. Minimum Word Count Requirement

**Patterns must have at least 2 words after preprocessing.** Single-word patterns are rejected.

| Input Pattern | After Preprocessing | Status |
|---------------|---------------------|--------|
| `"cnn com politics"` | `"cnn com politics"` | Accepted (3 words) |
| `"espn com"` | `"espn com"` | Accepted (2 words) |
| `"google"` | `"google"` | **Rejected** (1 word) |
| `"a google"` | `"google"` | **Rejected** (1 word after cleanup) |
| `"pro professional"` | `"professional"` | **Rejected** (1 word after prefix removal) |

#### 4. Stopword Removal (Optional)

When `-W` flag is enabled, common stopwords are removed from patterns:

```
Input:  "the best software in the world"
Output: "best software world"
```

### Preprocessing Log

Use `-l` flag to see preprocessing decisions:

```bash
./prefix_match -p patterns.txt -l 2>&1 | grep "Pattern_ref"
```

Output shows before/after for modified patterns:
```
Pattern_ref: '2784028	SERVERS_MANAGED_HOSTING	7509' changed from: 'pro professional linux' to 'professional linux'
```

---

## Stopwords

Stopwords are common words that can be optionally removed from patterns during loading to improve match precision.

### Stopword File Format

Stopwords are stored in a **comma-separated** file (not newline-separated):

```
the,and,or,is,a,an,of,to,in,for,on,with,at,by,from,as,be,was,were,been
```

The file can be gzip-compressed.

### Loading Stopwords

```bash
./prefix_match -p patterns.txt -w stopwords.txt -W -s strings.txt
```

| Flag | Purpose |
|------|---------|
| `-w <file>` | Load stopword file |
| `-W` | Enable stopword removal from patterns |

### Protected Words

Some words are protected and kept even if they appear in the stopwords file:

- `system`, `second`, `little`, `course`, `world`
- `value`, `right`, `needs`, `information`, `invention`

These words are commonly meaningful in technical/business contexts despite being stopwords in general text.

### Example

**Stopwords file (`stopwords.txt`):**
```
the,and,or,is,a,an,of,to,in,for,on,with
```

**Pattern file:**
```
the best software in the world	tech|T001
```

**With `-W` enabled:**
```
Pattern processed as: "best software world"
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
| `-s <file>` | Input string file for batch mode |
| `-P <port>` | Start TCP server on port |
| `-S <path>` | Start Unix socket server at path |

### Pattern Options

| Option | Description | Default |
|--------|-------------|---------|
| `-w <file>` | Load stopwords from file | none |
| `-W` | Remove stopwords from patterns | off |

### Matching Options

| Option | Description | Default |
|--------|-------------|---------|
| `-m` | Extract and output matched substring | off |
| `-L` | Enable LCSS (fuzzy) matching mode | off |
| `-v` | Verify matches (debug mode) | off |

### Output Options

| Option | Description | Default |
|--------|-------------|---------|
| `-l` | Log pattern processing details | off |
| `-q` | Quiet mode (minimal output) | off |

### Performance Options

| Option | Description | Default |
|--------|-------------|---------|
| `-t <num>` | Number of worker threads | CPU cores |

### Examples

```bash
# Batch processing with 8 threads
./prefix_match -p patterns.txt -s urls.txt -t 8

# With stopwords and substring extraction
./prefix_match -p patterns.txt -w stopwords.txt -W -s urls.txt -m

# LCSS (fuzzy) matching mode
./prefix_match -p patterns.txt -s urls.txt -L -m

# TCP server on port 9999
./prefix_match -p patterns.txt -P 9999 -t 8

# Unix socket server
./prefix_match -p patterns.txt -S /tmp/pm.sock -t 8

# Compressed files with logging
./prefix_match -p patterns.txt.gz -s urls.txt.gz -l
```

---

## Matching Modes

PrefixMatch supports two matching modes: **Exact Prefix Matching** (default) and **LCSS Matching** (fuzzy).

### Exact Prefix Matching (Default)

In exact mode, all pattern words must appear consecutively in the input string in the same order as the pattern.

**Pattern:** `"bmw luxury cars"`

| Input String | Match? | Reason |
|--------------|--------|--------|
| `"bmw luxury cars for sale"` | Yes | All words in order |
| `"new bmw luxury cars 2024"` | Yes | All words in order |
| `"bmw cars luxury"` | No | Wrong order |
| `"bmw luxury sedans"` | No | "cars" missing |

### LCSS Matching Mode (`-L`)

LCSS (Longest Common Subsequence) mode enables fuzzy matching where pattern words can appear non-consecutively in the input, as long as they maintain their relative order.

**Pattern:** `"software development tools"`

| Input String | Exact Mode | LCSS Mode |
|--------------|------------|-----------|
| `"software development tools"` | Yes | Yes |
| `"best software for development with tools"` | No | Yes |
| `"software engineering and development tools"` | No | Yes |
| `"tools for software development"` | No | No (wrong order) |

#### LCSS Requirements

1. **Minimum 3 unique words** must match from the pattern
2. Words must appear in the **same relative order** as in the pattern
3. **Must-have words** (marked with `*`) are required

#### Must-Have Words

Prefix a pattern word with `*` to make it required for LCSS matches:

```
enterprise *software solutions	tech|T001
```

This pattern only matches if "software" appears in the input, even if other words match.

#### LCSS Output Format

LCSS matches are marked with `*` instead of `=` in the output:

```
# Exact match output
=	2784028	SERVERS_MANAGED_HOSTING	linux mount command	linux.mount.command	https://example.com/linux/mount/command

# LCSS match output
*	1454481	DEV_APP_CONTAINERIZATION	docker monitoring	docker...monitoring	input string here
```

### Choosing a Matching Mode

| Use Case | Recommended Mode |
|----------|------------------|
| URL categorization | Exact (default) |
| Content classification | LCSS (`-L`) |
| Brand safety filtering | Exact (default) |
| Semantic topic detection | LCSS (`-L`) |
| High-precision matching | Exact (default) |
| High-recall matching | LCSS (`-L`) |

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
