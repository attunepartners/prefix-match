#!/usr/bin/env python3
"""
PrefixMatch Server Benchmark

Benchmarks the PrefixMatch server with concurrent clients to measure
throughput and latency under load.

Usage:
    # Unix socket benchmark
    ./benchmark_server.py --socket /tmp/pm.sock --clients 4 --requests 100 --batch 100

    # TCP benchmark
    ./benchmark_server.py --host 127.0.0.1 --port 9999 --clients 4 --requests 100 --batch 100

    # Using sample URLs file
    ./benchmark_server.py --socket /tmp/pm.sock --urls sample_urls.txt --clients 8
"""

import argparse
import socket
import json
import time
import sys
import gzip
from concurrent.futures import ThreadPoolExecutor, as_completed


def load_urls(filepath, limit=5000):
    """Load URLs from file (supports .gz compression)."""
    urls = []
    opener = gzip.open if filepath.endswith('.gz') else open
    mode = 'rt' if filepath.endswith('.gz') else 'r'

    with opener(filepath, mode) as f:
        for i, line in enumerate(f):
            if i >= limit:
                break
            urls.append(line.strip())
    return urls


def create_socket(socket_path=None, host=None, port=None):
    """Create and connect a socket (Unix or TCP)."""
    if socket_path:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(socket_path)
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
    return sock


def client_worker(client_id, urls, num_requests, batch_size, socket_path=None, host=None, port=None):
    """Worker function for a single benchmark client."""
    sock = create_socket(socket_path, host, port)

    total_queries = 0
    total_matches = 0
    latencies = []

    for req_num in range(num_requests):
        # Create batch request
        batch_start = (req_num * batch_size) % len(urls)
        batch_urls = urls[batch_start:batch_start + batch_size]
        if len(batch_urls) < batch_size:
            batch_urls += urls[:batch_size - len(batch_urls)]

        request = json.dumps({
            "id": f"c{client_id}-r{req_num}",
            "queries": batch_urls
        }) + "\n"

        # Time the request
        start = time.time()
        sock.sendall(request.encode())

        # Read response
        response = b""
        while b"\n" not in response:
            chunk = sock.recv(65536)
            if not chunk:
                break
            response += chunk

        elapsed = time.time() - start
        latencies.append(elapsed * 1000)  # Convert to ms

        result = json.loads(response.decode())
        total_queries += len(batch_urls)
        for r in result.get("results", []):
            total_matches += len(r.get("matches", []))

    sock.close()

    return {
        "client_id": client_id,
        "queries": total_queries,
        "matches": total_matches,
        "latencies": latencies,
        "total_time": sum(latencies) / 1000
    }


def percentile(data, p):
    """Calculate percentile of sorted data."""
    if not data:
        return 0
    sorted_data = sorted(data)
    k = (len(sorted_data) - 1) * p / 100
    f = int(k)
    c = f + 1 if f + 1 < len(sorted_data) else f
    return sorted_data[f] + (k - f) * (sorted_data[c] - sorted_data[f])


def main():
    parser = argparse.ArgumentParser(description="PrefixMatch Server Benchmark")
    parser.add_argument("--socket", "-S", help="Unix socket path")
    parser.add_argument("--host", "-H", default="127.0.0.1", help="TCP host (default: 127.0.0.1)")
    parser.add_argument("--port", "-P", type=int, help="TCP port")
    parser.add_argument("--urls", "-u", default="sample_urls.txt", help="URL file to use (default: sample_urls.txt)")
    parser.add_argument("--clients", "-c", type=int, default=4, help="Number of concurrent clients (default: 4)")
    parser.add_argument("--requests", "-r", type=int, default=100, help="Requests per client (default: 100)")
    parser.add_argument("--batch", "-b", type=int, default=100, help="URLs per request batch (default: 100)")
    parser.add_argument("--url-limit", type=int, default=5000, help="Max URLs to load (default: 5000)")

    args = parser.parse_args()

    if not args.socket and not args.port:
        parser.error("Either --socket or --port must be specified")

    # Load URLs
    print(f"Loading URLs from {args.urls}...")
    try:
        urls = load_urls(args.urls, args.url_limit)
    except FileNotFoundError:
        print(f"Error: URL file '{args.urls}' not found")
        sys.exit(1)
    print(f"Loaded {len(urls):,} URLs")

    # Print benchmark config
    transport = f"Unix socket: {args.socket}" if args.socket else f"TCP: {args.host}:{args.port}"
    total_queries = args.clients * args.requests * args.batch

    print(f"\nBenchmark Configuration:")
    print(f"  Transport: {transport}")
    print(f"  Clients: {args.clients}")
    print(f"  Requests per client: {args.requests}")
    print(f"  URLs per batch: {args.batch}")
    print(f"  Total queries: {total_queries:,}")
    print("-" * 60)

    # Run benchmark
    start = time.time()

    with ThreadPoolExecutor(max_workers=args.clients) as executor:
        futures = [
            executor.submit(
                client_worker, i, urls, args.requests, args.batch,
                socket_path=args.socket, host=args.host, port=args.port
            )
            for i in range(args.clients)
        ]

        results = []
        for future in as_completed(futures):
            try:
                results.append(future.result())
            except Exception as e:
                print(f"Client error: {e}")

    total_elapsed = time.time() - start

    # Aggregate results
    total_queries = 0
    total_matches = 0
    all_latencies = []

    for r in sorted(results, key=lambda x: x["client_id"]):
        qps = r["queries"] / r["total_time"]
        print(f"Client {r['client_id']}: {r['queries']:,} queries, {r['matches']:,} matches, "
              f"{r['total_time']:.2f}s, {qps:,.0f} q/s")
        total_queries += r["queries"]
        total_matches += r["matches"]
        all_latencies.extend(r["latencies"])

    # Calculate statistics
    combined_qps = total_queries / total_elapsed
    avg_latency = sum(all_latencies) / len(all_latencies) if all_latencies else 0
    p50 = percentile(all_latencies, 50)
    p95 = percentile(all_latencies, 95)
    p99 = percentile(all_latencies, 99)

    print("-" * 60)
    print(f"Combined: {total_queries:,} queries, {total_matches:,} matches")
    print(f"Wall time: {total_elapsed:.2f}s")
    print(f"Throughput: {combined_qps:,.0f} queries/sec")
    print(f"\nLatency (per batch of {args.batch} URLs):")
    print(f"  Mean: {avg_latency:.2f}ms")
    print(f"  p50:  {p50:.2f}ms")
    print(f"  p95:  {p95:.2f}ms")
    print(f"  p99:  {p99:.2f}ms")


if __name__ == "__main__":
    main()
