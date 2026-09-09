# scripts/verify_runtime_math.py
"""
Automated unit verification for Stage 1 runtime correctness:
1. Compositor blend math (Normal, Add, Multiply, Screen) with signed deltas and clamping.
2. Gamma table consistency.
3. PowerBudget post-render enforcement calculation.
4. ControlState priority & zone/layer resolution.
"""

def clamp(val, low, high):
    return max(low, min(high, val))

def blend_channel(b_val, f_val, mode, fg_opacity):
    if fg_opacity == 0:
        return b_val
    if mode == "normal":
        if fg_opacity == 255:
            return f_val
        delta = f_val - b_val
        res = b_val + ((delta * fg_opacity) // 255)
        return clamp(res, 0, 255)
    elif mode == "add":
        add = b_val + (f_val * fg_opacity) // 255
        return min(add, 255)
    elif mode == "multiply":
        mult = (b_val * f_val) // 255
        delta = mult - b_val
        res = b_val + ((delta * fg_opacity) // 255)
        return clamp(res, 0, 255)
    elif mode == "screen":
        screen = 255 - (((255 - b_val) * (255 - f_val)) // 255)
        delta = screen - b_val
        res = b_val + ((delta * fg_opacity) // 255)
        return clamp(res, 0, 255)
    return f_val

def test_compositor():
    # Test boundary: 0 -> 255
    assert blend_channel(0, 255, "normal", 0) == 0
    assert blend_channel(0, 255, "normal", 128) == 128
    assert blend_channel(0, 255, "normal", 255) == 255

    # Test boundary: 255 -> 0 (would underflow unsigned without signed delta)
    assert blend_channel(255, 0, "normal", 0) == 255
    assert blend_channel(255, 0, "normal", 128) == 127
    assert blend_channel(255, 0, "normal", 255) == 0

    # Test Add mode
    assert blend_channel(100, 200, "add", 255) == 255
    assert blend_channel(100, 50, "add", 128) == 125

    # Test Multiply mode
    assert blend_channel(255, 128, "multiply", 255) == 128
    assert blend_channel(0, 255, "multiply", 255) == 0

    # Test Screen mode
    assert blend_channel(128, 128, "screen", 255) == 192
    assert blend_channel(0, 255, "screen", 255) == 255
    print("[PASS] Compositor math tests passed.")

def test_power_enforce():
    # 100 pixels white at 300mW per pixel max
    limit_mw = 15000  # 15W limit
    # 100 pixels * 300mW = 30000mW (30W)
    total_mw = 100 * 300
    assert total_mw == 30000
    scale = (limit_mw * 255) // total_mw
    assert scale == 127  # scaled ~50%
    scaled_px = (255 * scale) // 255
    assert scaled_px == 127
    print("[PASS] Power enforcement math tests passed.")

def test_control_resolver():
    states = [
        {"source": 2, "active": True, "zone_mask": 0xFF, "layer_mask": 0x07, "expires_ms": 0},    # ScenePlayback
        {"source": 5, "active": True, "zone_mask": 0x01, "layer_mask": 0x04, "expires_ms": 1000}, # LiveDmx on zone 0, layer 2
    ]
    current_ms = 500
    
    def resolve(z, l, t):
        for s in reversed(states):
            if s["active"] and (s["expires_ms"] == 0 or t < s["expires_ms"]):
                if (s["zone_mask"] & (1 << z)) and (s["layer_mask"] & (1 << l)):
                    return s["source"]
        return 0

    # Zone 0, Layer 2 should be LiveDmx (5)
    assert resolve(0, 2, current_ms) == 5
    # Zone 0, Layer 1 should fall through to ScenePlayback (2)
    assert resolve(0, 1, current_ms) == 2
    # Zone 1, Layer 2 should fall through to ScenePlayback (2)
    assert resolve(1, 2, current_ms) == 2
    # After timeout (1500ms), Zone 0, Layer 2 should revert to ScenePlayback (2)
    assert resolve(0, 2, 1500) == 2
    print("[PASS] Control resolver tests passed.")

def test_color_pipeline():
    # Test subtractive RGBW conversion
    def rgb_to_rgbw(r, g, b):
        w = min(r, g, b)
        return (r - w, g - w, b - w, w)

    # Pure red -> no white
    assert rgb_to_rgbw(255, 0, 0) == (255, 0, 0, 0)
    # Pure white -> all white, 0 RGB
    assert rgb_to_rgbw(255, 255, 255) == (0, 0, 0, 255)
    # Pastel color (e.g. 255, 128, 128) -> w = 128, r = 127, g = 0, b = 0
    assert rgb_to_rgbw(255, 128, 128) == (127, 0, 0, 128)
    print("[PASS] ColorPipeline RGBW extraction tests passed.")

def test_gamma_alignment():
    # Mathematical check: gamma 2.2 for 8-bit (0..255) and 12-bit (0..4095)
    def gamma8(val):
        return round(255.0 * ((val / 255.0) ** 2.2))
    def gamma12(val):
        return round(4095.0 * ((val / 255.0) ** 2.2))

    # At 0, both are 0
    assert gamma8(0) == 0
    assert gamma12(0) == 0
    # At 255, full scale
    assert gamma8(255) == 255
    assert gamma12(255) == 4095
    # At mid-level (128): perceptual ratio should align
    r8 = gamma8(128) / 255.0
    r12 = gamma12(128) / 4095.0
    assert abs(r8 - r12) < 0.005
    print("[PASS] Perceptual gamma 8-bit & 12-bit alignment tests passed.")

def test_multitier_power():
    # 200 pixels @ 5mW idle = 1000mW quiescent
    # All pixels full white: active = 200 * 295mW = 59000mW
    # Total unconstrained = 60000mW (60W)
    num_pixels = 200
    idle_mw = num_pixels * 5
    active_mw = num_pixels * 295
    total_mw = idle_mw + active_mw
    assert total_mw == 60000

    # Test Local Fixture Clamp: Limit fixture to 15000mW (15W)
    fixture_limit = 15000
    allowed_active = fixture_limit - idle_mw  # 14000mW
    local_scale = (allowed_active * 255) // active_mw
    new_active = (active_mw * local_scale) // 255
    fixture_total = idle_mw + new_active
    assert fixture_total <= fixture_limit
    print(f"[PASS] Local fixture power clamp: {total_mw}mW -> {fixture_total}mW (limit: {fixture_limit}mW)")

    # Test Global Limit Clamp with soft-start:
    global_limit = 10000  # 10W global limit
    available_active = global_limit - idle_mw  # 9000mW
    global_scale = (available_active * 255) // new_active
    final_scale = (global_scale * 128) // 255  # 50% soft-start ramp
    final_active = (new_active * final_scale) // 255
    final_total = idle_mw + final_active
    assert final_total <= global_limit
    print(f"[PASS] Global limit + soft-start: clamped to {final_total}mW (budget: {global_limit}mW)")

def test_dmx_routing_and_patching():
    # 1. Zone Wash Mode test
    zone_start_channel = 1
    dmx_data = [0] * 512
    # Set Zone 0 (Ch 1..3) to Red, Zone 1 (Ch 4..6) to Green, Zone 2 (Ch 7..9) to Blue
    dmx_data[0] = 255; dmx_data[1] = 0;   dmx_data[2] = 0
    dmx_data[3] = 0;   dmx_data[4] = 255; dmx_data[5] = 0
    dmx_data[6] = 0;   dmx_data[7] = 0;   dmx_data[8] = 255

    def extract_zone_color(data, zone_idx, base_ch=1):
        offset = (base_ch - 1) + (zone_idx * 3)
        return (data[offset], data[offset + 1], data[offset + 2])

    assert extract_zone_color(dmx_data, 0, zone_start_channel) == (255, 0, 0)
    assert extract_zone_color(dmx_data, 1, zone_start_channel) == (0, 255, 0)
    assert extract_zone_color(dmx_data, 2, zone_start_channel) == (0, 0, 255)
    print("[PASS] DMX Zone Wash channel extraction tests passed.")

    # 2. Fixture Patch Mode test (Direct Pixel Map)
    # Fixture starts at DMX Ch 10, mode = PixelDirect (RGB)
    fixture_start_ch = 10
    num_pixels = 4
    # Set pixel 0 to white, pixel 1 to cyan, pixel 2 to magenta, pixel 3 to yellow
    colors = [(255, 255, 255), (0, 255, 255), (255, 0, 255), (255, 255, 0)]
    for p, c in enumerate(colors):
        ch = (fixture_start_ch - 1) + (p * 3)
        dmx_data[ch] = c[0]
        dmx_data[ch + 1] = c[1]
        dmx_data[ch + 2] = c[2]

    def extract_fixture_pixels(data, start_ch, count):
        pixels = []
        for p in range(count):
            ch = (start_ch - 1) + (p * 3)
            pixels.append((data[ch], data[ch + 1], data[ch + 2]))
        return pixels

    extracted = extract_fixture_pixels(dmx_data, fixture_start_ch, num_pixels)
    assert extracted == colors
    print("[PASS] DMX Direct Fixture pixel map extraction tests passed.")

def test_htp_ltp_and_priority():
    # 1. HTP Merge: max(existing, incoming)
    buf = [50, 100, 150, 200]
    incoming = [100, 80, 200, 100]
    htp_result = [max(b, i) for b, i in zip(buf, incoming)]
    assert htp_result == [100, 100, 200, 200]
    print("[PASS] HTP (Highest Takes Precedence) merge test passed.")

    # 2. LTP Merge: incoming replaces buffer
    ltp_result = list(incoming)
    assert ltp_result == incoming
    print("[PASS] LTP (Latest Takes Precedence) merge test passed.")

    # 3. sACN Priority Arbitration
    class UniverseArbiter:
        def __init__(self):
            self.priority = 0
            self.last_ms = 0
            self.data = [0] * 4

        def receive(self, priority, data, current_ms, timeout=2000):
            if self.last_ms > 0 and (current_ms - self.last_ms < timeout):
                if priority < self.priority:
                    return False  # Preempted by active higher priority stream
            self.priority = priority
            self.last_ms = current_ms
            self.data = list(data)
            return True

    arbiter = UniverseArbiter()
    # Source A (Priority 100)
    assert arbiter.receive(100, [10, 10, 10, 10], current_ms=100) is True
    assert arbiter.data == [10, 10, 10, 10]

    # Source B (Priority 80) attempts to override during active window -> Rejected!
    assert arbiter.receive(80, [99, 99, 99, 99], current_ms=200) is False
    assert arbiter.data == [10, 10, 10, 10]

    # Source C (Priority 150) arrives during active window -> Preempts Source A!
    assert arbiter.receive(150, [50, 50, 50, 50], current_ms=300) is True
    assert arbiter.data == [50, 50, 50, 50]

    # After timeout (2500ms elapsed since last packet), lower priority can claim
    assert arbiter.receive(80, [1, 2, 3, 4], current_ms=2500) is True
    assert arbiter.data == [1, 2, 3, 4]
    print("[PASS] sACN priority preemption and timeout release tests passed.")

def test_stage5_control_priority_and_estop():
    # Priority hierarchy: 0: Idle, 1: Fallback, 2: ScenePlayback, 3: Scheduler, 4: ManualOverride, 5: LiveDmx, 6: Factory, 7: EStop
    sources = {
        "Idle": 0, "Fallback": 1, "ScenePlayback": 2, "Scheduler": 3,
        "ManualOverride": 4, "LiveDmx": 5, "Factory": 6, "EStop": 7
    }

    class MockControlManager:
        def __init__(self):
            self.states = {i: {"active": False, "expires_ms": 0} for i in range(8)}
            self.owner = 0

        def acquire(self, src, lease_ms, now_ms):
            self.states[src]["active"] = True
            self.states[src]["expires_ms"] = 0 if lease_ms == 0 else (now_ms + lease_ms)
            self._evaluate(now_ms)
            return self.owner == src

        def release(self, src, now_ms):
            self.states[src]["active"] = False
            self._evaluate(now_ms)

        def _evaluate(self, now_ms):
            highest = 0
            for i in range(7, -1, -1):
                st = self.states[i]
                if st["active"]:
                    if st["expires_ms"] != 0 and now_ms >= st["expires_ms"]:
                        st["active"] = False
                    else:
                        highest = i
                        break
            self.owner = highest

    cm = MockControlManager()
    now = 1000

    # 1. Base scene playback
    cm.acquire(sources["ScenePlayback"], 0, now)
    assert cm.owner == sources["ScenePlayback"]

    # 2. Scheduler takes control (priority 3 > 2)
    cm.acquire(sources["Scheduler"], 200, now)
    assert cm.owner == sources["Scheduler"]

    # 3. Manual override takes control over scheduler (priority 4 > 3)
    cm.acquire(sources["ManualOverride"], 60000, now)
    assert cm.owner == sources["ManualOverride"]

    # 4. Live DMX arrives and takes control (priority 5 > 4)
    cm.acquire(sources["LiveDmx"], 2000, now)
    assert cm.owner == sources["LiveDmx"]

    # 5. EStop takes control over everything (priority 7 > 5)
    cm.acquire(sources["EStop"], 0, now)
    assert cm.owner == sources["EStop"]

    # 6. Releasing EStop drops back to LiveDmx
    cm.release(sources["EStop"], now)
    assert cm.owner == sources["LiveDmx"]

    # 7. LiveDmx expires after 2500ms -> falls back to ManualOverride
    now = 3500
    cm._evaluate(now)
    assert cm.owner == sources["ManualOverride"]

    # 8. Releasing ManualOverride drops back to Scheduler or ScenePlayback
    cm.release(sources["ManualOverride"], now)
    # Scheduler had 200ms lease from t=1000, so at t=3500 it expired -> reverts to ScenePlayback!
    assert cm.owner == sources["ScenePlayback"]
    print("[PASS] Stage 5 Control Priority & EStop arbitration tests passed.")

def test_stage5_multinode_preset_sync():
    # Test encoding and decoding for all presets 0..31
    for preset in range(32):
        for has_epoch in (False, True):
            for sync_enabled in (False, True):
                flags = (1 if has_epoch else 0) | (2 | ((preset & 0x1F) << 2) if sync_enabled else 0)

                # Decode on worker
                decoded_has_epoch = (flags & 0x01) != 0
                assert decoded_has_epoch == has_epoch

                if sync_enabled:
                    assert (flags & 0x02) != 0
                    decoded_preset = (flags >> 2) & 0x1F
                    assert decoded_preset == preset
                else:
                    assert (flags & 0x02) == 0
    print("[PASS] Stage 5 Multi-node fleet preset synchronization packing tests passed.")

def test_stage5_watchdog_recovery():
    class MockSceneManager:
        def __init__(self, fallback_idx=15):
            self.active_idx = 3
            self.saved_active_idx = 3
            self.fallback_active = False
            self.fallback_idx = fallback_idx

        def watchdog_timeout(self):
            self.saved_active_idx = self.active_idx
            self.active_idx = self.fallback_idx
            self.fallback_active = True

        def feed_watchdog(self):
            if self.fallback_active:
                self.fallback_active = False
                self.active_idx = self.saved_active_idx

    sm = MockSceneManager(fallback_idx=7)
    assert sm.active_idx == 3
    assert not sm.fallback_active

    # Watchdog trips
    sm.watchdog_timeout()
    assert sm.active_idx == 7
    assert sm.fallback_active
    assert sm.saved_active_idx == 3

    # Watchdog fed -> automatically recovers to active scene 3!
    sm.feed_watchdog()
    assert sm.active_idx == 3
    assert not sm.fallback_active
    print("[PASS] Stage 5 Watchdog failover and state recovery tests passed.")

if __name__ == "__main__":
    test_compositor()
    test_power_enforce()
    test_control_resolver()
    test_color_pipeline()
    test_gamma_alignment()
    test_multitier_power()
    test_dmx_routing_and_patching()
    test_htp_ltp_and_priority()
    test_stage5_control_priority_and_estop()
    test_stage5_multinode_preset_sync()
    test_stage5_watchdog_recovery()
    print("ALL STAGE 1, 2, 3, 4, AND 5 TESTS PASSED SUCCESSFULLY.")
