#!/usr/bin/env python3
"""
PrefixMatch Python Client Example

Demonstrates connecting to PrefixMatch server via TCP or Unix socket
and performing single and batch queries.

Usage:
    # TCP connection
    python python_client.py --host localhost --port 9999

    # Unix socket connection
    python python_client.py --socket /tmp/pm.sock

    # Batch mode with file
    python python_client.py --port 9999 --file urls.txt --batch-size 100
"""

import argparse
import json
import socket
import sys
import time
from typing import List, Dict, Any, Optional


class PrefixMatchClient:
    """Client for PrefixMatch server."""

    def __init__(
        self,
        host: str = "localhost",
        port: int = 9999,
        unix_socket: Optional[str] = None,
        timeout: float = 30.0
    ):
        """
        Initialize client connection.

        Args:
            host: TCP host address
            port: TCP port number
            unix_socket: Path to Unix socket (overrides TCP if set)
            timeout: Socket timeout in seconds
        """
        self.request_id = 0

        if unix_socket:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(unix_socket)
            self.endpoint = f"unix:{unix_socket}"
        else:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((host, port))
            self.endpoint = f"tcp:{host}:{port}"

        self.sock.settimeout(timeout)

    def query(self, urls: List[str]) -> Dict[str, Any]:
        """
        Query one or more URLs for category matches.

        Args:
            urls: List of URLs to categorize

        Returns:
            Response dictionary with results
        """
        if isinstance(urls, str):
            urls = [urls]

        self.request_id += 1
        request = {
            "id": f"req-{self.request_id}",
            "queries": urls
        }

        # Send request
        request_bytes = (json.dumps(request) + "\n").encode("utf-8")
        self.sock.sendall(request_bytes)

        # Read response
        response = b""
        while b"\n" not in response:
            chunk = self.sock.recv(1048576)
            if not chunk:
                raise ConnectionError("Server closed connection")
            response += chunk

        return json.loads(response.decode("utf-8").strip())

    def query_single(self, url: str) -> List[Dict[str, str]]:
        """
        Query a single URL and return matches.

        Args:
            url: URL to categorize

        Returns:
            List of match dictionaries
        """
        result = self.query([url])
        if result.get("results") and len(result["results"]) > 0:
            return result["results"][0].get("matches", [])
        return []

    def close(self):
        """Close the connection."""
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def demo_single_queries(client: PrefixMatchClient):
    """Demonstrate single URL queries."""
    print("\n=== Single Query Demo ===\n")

    test_urls = [
        "https://cnn.com/politics/2024/election",
        "https://espn.com/nba/standings",
        "https://amazon.com/dp/B09XYZ123",
        "https://unknown-site.com/page",
    ]

    for url in test_urls:
        matches = client.query_single(url)
        if matches:
            for match in matches:
                print(f"  URL: {url}")
                print(f"    Category: {match['category']}")
                print(f"    Pattern:  {match['pattern']}")
                print()
        else:
            print(f"  URL: {url}")
            print(f"    No matches")
            print()


def demo_batch_query(client: PrefixMatchClient):
    """Demonstrate batch URL queries."""
    print("\n=== Batch Query Demo ===\n")

    urls = [
        "https://cnn.com/politics/article",
        "https://espn.com/nfl/scores",
        "https://bmw.com/models",
        "https://random.com/page",
        "https://bloomberg.com/markets",
    ]

    start = time.time()
    result = client.query(urls)
    elapsed = (time.time() - start) * 1000

    print(f"  Queried {len(urls)} URLs in {elapsed:.2f}ms")
    print(f"  Request ID: {result['id']}")
    print()

    for r in result.get("results", []):
        url = r["query"]
        matches = r.get("matches", [])
        if matches:
            categories = [m["category"] for m in matches]
            print(f"  {url}")
            print(f"    -> {', '.join(categories)}")
        else:
            print(f"  {url}")
            print(f"    -> (no match)")


def demo_performance(client: PrefixMatchClient, iterations: int = 1000):
    """Measure query performance."""
    print("\n=== Performance Test ===\n")

    test_url = "https://cnn.com/politics/2024/election-coverage"

    # Warm up
    for _ in range(10):
        client.query_single(test_url)

    # Measure single queries
    start = time.time()
    for _ in range(iterations):
        client.query_single(test_url)
    elapsed = time.time() - start

    print(f"  Single queries: {iterations} iterations")
    print(f"  Total time: {elapsed*1000:.2f}ms")
    print(f"  Per query: {elapsed*1000/iterations:.3f}ms")
    print(f"  Throughput: {iterations/elapsed:.0f} queries/sec")
    print()

    # Measure batch queries
    batch_size = 100
    batch = [test_url] * batch_size
    batch_iterations = iterations // batch_size

    start = time.time()
    for _ in range(batch_iterations):
        client.query(batch)
    elapsed = time.time() - start

    total_queries = batch_iterations * batch_size
    print(f"  Batch queries: {batch_iterations} batches x {batch_size}")
    print(f"  Total time: {elapsed*1000:.2f}ms")
    print(f"  Per query: {elapsed*1000/total_queries:.3f}ms")
    print(f"  Throughput: {total_queries/elapsed:.0f} queries/sec")


def process_file(
    client: PrefixMatchClient,
    filepath: str,
    batch_size: int = 100,
    verbose: bool = False
):
    """Process a file of URLs."""
    print(f"\n=== Processing File: {filepath} ===\n")

    # Read URLs
    with open(filepath, "r") as f:
        urls = [line.strip() for line in f if line.strip() and not line.startswith("#")]

    print(f"  Loaded {len(urls)} URLs")

    total_matches = 0
    start = time.time()

    # Process in batches
    for i in range(0, len(urls), batch_size):
        batch = urls[i:i + batch_size]
        result = client.query(batch)

        for r in result.get("results", []):
            matches = r.get("matches", [])
            total_matches += len(matches)

            if verbose and matches:
                print(f"  {r['query']}: {[m['category'] for m in matches]}")

        # Progress
        progress = min(i + batch_size, len(urls))
        if progress % 1000 == 0 or progress == len(urls):
            print(f"  Processed {progress}/{len(urls)} URLs...")

    elapsed = time.time() - start

    print()
    print(f"  Total URLs: {len(urls)}")
    print(f"  Total matches: {total_matches}")
    print(f"  Time: {elapsed:.2f}s")
    print(f"  Throughput: {len(urls)/elapsed:.0f} URLs/sec")


def main():
    parser = argparse.ArgumentParser(
        description="PrefixMatch Python Client",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Connect via TCP and run demo
    python python_client.py --port 9999

    # Connect via Unix socket
    python python_client.py --socket /tmp/pm.sock

    # Process URL file
    python python_client.py --port 9999 --file urls.txt

    # Performance test
    python python_client.py --port 9999 --perf
        """
    )

    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", type=int, default=9999, help="Server port")
    parser.add_argument("--socket", "-s", help="Unix socket path")
    parser.add_argument("--file", "-f", help="File of URLs to process")
    parser.add_argument("--batch-size", "-b", type=int, default=100, help="Batch size")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--perf", action="store_true", help="Run performance test")

    args = parser.parse_args()

    try:
        with PrefixMatchClient(
            host=args.host,
            port=args.port,
            unix_socket=args.socket
        ) as client:
            print(f"Connected to {client.endpoint}")

            if args.file:
                process_file(client, args.file, args.batch_size, args.verbose)
            elif args.perf:
                demo_performance(client)
            else:
                demo_single_queries(client)
                demo_batch_query(client)

    except ConnectionRefusedError:
        print(f"Error: Could not connect to server", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
