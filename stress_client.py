#!/usr/bin/env python3
"""
stress_client.py — Mini-Redis Stress Test
==========================================
Hammers the server with concurrent SET and GET requests to measure
throughput (requests per second) and verify correctness.
"""

import socket
import threading
import time
import argparse
import sys
import struct  # Added for socket option packing
from statistics import mean, median

# ---- low-level helpers -------------------------------------------------------

def send_command(host: str, port: int, command: str) -> str:
    """
    Open a fresh TCP connection, send one command, read the response, close.
    This matches the server's one-command-per-connection model.
    """
    with socket.create_connection((host, port), timeout=5.0) as sock:
        # --- FIX: HARD DISCONNECT TO BYPASS WINDOWS PORT EXHAUSTION ---
        # This tells the OS to reset the connection immediately rather than
        # entering a TIME_WAIT state, preventing 'WinError 10048'.
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
        # --------------------------------------------------------------
        
        sock.sendall((command + "\r\n").encode())
        # Read until we get a full response line
        data = b""
        while not data.endswith(b"\r\n"):
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
    return data.decode().strip()


def ping(host: str, port: int) -> bool:
    """Check server is alive before starting the stress test."""
    try:
        resp = send_command(host, port, "PING")
        return resp == "+PONG"
    except Exception as e:
        print(f"  [!] PING failed: {e}")
        return False


# ---- worker functions --------------------------------------------------------

def set_worker(host, port, key_prefix, count, results, errors, latencies):
    """
    Thread worker: performs `count` SET commands.
    """
    local_latencies = []
    for i in range(count):
        key   = f"{key_prefix}:{i}"
        value = f"value_{i}"
        cmd   = f"SET {key} {value}"
        t0    = time.perf_counter()
        try:
            resp = send_command(host, port, cmd)
            elapsed_ms = (time.perf_counter() - t0) * 1000
            local_latencies.append(elapsed_ms)
            if resp == "+OK":
                results["ok"] += 1
            else:
                errors.append(f"SET {key} got: {resp}")
        except Exception as e:
            errors.append(f"SET {key} exception: {e}")

    latencies.extend(local_latencies)


def get_worker(host, port, key_prefix, count, results, errors, latencies):
    """
    Thread worker: performs `count` GET commands and verifies the value.
    """
    local_latencies = []
    for i in range(count):
        key      = f"{key_prefix}:{i}"
        expected = f"value_{i}"
        cmd      = f"GET {key}"
        t0       = time.perf_counter()
        try:
            resp = send_command(host, port, cmd)
            elapsed_ms = (time.perf_counter() - t0) * 1000
            local_latencies.append(elapsed_ms)
            if resp == f"${expected}":
                results["ok"] += 1
            else:
                errors.append(f"GET {key} expected '${expected}' got '{resp}'")
        except Exception as e:
            errors.append(f"GET {key} exception: {e}")

    latencies.extend(local_latencies)


# ---- runner ------------------------------------------------------------------

def run_phase(name, worker_fn, host, port, total_requests, num_threads):
    """
    Splits `total_requests` across `num_threads` threads, runs them
    concurrently, and returns throughput + latency stats.
    """
    per_thread   = total_requests // num_threads
    results      = {"ok": 0}
    error_store   = []
    latency_store = []

    threads = []
    for t in range(num_threads):
        key_prefix = f"thread{t}"
        t_results  = {"ok": 0}
        t_errors   = []
        t_latencies= []
        th = threading.Thread(
            target=worker_fn,
            args=(host, port, key_prefix, per_thread,
                  t_results, t_errors, t_latencies)
        )
        threads.append((th, t_results, t_errors, t_latencies))

    start = time.perf_counter()
    for th, _, _, _ in threads:
        th.start()
    for th, t_results, t_errors, t_latencies in threads:
        th.join()
        results["ok"]    += t_results["ok"]
        error_store.extend(t_errors)
        latency_store.extend(t_latencies)

    elapsed = time.perf_counter() - start
    rps     = total_requests / elapsed if elapsed > 0 else 0

    avg_lat = mean(latency_store) if latency_store else 0
    med_lat = median(latency_store) if latency_store else 0
    p99_lat = sorted(latency_store)[int(len(latency_store) * 0.99)] if latency_store else 0

    print(f"\n  [{name}]")
    print(f"    Requests   : {total_requests:,}")
    print(f"    Completed  : {results['ok']:,}")
    print(f"    Errors     : {len(error_store)}")
    print(f"    Duration   : {elapsed:.2f}s")
    print(f"    Throughput : {rps:,.0f} req/s")
    print(f"    Latency avg: {avg_lat:.2f} ms")
    print(f"    Latency med: {med_lat:.2f} ms")
    print(f"    Latency p99: {p99_lat:.2f} ms")

    if error_store:
        print(f"\n  First 5 errors:")
        for e in error_store[:5]:
            print(f"    - {e}")

    return rps, len(error_store)


# ---- main --------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Mini-Redis stress test")
    parser.add_argument("--host",     default="127.0.0.1")
    parser.add_argument("--port",     type=int, default=6379)
    parser.add_argument("--requests", type=int, default=50_000,
                        help="Total number of requests per phase")
    parser.add_argument("--threads",  type=int, default=16,
                        help="Number of concurrent client threads")
    args = parser.parse_args()

    print("=" * 55)
    print("  Mini-Redis Stress Test")
    print("=" * 55)
    print(f"  Target   : {args.host}:{args.port}")
    print(f"  Requests : {args.requests:,} per phase")
    print(f"  Threads  : {args.threads}")

    print("\n  Checking server connectivity...")
    if not ping(args.host, args.port):
        print("  Server did not respond to PING. Is it running?")
        sys.exit(1)
    print("  Server is up!")

    set_rps, set_errors = run_phase(
        "Phase 1: SET", set_worker,
        args.host, args.port, args.requests, args.threads
    )

    get_rps, get_errors = run_phase(
        "Phase 2: GET", get_worker,
        args.host, args.port, args.requests, args.threads
    )

    total_errors = set_errors + get_errors
    print("\n" + "=" * 55)
    print("  Summary")
    print("=" * 55)
    print(f"  SET throughput : {set_rps:,.0f} req/s")
    print(f"  GET throughput : {get_rps:,.0f} req/s")
    print(f"  Total errors   : {total_errors}")
    print(f"  Result         : {'PASS ✓' if total_errors == 0 else 'FAIL ✗'}")
    print("=" * 55)

    sys.exit(0 if total_errors == 0 else 1)


if __name__ == "__main__":
    main()