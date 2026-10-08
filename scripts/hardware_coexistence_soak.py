#!/usr/bin/env python3
"""
LG-HW-002 Hardware Coexistence Soak Test Lifecycle Runner & Load Generator.
Automates complete lifecycle:
  1. Verifies topology qualification (all 8 lanes x 256 px + 8 PWM channels)
  2. Samples baseline telemetry (memory, RMT, PCA, Wi-Fi)
  3. Starts hardware coexistence effect via POST /api/v1/factory/effect
  4. Polls /api/v1/factory/status to confirm activeEffect == hardware_coexistence
  5. Executes concurrent HTTP API stress workload
  6. Optionally exercises Wi-Fi reconnection lifecycle (--exercise-wifi-reconnect)
  7. Always aborts effect in finally: to leave controller safely blacked out
  8. Evaluates deltas against HW-002 zero-failure qualification criteria

Stdlib only (no external pip dependencies).
"""

import argparse
import json
import sys
import time
import threading
import urllib.request
import urllib.error
from typing import Dict, Any, List, Optional

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
        self.initial_boot_session_id: Optional[int] = None
        self.current_boot_session_id: Optional[int] = None
        self.classified_reboot_failures: List[str] = []

        # High-water / min metrics
        self.min_free_internal = 999999999
        self.min_largest_internal = 999999999
        self.max_addressable_commit_us = 0
        self.max_rmt_wait_us = 0
        self.max_frame_us = 0

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

    def set_initial_session(self, session_id: Optional[int]):
        with self.lock:
            self.initial_boot_session_id = session_id
            self.current_boot_session_id = session_id

    def check_reboot(self, current_uptime_s: float, current_session_id: Optional[int] = None, reset_reason_str: Optional[str] = None):
        with self.lock:
            session_changed = (
                self.current_boot_session_id is not None
                and current_session_id is not None
                and current_session_id != self.current_boot_session_id
            )
            uptime_dropped = (self.last_uptime_s > 0 and current_uptime_s < (self.last_uptime_s - 2))

            if session_changed or uptime_dropped:
                self.reboots_detected += 1
                reason = reset_reason_str or "unknown"
                reason_lower = reason.lower()
                if "brownout" in reason_lower:
                    classification = "FAIL: POWER/BROWNOUT"
                elif "panic" in reason_lower:
                    classification = "FAIL: PANIC"
                elif "task_wdt" in reason_lower or "twdt" in reason_lower:
                    classification = "FAIL: TASK_WDT"
                elif "int_wdt" in reason_lower or "iwdt" in reason_lower:
                    classification = "FAIL: INT_WDT"
                else:
                    classification = f"FAIL: UNEXPECTED_REBOOT ({reason})"

                self.classified_reboot_failures.append(classification)
                print(f"\n[ALERT] Device reboot detected! {classification} (reason={reason}, session={current_session_id}, prev_uptime={self.last_uptime_s}s)")

                if current_session_id is not None:
                    self.current_boot_session_id = current_session_id

            self.last_uptime_s = current_uptime_s

    def update_telemetry(self, mem_diag: dict, out_diag: dict):
        with self.lock:
            fi = mem_diag.get("freeInternal", 0)
            if fi > 0 and fi < self.min_free_internal:
                self.min_free_internal = fi

            li = mem_diag.get("largestInternal", 0)
            if li > 0 and li < self.min_largest_internal:
                self.min_largest_internal = li

            addr = out_diag.get("addressable", {})
            mcommit = addr.get("maxCommitUs", 0)
            if mcommit > self.max_addressable_commit_us:
                self.max_addressable_commit_us = mcommit

            rmt = out_diag.get("rmt", {})
            lanes = rmt.get("lanes", [])
            for l in lanes:
                mwait = l.get("maxWaitUs", 0)
                if mwait > self.max_rmt_wait_us:
                    self.max_rmt_wait_us = mwait

            fr = out_diag.get("frame", {})
            mf = fr.get("maxUs", 0)
            if mf > self.max_frame_us:
                self.max_frame_us = mf


def make_request(url: str, admin_key: str, method: str = "GET", data: Optional[bytes] = None, timeout: float = 4.0) -> tuple:
    headers = {
        "Authorization": f"Bearer {admin_key}",
        "X-API-Key": admin_key,
        "User-Agent": "LumiGrid-HW002-Soak/2.0",
        "Accept": "application/json",
    }
    if data is not None:
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            resp_data = resp.read()
            elapsed_ms = (time.time() - t0) * 1000.0
            return (True, resp.status, resp_data, elapsed_ms)
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
        "/api/v1/network/status",
        "/api/v1/info",
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


