#!/usr/bin/env python3
"""
LG-HW-001 Hardware Coexistence Soak Test Load Generator
Stress network and HTTP API activity concurrently while device executes
the hardware coexistence test (8-lane RMT + 8-channel PCA9685).

Stdlib only (no external pip dependencies).
"""

import argparse
import json
import sys
import time
import threading
import urllib.request
import urllib.error
from typing import Dict, Any, List

class SoakMetrics:
    def __init__(self):
        self.lock = threading.Lock()
        self.total_requests = 0
        self.successful_requests = 0
        self.failed_requests = 0
        self.latencies_ms: List[float] = []
        self.errors: Dict[str, int] = {}
        self.reboots_detected = 0
        self.last_uptime_s = -1

    def record_success(self, latency_ms: float):
        with self.lock:
            self.total_requests += 1
            self.successful_requests += 1
            self.latencies_ms.append(latency_ms)

    def record_failure(self, error_type: str):
        with self.lock:
            self.total_requests += 1
            self.failed_requests += 1
            self.errors[error_type] = self.errors.get(error_type, 0) + 1

    def check_reboot(self, current_uptime_s: float):
        with self.lock:
            if self.last_uptime_s > 0 and current_uptime_s < (self.last_uptime_s - 2):
                self.reboots_detected += 1
                print(f"\n[ALERT] Device reboot detected! Previous uptime: {self.last_uptime_s}s, New uptime: {current_uptime_s}s")
            self.last_uptime_s = current_uptime_s


def make_request(url: str, admin_key: str, timeout: float = 3.0) -> tuple:
    headers = {
        "Authorization": f"Bearer {admin_key}",
        "X-API-Key": admin_key,
        "User-Agent": "LumiGrid-HW001-Soak/1.0",
        "Accept": "application/json",
    }
    req = urllib.request.Request(url, headers=headers)
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
            elapsed_ms = (time.time() - t0) * 1000.0
            return (True, resp.status, data, elapsed_ms)
    except urllib.error.HTTPError as e:
        elapsed_ms = (time.time() - t0) * 1000.0
        return (False, e.code, str(e), elapsed_ms)
    except urllib.error.URLError as e:
        elapsed_ms = (time.time() - t0) * 1000.0
        return (False, 0, str(e.reason), elapsed_ms)
    except Exception as e:
        elapsed_ms = (time.time() - t0) * 1000.0
        return (False, 0, str(e), elapsed_ms)


def worker_loop(worker_id: int, base_url: str, admin_key: str, end_time: float, metrics: SoakMetrics):
    endpoints = [
        "/api/v1/status",
        "/api/v1/diagnostics/output",
        "/api/v1/diagnostics/memory",
        "/api/v1/diagnostics/storage",
    ]
    idx = worker_id % len(endpoints)

    while time.time() < end_time:
        ep = endpoints[idx]
        idx = (idx + 1) % len(endpoints)
        url = f"{base_url}{ep}"

        ok, code, data, elapsed_ms = make_request(url, admin_key)
        if ok and 200 <= code < 300:
            metrics.record_success(elapsed_ms)
        else:
            err_msg = f"HTTP_{code}" if code else f"ERR_{str(data)[:30]}"
            metrics.record_failure(err_msg)

        time.sleep(0.02)


