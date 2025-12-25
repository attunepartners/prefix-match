#!/usr/bin/env python3
"""
Prefix Match Client - sends queries to the prefix_match server

Usage:
    Single query:
        ./client.py -q "string to match"

    Batch from file:
        ./client.py -f strings.txt
        ./client.py -f strings.txt.gz

    Options:
        -h, --host HOST     Server host (default: localhost)
        -p, --port PORT     Server port (default: 9999)
        -s, --socket PATH   Unix socket path (instead of TCP)
        -b, --batch SIZE    Batch size for file mode (default: 100)
        -q, --query STRING  Single query string
        -f, --file FILE     File with strings (one per line, supports .gz)
        -o, --output FILE   Output file for results (default: stdout)
        -v, --verbose       Verbose output
"""

import socket
import json
import gzip
import sys
import time
import argparse


def connect(host, port, socket_path):
    """Connect to server via TCP or Unix socket"""
    if socket_path:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(socket_path)
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
    sock.settimeout(60)
    return sock


def send_request(sock, request_id, queries):
    """Send a request and receive response"""
    if isinstance(queries, str):
        req = {"id": request_id, "query": queries}
    else:
        req = {"id": request_id, "queries": queries}

    data = json.dumps(req) + "\n"
    sock.sendall(data.encode())

    # Read response until newline
    resp = b""
    while b"\n" not in resp:
        chunk = sock.recv(1048576)
        if not chunk:
            break
        resp += chunk

    return json.loads(resp.decode().strip())


def format_result(result, verbose=False):
    """Format a single match result"""
    if verbose:
        return f"  category={result['category']} id={result['id']} pattern=\"{result['pattern']}\" match=\"{result['match']}\""
    else:
        return f"{result['category']}\t{result['id']}\t{result['pattern']}\t{result['match']}"


def main():
    parser = argparse.ArgumentParser(description='Prefix Match Client')
    parser.add_argument('-H', '--host', default='localhost', help='Server host')
    parser.add_argument('-p', '--port', type=int, default=9999, help='Server port')
    parser.add_argument('-s', '--socket', help='Unix socket path')
    parser.add_argument('-b', '--batch', type=int, default=100, help='Batch size')
    parser.add_argument('-q', '--query', help='Single query string')
    parser.add_argument('-f', '--file', help='File with strings to query')
    parser.add_argument('-o', '--output', help='Output file')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    if not args.query and not args.file:
        parser.print_help()
        sys.exit(1)

    # Setup output
    out = open(args.output, 'w') if args.output else sys.stdout

    # Connect
    sock = connect(args.host, args.port, args.socket)

    start_time = time.time()
    total_queries = 0
    total_matches = 0

    try:
        if args.query:
            # Single query mode
            resp = send_request(sock, "q1", args.query)
            total_queries = 1

            if resp['status'] == 200:
                for match in resp.get('results', []):
                    total_matches += 1
                    print(format_result(match, args.verbose), file=out)
            elif args.verbose:
                print(f"No matches (status {resp['status']})", file=sys.stderr)

        else:
            # File mode
            if args.verbose:
                print(f"Loading strings from {args.file}...", file=sys.stderr)

            if args.file.endswith('.gz'):
                with gzip.open(args.file, 'rt') as f:
                    lines = [line.strip() for line in f if line.strip()]
            else:
                with open(args.file, 'r') as f:
                    lines = [line.strip() for line in f if line.strip()]

            if args.verbose:
                print(f"Loaded {len(lines)} strings, processing in batches of {args.batch}", file=sys.stderr)

            # Process in batches
            for batch_start in range(0, len(lines), args.batch):
                batch_end = min(batch_start + args.batch, len(lines))
                batch = lines[batch_start:batch_end]
                batch_id = f"b{batch_start}"

                resp = send_request(sock, batch_id, batch)
                total_queries += len(batch)

                if resp['status'] in (200, 404):
                    for i, result in enumerate(resp.get('results', [])):
                        line_idx = batch_start + result['index']
                        for match in result.get('matches', []):
                            total_matches += 1
                            if args.verbose:
                                print(f"[{line_idx}] {lines[line_idx][:50]}...", file=out)
                                print(format_result(match, True), file=out)
                            else:
                                print(f"{line_idx}\t{format_result(match)}", file=out)

                # Progress
                if args.verbose and batch_start % (max(1, len(lines) // 10) * args.batch // args.batch * args.batch) < args.batch:
                    elapsed = time.time() - start_time
                    rate = total_queries / elapsed if elapsed > 0 else 0
                    pct = batch_end / len(lines) * 100
                    print(f"Progress: {pct:.0f}% ({total_queries} queries, {rate:.0f}/sec)", file=sys.stderr)

    finally:
        sock.close()
        if out != sys.stdout:
            out.close()

    elapsed = time.time() - start_time

    if args.verbose:
        print(f"\nCompleted: {total_queries} queries, {total_matches} matches in {elapsed:.2f}s", file=sys.stderr)
        if total_queries > 0:
            print(f"Throughput: {total_queries/elapsed:.0f} queries/sec", file=sys.stderr)


if __name__ == "__main__":
    main()