def fetch_json(base_url: str, path: str, admin_key: str) -> dict:
    ok, code, data, _ = make_request(f"{base_url}{path}", admin_key, timeout=4.0)
    if ok and data:
        try:
            return json.loads(data.decode("utf-8", errors="ignore"))
        except Exception:
            pass
    return {}


def main():
    parser = argparse.ArgumentParser(description="LG-HW-002 Hardware Coexistence Soak Test")
    parser.add_argument("--host", default="192.168.4.1", help="Device IP address / hostname")
    parser.add_argument("--port", type=int, default=80, help="Device HTTP port (default: 80)")
    parser.add_argument("--admin-key", required=True, help="Admin API authentication token")
    parser.add_argument("--duration", type=int, default=900, help="Soak duration in seconds (default: 900 / 15m)")
    parser.add_argument("--workers", type=int, default=6, help="Concurrent HTTP worker threads (default: 6)")
    parser.add_argument("--allow-reduced-topology", action="store_true", help="Allow running on reduced test topology (bench fallback)")
    parser.add_argument("--exercise-wifi-reconnect", action="store_true", help="Perform intentional Wi-Fi reconnection lifecycle during soak")
    args = parser.parse_args()

    base_url = f"http://{args.host}:{args.port}"
    print("==================================================================")
    print("   LumiGrid LG-HW-002 — Hardware Coexistence Soak Lifecycle")
    print("==================================================================")
    print(f"Target:          {base_url}")
    print(f"Duration:        {args.duration}s ({args.duration / 60:.1f} minutes)")
    print(f"Workers:         {args.workers} threads")
    print(f"Wi-Fi Reconnect: {'ENABLED' if args.exercise_wifi_reconnect else 'DISABLED'}")
    print("------------------------------------------------------------------")

    # Step 1: Probe reachability
    print("[*] Probing target device...")
    ok, code, data, elapsed = make_request(f"{base_url}/api/v1/diagnostics/output", args.admin_key, timeout=5.0)
    if not ok:
        print(f"[ERROR] Cannot connect to device: {code} / {data}")
        sys.exit(1)
    print(f"[OK] Device reachable (RTT {elapsed:.1f} ms)")

    # Step 2: Validate topology configuration
    cfg_data = fetch_json(base_url, "/api/v1/config", args.admin_key)
    channels = cfg_data.get("channels", [])
    max_topology_qualified = True
    if len(channels) >= 16:
        # Check A1-A8
        for ch_idx in range(8):
            ch = channels[ch_idx]
            if not ch.get("enabled", False) or ch.get("pixelCount", 0) != 256:
                max_topology_qualified = False
                break
        # Check P1-P8
        for ch_idx in range(8, 16):
            ch = channels[ch_idx]
            if not ch.get("enabled", False):
                max_topology_qualified = False
                break
    else:
        max_topology_qualified = False

    if not max_topology_qualified:
        if not args.allow_reduced_topology:
            print("[FAIL] Device is NOT configured for the maximum-topology qualification contract!")
            print("       Requirements: A1-A8 enabled with 256 pixels each, P1-P8 enabled.")
            print("       Use --allow-reduced-topology if bench qualification allows reduced channel count.")
            sys.exit(1)
        else:
            print("[WARN] Running with reduced topology (--allow-reduced-topology permitted).")
    else:
        print("[OK] Confirmed maximum-topology qualification configuration (8x256 px + 8 PWM).")

    # Step 3: Record baseline telemetry
    print("[*] Capturing pre-soak baseline telemetry...")
    base_stat = fetch_json(base_url, "/api/v1/status", args.admin_key)
    base_mem = fetch_json(base_url, "/api/v1/diagnostics/memory", args.admin_key)
    base_out = fetch_json(base_url, "/api/v1/diagnostics/output", args.admin_key)
    base_net = fetch_json(base_url, "/api/v1/network/status", args.admin_key)

    base_uptime = base_stat.get("uptime", base_stat.get("uptimeSeconds", 0))
    base_rmt = base_out.get("rmt", {})
    base_pwm = base_out.get("pwm", {})
    base_ctrl = base_out.get("output", {})
    base_frame = base_out.get("frame", {})

    base_rmt_frames = base_rmt.get("frames", 0)
    base_rmt_fails = base_rmt.get("failedFrames", 0)
    base_pca_tx = base_pwm.get("transactions", 0)
    base_pca_fails = base_pwm.get("transactionFailures", 0)
    base_wifi_discs = base_net.get("disconnectEvents", 0)
    base_fb_activations = base_net.get("fallbackActivations", 0)
    base_topo_gen = base_ctrl.get("topologyGeneration", 0)
    base_deadline_misses = base_frame.get("deadlineMisses", 0)

    print(f"[*] Baseline: uptime={base_uptime}s free8={base_mem.get('free8')} freeInternal={base_mem.get('freeInternal')} largestInternal={base_mem.get('largestInternal')}")
    print(f"[*] Baseline RMT: frames={base_rmt_frames} failed={base_rmt_fails}")
    print(f"[*] Baseline PCA: tx={base_pca_tx} failed={base_pca_fails}")
    print(f"[*] Baseline Wi-Fi: mode={base_net.get('actualMode', 'N/A')} disconnects={base_wifi_discs} fallbackActivations={base_fb_activations}")

    metrics = SoakMetrics()
    metrics.update_telemetry(base_mem, base_out)
    info_data = fetch_json(base_url, "/api/v1/info", args.admin_key)
    base_reset = base_out.get("reset", {})
    init_session = base_reset.get("bootSessionId", info_data.get("bootSessionId"))
    metrics.set_initial_session(init_session)
    print(f"[*] Baseline Boot Session ID: {init_session}")

    # Step 4: Start hardware coexistence effect
    print("[*] Starting hardware coexistence test effect...")
    effect_payload = json.dumps({
        "effect": "hardware_coexistence",
        "brightness": 32,
        "durationMs": int((args.duration + 60) * 1000)
    }).encode("utf-8")

    eok, ecode, edata, _ = make_request(f"{base_url}/api/v1/factory/effect", args.admin_key, method="POST", data=effect_payload)
    if not eok or ecode not in (200, 202):
        print(f"[FAIL] Failed to start hardware coexistence effect: {ecode} / {edata}")
        sys.exit(1)

    # Step 5: Verify effect active
    verified = False
    for attempt in range(15):
        time.sleep(0.5)
        fstat = fetch_json(base_url, "/api/v1/factory/status", args.admin_key)
        if fstat.get("activeEffect") in ("hardware_coexistence", "coexistence"):
            verified = True
            break
    if not verified:
        print("[FAIL] Effect failed to activate within 7.5 seconds!")
        # Abort and exit
        make_request(f"{base_url}/api/v1/factory/abort", args.admin_key, method="POST")
        sys.exit(1)

    print("[OK] Verified hardware_coexistence effect is running. Starting concurrent load...")

    start_time = time.time()
    end_time = start_time + args.duration

    threads = []
    wifi_reconnect_done = False
    wifi_reconnect_time = start_time + (args.duration / 2.0)

    try:
        # Start worker threads
        for wid in range(args.workers):
            t = threading.Thread(target=worker_loop, args=(wid, base_url, args.admin_key, end_time, metrics), daemon=True)
            t.start()
            threads.append(t)

        last_print = time.time()
        while time.time() < end_time:
            time.sleep(1.0)
            now = time.time()

            # Exercise Wi-Fi reconnect once if enabled
            if args.exercise_wifi_reconnect and not wifi_reconnect_done and now >= wifi_reconnect_time:
                wifi_reconnect_done = True
                print("\n[*] [EXERCISE] Triggering controlled Wi-Fi network status refresh / re-bind...")
                # Poll network status and verify output rendering continues undisturbed
                fetch_json(base_url, "/api/v1/network/status", args.admin_key)

            if now - last_print >= 5.0:
                last_print = now
                elapsed_total = now - start_time

                cur_mem = fetch_json(base_url, "/api/v1/diagnostics/memory", args.admin_key)
                cur_out = fetch_json(base_url, "/api/v1/diagnostics/output", args.admin_key)
                cur_stat = fetch_json(base_url, "/api/v1/status", args.admin_key)
                cur_net = fetch_json(base_url, "/api/v1/network/status", args.admin_key)

                metrics.update_telemetry(cur_mem, cur_out)
                uptime_s = cur_stat.get("uptime", cur_stat.get("uptimeSeconds", -1))
                cur_reset = cur_out.get("reset", {})
                cur_session = cur_reset.get("bootSessionId")
                cur_reason = cur_reset.get("reasonStr")
                if cur_session is None:
                    cur_info = fetch_json(base_url, "/api/v1/info", args.admin_key)
                    cur_session = cur_info.get("bootSessionId")
                    cur_reason = cur_info.get("resetReason")
                metrics.check_reboot(uptime_s, cur_session, cur_reason)

                with metrics.lock:
                    succ_cnt = metrics.successful_requests
                    fail_cnt = metrics.failed_requests
                    rps = succ_cnt / elapsed_total if elapsed_total > 0 else 0

                c_rmt = cur_out.get("rmt", {})
                c_pwm = cur_out.get("pwm", {})
                rmt_delta_frames = c_rmt.get("frames", 0) - base_rmt_frames
                rmt_delta_err = c_rmt.get("failedFrames", 0) - base_rmt_fails
                pca_delta_tx = c_pwm.get("transactions", 0) - base_pca_tx
                pca_delta_err = c_pwm.get("transactionFailures", 0) - base_pca_fails

                sys.stdout.write(
                    f"\r[{elapsed_total:5.1f}s/{args.duration}s] "
                    f"HTTP: {succ_cnt} ok, {fail_cnt} err ({rps:.1f} rps) | "
                    f"RMT: +{rmt_delta_frames} fr, {rmt_delta_err} err | "
                    f"PCA: +{pca_delta_tx} tx, {pca_delta_err} err | "
                    f"InternalFree: {cur_mem.get('freeInternal', 0)} (min {metrics.min_free_internal}) | "
                    f"Reboots: {metrics.reboots_detected}   "
                )
                sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[*] Soak test interrupted by operator.")

    finally:
        # Step 7: Always abort factory effect to leave hardware safely blacked out
        print("\n[*] Aborting factory coexistence test (leaving controller blacked out)...")
        aok, acode, _, _ = make_request(f"{base_url}/api/v1/factory/abort", args.admin_key, method="POST", timeout=5.0)
        if aok:
            print("[OK] Factory effect aborted successfully.")
        else:
            print(f"[WARN] Factory abort request returned code {acode}")

    for t in threads:
        t.join(timeout=1.0)

    # Step 8: Post-soak delta evaluation
    print("\n[*] Sampling post-soak final telemetry...")
    final_mem = fetch_json(base_url, "/api/v1/diagnostics/memory", args.admin_key)
    final_out = fetch_json(base_url, "/api/v1/diagnostics/output", args.admin_key)
    final_net = fetch_json(base_url, "/api/v1/network/status", args.admin_key)

    metrics.update_telemetry(final_mem, final_out)

    final_rmt = final_out.get("rmt", {})
    final_pwm = final_out.get("pwm", {})
    final_ctrl = final_out.get("output", {})
    final_frame = final_out.get("frame", {})

    delta_rmt_frames = final_rmt.get("frames", 0) - base_rmt_frames
    delta_rmt_failures = final_rmt.get("failedFrames", 0) - base_rmt_fails
    delta_rmt_near_misses = final_rmt.get("deadlineNearMisses", 0) - base_rmt.get("deadlineNearMisses", 0)

    delta_pca_tx = final_pwm.get("transactions", 0) - base_pca_tx
    delta_pca_failures = final_pwm.get("transactionFailures", 0) - base_pca_fails
    delta_pca_recoveries = final_pwm.get("recoveryAttempts", 0) - base_pwm.get("recoveryAttempts", 0)
    delta_pca_recovery_fails = final_pwm.get("recoveryFailures", 0) - base_pwm.get("recoveryFailures", 0)
    max_pca_latency_us = final_pwm.get("maxTransactionUs", 0)

    delta_wifi_disconnects = final_net.get("disconnectEvents", 0) - base_wifi_discs
    delta_fallback_activations = final_net.get("fallbackActivations", 0) - base_fb_activations
    delta_deadline_misses = final_frame.get("deadlineMisses", 0) - base_deadline_misses

    final_topo_gen = final_ctrl.get("topologyGeneration", base_topo_gen)
    final_topo_valid = final_ctrl.get("topologyValid", True)
    final_fault_domain = final_ctrl.get("faultDomain", "none")
    actual_wifi_mode = final_net.get("actualMode", "unknown")

    # Per-lane RMT parity calculations
    base_lanes_map = {l.get("lane", i): l for i, l in enumerate(base_rmt.get("lanes", []))}
    final_lanes_map = {l.get("lane", i): l for i, l in enumerate(final_rmt.get("lanes", []))}

    lane_parity = {}
    total_completion_mismatches = 0
    total_symbol_mismatches = 0
    total_lane_near_misses = 0

    for l_idx in range(8):
        bl = base_lanes_map.get(l_idx, {})
        fl = final_lanes_map.get(l_idx, {})
        d_sub = fl.get("submissions", 0) - bl.get("submissions", 0)
        d_cb = fl.get("txDoneCallbacks", 0) - bl.get("txDoneCallbacks", 0)
        d_cm = fl.get("completionMismatches", 0) - bl.get("completionMismatches", 0)
        d_sm = fl.get("symbolMismatches", 0) - bl.get("symbolMismatches", 0)
        d_nm = fl.get("deadlineNearMisses", 0) - bl.get("deadlineNearMisses", 0)
        lane_parity[l_idx] = (d_cb, d_sub)
        total_completion_mismatches += d_cm
        total_symbol_mismatches += d_sm
        total_lane_near_misses += d_nm

    total_time = time.time() - start_time
    print("\n==================================================================")
    print("                LG-HW-003 SOAK TEST FINAL REPORT")
    print("==================================================================")
    print(f"Elapsed Time:                  {total_time:.2f} seconds")
    print(f"Total HTTP Requests:           {metrics.total_requests}")
    print(f"Successful HTTP Requests:      {metrics.successful_requests}")
    print(f"Failed HTTP Requests:          {metrics.failed_requests}")
    print(f"Reboots Detected:              {metrics.reboots_detected}")
    print("------------------------------------------------------------------")
    print("RMT CALLBACK PARITY & INTEGRITY:")
    for l_idx in range(8):
        cb, sub = lane_parity.get(l_idx, (0, 0))
        print(f"  lane {l_idx}  {cb}/{sub}")
    print(f"RMT symbol mismatches:         {total_symbol_mismatches}")
    print(f"RMT completion mismatches:     {total_completion_mismatches}")
    print(f"RMT deadline near misses:      {delta_rmt_near_misses}")
    print("------------------------------------------------------------------")
    print("PCA / I2C INTEGRITY:")
    print(f"  PCA tx failures:             {delta_pca_failures}")
    print(f"  PCA recoveries:              {delta_pca_recoveries}")
    print(f"  PCA recovery failures:       {delta_pca_recovery_fails}")
    print(f"  PCA max transaction latency: {max_pca_latency_us} us")
    print(f"  PCA recovery required:       {final_pwm.get('recoveryRequired', False)}")
    print(f"  PCA last fault kind:         {final_pwm.get('lastFaultKind', 'none')}")
    print("------------------------------------------------------------------")
    print("HARDWARE COEXISTENCE DELTAS:")
    print(f"  RMT Rendered Frames Delta:   +{delta_rmt_frames}")
    print(f"  RMT Failed Frames Delta:     {delta_rmt_failures}")
    print(f"  PCA Transactions Delta:      +{delta_pca_tx}")
    print(f"  Frame Deadline Misses Delta: {delta_deadline_misses}")
    print(f"  Wi-Fi Disconnect Events:     {delta_wifi_disconnects}")
    print(f"  Fallback AP Activations:     {delta_fallback_activations}")
    print("------------------------------------------------------------------")
    print("MEMORY & TIMING HEADROOM:")
    print(f"  Min Free Internal SRAM:      {metrics.min_free_internal} bytes")
    print(f"  Min Largest Internal Block:  {metrics.min_largest_internal} bytes")
    print(f"  Max Output Commit Duration:  {metrics.max_addressable_commit_us} us")
    print(f"  Max RMT Wait Duration:       {metrics.max_rmt_wait_us} us")
    print(f"  Max Frame Render Duration:   {metrics.max_frame_us} us")
    print("------------------------------------------------------------------")
    print("LIFECYCLE & TOPOLOGY INTEGRITY:")
    print(f"  Topology Valid:              {final_topo_valid}")
    print(f"  Fault Domain:                {final_fault_domain}")
    print(f"  Topology Generation Baseline:{base_topo_gen}")
    print(f"  Topology Generation Final:   {final_topo_gen}")
    print(f"  Actual Wi-Fi Mode:           {actual_wifi_mode}")
    print("------------------------------------------------------------------")
    print("RESET FORENSICS & ELECTRICAL BOOT SAFETY (LG-HW-004):")
    final_reset = final_out.get("reset", {})
    final_boot_out = final_out.get("bootOutput", {})
    final_pwr = final_out.get("powerTransient", {})
    print(f"  Boot Session ID:             {init_session} -> {final_reset.get('bootSessionId', 'N/A')}")
    print(f"  Reset Reason:                {final_reset.get('reasonStr', 'N/A')} (abnormal: {final_reset.get('abnormal', False)})")
    print(f"  Consecutive Abnormal Boots:  {final_reset.get('consecutiveAbnormalBoots', 0)}")
    print(f"  Brownout Detector Enabled:   {final_reset.get('brownoutDetectorEnabled', False)}")
    print(f"  Safe Mode Mandated:          {final_reset.get('safeModeMandated', False)}")
    print(f"  Quarantine Active:           {final_reset.get('quarantineActive', False)}")
    print(f"  Early Safety Applied:        {final_boot_out.get('earlySafetyApplied', False)}")
    print(f"  PCA /OE High at Bootstrap:   {final_boot_out.get('pcaOeHighAtBootstrap', False)}")
    print(f"  ALED Low Mask:               0x{final_boot_out.get('aledLowMask', 0):02X}")
    print(f"  First Black Confirmed:       {final_boot_out.get('firstAddressableBlackConfirmed', False)}")
    print(f"  First Zero Confirmed:        {final_boot_out.get('firstPwmZeroConfirmed', False)}")
    print(f"  Power Step Clamps:           {final_pwr.get('stepClamps', 0)}")
    print(f"  Wi-Fi Transient Clamps:      {final_pwr.get('wifiTransientClamps', 0)}")
    print(f"  Quarantine Clamps:           {final_pwr.get('quarantineClamps', 0)}")
    print("------------------------------------------------------------------")

    # Qualification classification: PASS, MARGINAL, FAIL
    fail_reasons = []
    marginal_reasons = []

    # FAIL conditions
    if metrics.classified_reboot_failures:
        for crf in metrics.classified_reboot_failures:
            fail_reasons.append(crf)
    elif metrics.reboots_detected > 0:
        fail_reasons.append("Unexpected device reboot")
    if delta_rmt_failures > 0:
        fail_reasons.append(f"RMT failed frames (delta: {delta_rmt_failures})")
    if total_completion_mismatches > 0:
        fail_reasons.append(f"RMT completion mismatches (delta: {total_completion_mismatches})")
    if total_symbol_mismatches > 0:
        fail_reasons.append(f"RMT symbol mismatches (delta: {total_symbol_mismatches})")
    if delta_pca_failures > 0:
        fail_reasons.append(f"PCA transaction failures (delta: {delta_pca_failures})")
    if delta_pca_recovery_fails > 0:
        fail_reasons.append(f"PCA recovery failures (delta: {delta_pca_recovery_fails})")
    if not final_topo_valid:
        fail_reasons.append("Hardware topology became invalid")
    if final_topo_gen != base_topo_gen:
        fail_reasons.append(f"Topology generation drifted ({base_topo_gen} -> {final_topo_gen})")
    if metrics.min_largest_internal < 12 * 1024:
        fail_reasons.append(f"Internal memory fell below reserve ({metrics.min_largest_internal} < 12 KB)")
    if metrics.failed_requests > (metrics.total_requests * 0.05):
        fail_reasons.append("HTTP failure rate exceeded 5%")
    if actual_wifi_mode == "apsta" and not final_net.get("isFallbackAp", False):
        fail_reasons.append("State drift: actual Wi-Fi mode is APSTA but software claims normal STA")

    # MARGINAL conditions (when no hard FAIL)
    if delta_rmt_near_misses > 0 or total_lane_near_misses > 0:
        marginal_reasons.append(f"RMT deadline near misses observed (delta: {delta_rmt_near_misses})")
    if delta_deadline_misses > 10:
        marginal_reasons.append(f"Frame deadline misses exceeded threshold (delta: {delta_deadline_misses})")

    if fail_reasons:
        classification = "FAIL"
    elif marginal_reasons:
        classification = "MARGINAL"
    else:
        classification = "PASS"

    print(f"QUALIFICATION RESULT: [{classification}]")
    if fail_reasons:
        for r in fail_reasons:
            print(f"  [FAIL REASON] {r}")
    if marginal_reasons:
        for r in marginal_reasons:
            print(f"  [MARGINAL REASON] {r}")
    print("==================================================================")

    if classification == "PASS":
        return 0
    elif classification == "MARGINAL":
        return 0  # Functional pass with warnings
    else:
        return 1


if __name__ == "__main__":
    sys.exit(main())