def main():
    parser = argparse.ArgumentParser(description="LG-HW-001 Hardware Coexistence Soak Test")
    parser.add_argument("--host", default="192.168.4.1", help="Device IP address / hostname")
    parser.add_argument("--port", type=int, default=80, help="Device HTTP port")
    parser.add_argument("--admin-key", required=True, help="Admin API authentication token")
    parser.add_argument("--duration", type=int, default=600, help="Soak duration in seconds (default: 600)")
    parser.add_argument("--workers", type=int, default=6, help="Concurrent HTTP worker threads (default: 6)")
    args = parser.parse_args()

    base_url = f"http://{args.host}:{args.port}"
    print("==================================================================")
    print("   LumiGrid LG-HW-001 — Hardware Coexistence Soak Test Load")
    print("==================================================================")
    print(f"Target:      {base_url}")
    print(f"Duration:    {args.duration} seconds ({args.duration / 60:.1f} minutes)")
    print(f"Workers:     {args.workers} threads")
    print("------------------------------------------------------------------")

    # Initial probe to verify reachability
    print("[*] Probing target device...")
    ok, code, data, elapsed = make_request(f"{base_url}/api/v1/diagnostics/output", args.admin_key, timeout=5.0)
    if not ok:
        print(f"[ERROR] Cannot connect to device: {code} / {data}")
        sys.exit(1)

    print(f"[OK] Device reachable (RTT {elapsed:.1f} ms)")
    try:
        init_diag = json.loads(data)
        rmt_init = init_diag.get("rmt", {})
        pwm_init = init_diag.get("pwm", {})
        print(f"[*] Initial RMT: core={rmt_init.get('core')} prio={rmt_init.get('priority')} iramSafe={rmt_init.get('iramSafe')} activeLanes={rmt_init.get('activeLanes')}")
        print(f"[*] Initial PWM: addr=0x{pwm_init.get('address', 0):02X} initCore={pwm_init.get('initCore')} available={pwm_init.get('available')}")
    except Exception as e:
        print(f"[*] Raw probe response received: {e}")

    metrics = SoakMetrics()
    start_time = time.time()
    end_time = start_time + args.duration

    # Start workers
    threads = []
    for wid in range(args.workers):
        t = threading.Thread(target=worker_loop, args=(wid, base_url, args.admin_key, end_time, metrics), daemon=True)
        t.start()
        threads.append(t)

    print(f"[*] Started {args.workers} worker threads. Running soak...")

    # Periodic telemetry monitor
    last_print = time.time()
    latest_rmt = {}
    latest_pwm = {}

    try:
        while time.time() < end_time:
            time.sleep(2.0)
            now = time.time()
            if now - last_print >= 5.0:
                last_print = now
                elapsed_total = now - start_time
                remaining = max(0.0, end_time - now)

                # Fetch latest output diagnostics
                dok, _, ddata, _ = make_request(f"{base_url}/api/v1/diagnostics/output", args.admin_key, timeout=3.0)
                if dok:
                    try:
                        diag_json = json.loads(ddata)
                        latest_rmt = diag_json.get("rmt", {})
                        latest_pwm = diag_json.get("pwm", {})
                    except Exception:
                        pass

                # Fetch status for uptime
                sok, _, sdata, _ = make_request(f"{base_url}/api/v1/status", args.admin_key, timeout=3.0)
                if sok:
                    try:
                        stat_json = json.loads(sdata)
                        uptime_s = stat_json.get("uptime", stat_json.get("uptimeSeconds", -1))
                        if uptime_s > 0:
                            metrics.check_reboot(uptime_s)
                    except Exception:
                        pass

                with metrics.lock:
                    req_cnt = metrics.total_requests
                    fail_cnt = metrics.failed_requests
                    succ_cnt = metrics.successful_requests
                    rps = succ_cnt / elapsed_total if elapsed_total > 0 else 0

                rmt_frames = latest_rmt.get("frames", 0)
                rmt_fails = latest_rmt.get("failedFrames", 0)
                pca_tx = latest_pwm.get("transactions", 0)
                pca_fails = latest_pwm.get("transactionFailures", 0)

                sys.stdout.write(
                    f"\r[{elapsed_total:5.1f}s / {args.duration}s] "
                    f"HTTP: {succ_cnt} ok, {fail_cnt} err ({rps:.1f} rps) | "
                    f"RMT: {rmt_frames} frames, {rmt_fails} err | "
                    f"PCA: {pca_tx} tx, {pca_fails} err | "
                    f"Reboots: {metrics.reboots_detected}   "
                )
                sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[*] Soak test interrupted by operator.")

    for t in threads:
        t.join(timeout=1.0)

    total_time = time.time() - start_time
    print("\n\n==================================================================")
    print("                    SOAK TEST FINAL REPORT")
    print("==================================================================")
    print(f"Elapsed Time:       {total_time:.2f} seconds")
    print(f"Total Requests:     {metrics.total_requests}")
    print(f"Successful:         {metrics.successful_requests}")
    print(f"Failed:             {metrics.failed_requests}")
    print(f"Reboots Detected:   {metrics.reboots_detected}")

    if metrics.latencies_ms:
        sorted_lat = sorted(metrics.latencies_ms)
        avg_lat = sum(sorted_lat) / len(sorted_lat)
        p50 = sorted_lat[len(sorted_lat) // 2]
        p95 = sorted_lat[int(len(sorted_lat) * 0.95)]
        max_lat = sorted_lat[-1]
        print(f"Latency (ms):       avg={avg_lat:.1f}  p50={p50:.1f}  p95={p95:.1f}  max={max_lat:.1f}")

    if metrics.errors:
        print(f"Error Breakdown:    {metrics.errors}")

    print("------------------------------------------------------------------")
    print("Hardware Diagnostics Snapshot:")
    print(f"RMT Allocation Core:      {latest_rmt.get('core', 'N/A')}")
    print(f"RMT Priority:             {latest_rmt.get('priority', 'N/A')}")
    print(f"RMT IRAM Safe:            {latest_rmt.get('iramSafe', 'N/A')}")
    print(f"RMT Active Lanes:         {latest_rmt.get('activeLanes', 'N/A')}")
    print(f"RMT Total Frames:         {latest_rmt.get('frames', 'N/A')}")
    print(f"RMT Failed Frames:        {latest_rmt.get('failedFrames', 'N/A')}")
    print(f"PCA Address:              0x{latest_pwm.get('address', 0):02X}")
    print(f"PCA Init Core:            {latest_pwm.get('initCore', 'N/A')}")
    print(f"PCA Total Transactions:   {latest_pwm.get('transactions', 'N/A')}")
    print(f"PCA Failures:             {latest_pwm.get('transactionFailures', 'N/A')}")
    print(f"PCA Consecutive Failures: {latest_pwm.get('consecutiveFailures', 'N/A')}")
    print("------------------------------------------------------------------")

    # Evaluation against LG-HW-001 criteria
    passed = True
    if metrics.reboots_detected > 0:
        print("[FAIL] Unexpected reboots detected during soak!")
        passed = False
    if latest_rmt.get("failedFrames", 0) > 0:
        print("[FAIL] RMT failed frames observed!")
        passed = False
    if latest_pwm.get("transactionFailures", 0) > 0:
        print("[FAIL] PCA transaction failures observed!")
        passed = False
    if metrics.failed_requests > (metrics.total_requests * 0.05):
        print("[FAIL] Excessive HTTP failure rate (>5%)!")
        passed = False

    if passed:
        print("[PASS] LG-HW-001 Hardware Coexistence Soak Test PASSED!")
    else:
        print("[FAIL] LG-HW-001 Hardware Coexistence Soak Test FAILED criteria.")
    print("==================================================================")

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
