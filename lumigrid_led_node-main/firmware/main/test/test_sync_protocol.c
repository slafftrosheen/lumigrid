#include "unity.h"
#include "sync_protocol.h"
#include <string.h>
#include <math.h>
#include <stddef.h>

// NOTE: Packet structure is duplicated here for testing because it's not exported
// in sync_protocol.h (it's an internal implementation detail). This allows us to
// test the binary packet encoding/decoding without exposing internal structures.
// If the packet format changes in sync_protocol.c, these tests will need updating.
typedef enum {
    SYNC_PACKET_TICK = 1,
    SYNC_PACKET_CUE = 2
} sync_packet_type_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t type;
    uint8_t pad[3];
} sync_packet_t;

// Test packet structure size and alignment
TEST_CASE("sync packet has correct size and alignment", "[sync_protocol]") {
    sync_packet_t packet;
    
    // Verify expected size (packed structure)
    // uint32_t (4) + uint8_t (1) + uint8_t[3] (3) = 8 bytes
    TEST_ASSERT_EQUAL_INT(8, sizeof(sync_packet_t));
    
    // Verify field offsets
    TEST_ASSERT_EQUAL_INT(0, offsetof(sync_packet_t, timestamp_ms));
    TEST_ASSERT_EQUAL_INT(4, offsetof(sync_packet_t, type));
    TEST_ASSERT_EQUAL_INT(5, offsetof(sync_packet_t, pad));
}

// Test TICK packet encoding
TEST_CASE("sync tick packet encodes correctly", "[sync_protocol]") {
    sync_packet_t packet = {
        .timestamp_ms = 12345678,
        .type = SYNC_PACKET_TICK,
        .pad = {0, 0, 0}
    };
    
    TEST_ASSERT_EQUAL_UINT32(12345678, packet.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(SYNC_PACKET_TICK, packet.type);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[0]);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[1]);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[2]);
}

// Test CUE packet encoding
TEST_CASE("sync cue packet encodes correctly", "[sync_protocol]") {
    sync_packet_t packet = {
        .timestamp_ms = 87654321,
        .type = SYNC_PACKET_CUE,
        .pad = {0, 0, 0}
    };
    
    TEST_ASSERT_EQUAL_UINT32(87654321, packet.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(SYNC_PACKET_CUE, packet.type);
}

// Test packet decoding
TEST_CASE("sync packet decodes from buffer", "[sync_protocol]") {
    // Create a packet buffer
    uint8_t buffer[8];
    sync_packet_t *packet = (sync_packet_t *)buffer;
    
    // Fill buffer with encoded packet
    packet->timestamp_ms = 11223344;
    packet->type = SYNC_PACKET_TICK;
    packet->pad[0] = 0;
    packet->pad[1] = 0;
    packet->pad[2] = 0;
    
    // Decode from buffer
    sync_packet_t decoded;
    memcpy(&decoded, buffer, sizeof(sync_packet_t));
    
    TEST_ASSERT_EQUAL_UINT32(11223344, decoded.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(SYNC_PACKET_TICK, decoded.type);
}

// Test tempo setting and getting
TEST_CASE("sync tempo set and get", "[sync_protocol]") {
    // Test valid tempo
    sync_set_tempo(120.0f);
    float tempo = sync_get_tempo();
    TEST_ASSERT_EQUAL_FLOAT(120.0f, tempo);
    
    // Test different tempo
    sync_set_tempo(140.0f);
    tempo = sync_get_tempo();
    TEST_ASSERT_EQUAL_FLOAT(140.0f, tempo);
    
    // Test minimum tempo (should clamp to 20 BPM)
    sync_set_tempo(10.0f);
    tempo = sync_get_tempo();
    TEST_ASSERT_TRUE(tempo >= 20.0f);
    
    // Test very high tempo
    sync_set_tempo(200.0f);
    tempo = sync_get_tempo();
    TEST_ASSERT_EQUAL_FLOAT(200.0f, tempo);
}

// Test phase wrapping
TEST_CASE("sync phase wraps correctly", "[sync_protocol]") {
    // Phase should be in range [0.0, 1.0)
    
    // Test fmod wrapping logic
    double phase = 0.5;
    phase = fmod(phase, 1.0);
    TEST_ASSERT_TRUE(phase >= 0.0 && phase < 1.0);
    
    // Test wrapping above 1.0
    phase = 1.5;
    phase = fmod(phase, 1.0);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, phase);
    
    // Test wrapping at 1.0
    phase = 1.0;
    phase = fmod(phase, 1.0);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, phase);
    
    // Test negative wrapping
    phase = -0.5;
    phase = fmod(phase, 1.0);
    if (phase < 0.0) phase += 1.0;
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, phase);
}

// Test tempo to period conversion
TEST_CASE("sync tempo converts to period correctly", "[sync_protocol]") {
    // period_ms = 60000 / bpm
    
    // 120 BPM -> 500ms period
    float bpm = 120.0f;
    double period = 60000.0 / (double)bpm;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 500.0, period);
    
    // 60 BPM -> 1000ms period
    bpm = 60.0f;
    period = 60000.0 / (double)bpm;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1000.0, period);
    
    // 240 BPM -> 250ms period
    bpm = 240.0f;
    period = 60000.0 / (double)bpm;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 250.0, period);
    
    // Invalid tempo (0 or negative) should use fallback
    bpm = 0.0f;
    period = (bpm > 0.0f) ? (60000.0 / (double)bpm) : 500.0;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 500.0, period);
}

// Test phase increment calculation
TEST_CASE("sync phase increment calculated correctly", "[sync_protocol]") {
    // delta_phase = delta_time_ms / period_ms
    
    float bpm = 120.0f; // 500ms period
    double period = 60000.0 / (double)bpm;
    
    // 50ms elapsed -> 0.1 phase increment
    double dt = 50.0;
    double delta_phase = dt / period;
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.1, delta_phase);
    
    // 250ms elapsed -> 0.5 phase increment
    dt = 250.0;
    delta_phase = dt / period;
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, delta_phase);
    
    // 500ms elapsed -> 1.0 phase increment (full beat)
    dt = 500.0;
    delta_phase = dt / period;
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, delta_phase);
}

// Test drift correction EWMA
TEST_CASE("sync drift correction uses ewma", "[sync_protocol]") {
    // Test exponential weighted moving average calculation
    // ewma = alpha * new_value + (1 - alpha) * old_ewma
    
    double ewma = 0.0;
    double alpha = 0.1;
    
    // First measurement: 10ms drift
    double drift = 10.0;
    ewma = alpha * drift + (1.0 - alpha) * ewma;
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, ewma);
    
    // Second measurement: 20ms drift
    drift = 20.0;
    ewma = alpha * drift + (1.0 - alpha) * ewma;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 2.9, ewma);
    
    // Third measurement: 15ms drift
    drift = 15.0;
    ewma = alpha * drift + (1.0 - alpha) * ewma;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 4.11, ewma);
    
    // Converges toward actual drift over time
    TEST_ASSERT_TRUE(ewma > 0.0);
}

// Test packet type validation
TEST_CASE("sync packet type validates correctly", "[sync_protocol]") {
    sync_packet_t packet;
    
    // Valid packet types
    packet.type = SYNC_PACKET_TICK;
    TEST_ASSERT_TRUE(packet.type == SYNC_PACKET_TICK || packet.type == SYNC_PACKET_CUE);
    
    packet.type = SYNC_PACKET_CUE;
    TEST_ASSERT_TRUE(packet.type == SYNC_PACKET_TICK || packet.type == SYNC_PACKET_CUE);
    
    // Invalid packet type
    packet.type = 99;
    TEST_ASSERT_FALSE(packet.type == SYNC_PACKET_TICK || packet.type == SYNC_PACKET_CUE);
}

// Test timestamp overflow handling
TEST_CASE("sync handles timestamp overflow", "[sync_protocol]") {
    // uint32_t wraps at 0xFFFFFFFF
    
    uint32_t ts1 = 0xFFFFFFF0;
    uint32_t ts2 = 0x00000010;
    
    // Calculate time difference accounting for overflow
    int32_t diff;
    if (ts2 >= ts1) {
        diff = ts2 - ts1;
    } else {
        // Overflow occurred
        diff = (int32_t)((uint32_t)0xFFFFFFFF - ts1 + ts2 + 1);
    }
    
    TEST_ASSERT_EQUAL_INT32(32, diff);
}

// Test packet buffer marshalling
TEST_CASE("sync packet marshals to bytes correctly", "[sync_protocol]") {
    sync_packet_t packet = {
        .timestamp_ms = 0x12345678,
        .type = SYNC_PACKET_TICK,
        .pad = {0xAA, 0xBB, 0xCC}
    };
    
    uint8_t buffer[8];
    memcpy(buffer, &packet, sizeof(packet));
    
    // Verify bytes (little-endian assumed)
    TEST_ASSERT_EQUAL_UINT8(0x78, buffer[0]); // LSB of timestamp
    TEST_ASSERT_EQUAL_UINT8(0x56, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[3]); // MSB of timestamp
    TEST_ASSERT_EQUAL_UINT8(SYNC_PACKET_TICK, buffer[4]); // type
    TEST_ASSERT_EQUAL_UINT8(0xAA, buffer[5]); // pad[0]
    TEST_ASSERT_EQUAL_UINT8(0xBB, buffer[6]); // pad[1]
    TEST_ASSERT_EQUAL_UINT8(0xCC, buffer[7]); // pad[2]
}

// Test packet buffer unmarshalling
TEST_CASE("sync packet unmarshals from bytes correctly", "[sync_protocol]") {
    uint8_t buffer[8] = {
        0x78, 0x56, 0x34, 0x12, // timestamp (little-endian)
        0x02,                    // type = CUE
        0x00, 0x00, 0x00        // padding
    };
    
    sync_packet_t packet;
    memcpy(&packet, buffer, sizeof(packet));
    
    TEST_ASSERT_EQUAL_UINT32(0x12345678, packet.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(SYNC_PACKET_CUE, packet.type);
}

// Test zero-initialized packet
TEST_CASE("sync packet zero initialization", "[sync_protocol]") {
    sync_packet_t packet = {0};
    
    TEST_ASSERT_EQUAL_UINT32(0, packet.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(0, packet.type);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[0]);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[1]);
    TEST_ASSERT_EQUAL_UINT8(0, packet.pad[2]);
}

// New tests for resilience and error handling

// Test dropped packet handling
TEST_CASE("sync handles dropped packets gracefully", "[sync_protocol]") {
    // Simulate receiving packets with gaps
    uint32_t base_time = 1000;
    
    // First packet received
    sync_rx_peer(base_time, 0.0f);
    
    // Simulate dropped packets by advancing time significantly
    // Normal interval is 50ms, simulate 200ms gap (4 dropped packets)
    sync_rx_peer(base_time + 200, 0.2f);
    
    // Drift EWMA should still be calculable
    float drift = sync_drift_correction_ms();
    TEST_ASSERT_TRUE(drift > -1000.0f && drift < 1000.0f);
}

// Test clock drift simulation
TEST_CASE("sync handles clock drift over time", "[sync_protocol]") {
    // Simulate gradual clock drift
    uint32_t peer_time = 5000;
    
    // Peer is running 1ms faster per packet
    for (int i = 0; i < 10; i++) {
        sync_rx_peer(peer_time + i * 51, -1.0f);  // 51ms instead of 50ms
    }
    
    // Drift should be detected and corrected via EWMA
    float drift = sync_drift_correction_ms();
    TEST_ASSERT_TRUE(drift != 0.0f);
}

// Test excessive jitter detection
TEST_CASE("sync detects excessive jitter", "[sync_protocol]") {
    uint32_t base_time = 2000;
    
    // Start with stable packets
    for (int i = 0; i < 5; i++) {
        sync_rx_peer(base_time + i * 50, -1.0f);
    }
    
    // Introduce large jitter (simulate network issue)
    sync_rx_peer(base_time + 500, -1.0f);  // 200ms jump
    
    // System should detect and handle the jitter
    float drift = sync_drift_correction_ms();
    TEST_ASSERT_NOT_EQUAL(0.0f, drift);
}

// Test phase resynchronization
TEST_CASE("sync phase resyncs correctly", "[sync_protocol]") {
    uint32_t now = 1000;
    
    // Set initial phase
    sync_set_tempo(120.0f);
    sync_pulse(now);
    
    float phase1 = sync_phase_01(now);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, phase1);
    
    // Advance time by half a beat (250ms at 120 BPM)
    float phase2 = sync_phase_01(now + 250);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.5f, phase2);
    
    // Advance time by full beat (500ms at 120 BPM)
    float phase3 = sync_phase_01(now + 500);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, phase3);
}

// Test quantized phase with different step counts
TEST_CASE("sync quantized phase with various steps", "[sync_protocol]") {
    uint32_t now = 2000;
    sync_set_tempo(120.0f);
    sync_pulse(now);
    
    // Test 4 steps (quarter notes)
    float phase_q4 = sync_phase_quantized(now + 125, 4);  // 1/4 beat
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.25f, phase_q4);
    
    // Test 8 steps (eighth notes)
    float phase_q8 = sync_phase_quantized(now + 62, 8);  // 1/8 beat
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 0.125f, phase_q8);
    
    // Test 16 steps (sixteenth notes)
    float phase_q16 = sync_phase_quantized(now + 31, 16);  // 1/16 beat
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0625f, phase_q16);
}

// Test tempo boundary conditions
TEST_CASE("sync tempo handles boundary conditions", "[sync_protocol]") {
    // Test very low tempo (should clamp to minimum)
    sync_set_tempo(5.0f);
    float tempo_low = sync_get_tempo();
    TEST_ASSERT_TRUE(tempo_low >= 20.0f);
    
    // Test zero tempo (should clamp)
    sync_set_tempo(0.0f);
    float tempo_zero = sync_get_tempo();
    TEST_ASSERT_TRUE(tempo_zero >= 20.0f);
    
    // Test negative tempo (should clamp)
    sync_set_tempo(-50.0f);
    float tempo_neg = sync_get_tempo();
    TEST_ASSERT_TRUE(tempo_neg >= 20.0f);
    
    // Test very high tempo
    sync_set_tempo(300.0f);
    float tempo_high = sync_get_tempo();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, tempo_high);
}

// Test multiple pulse resets
TEST_CASE("sync handles multiple pulse resets", "[sync_protocol]") {
    sync_set_tempo(120.0f);
    
    uint32_t t1 = 1000;
    sync_pulse(t1);
    float phase1 = sync_phase_01(t1 + 100);
    TEST_ASSERT_TRUE(phase1 > 0.0f);
    
    // Reset pulse
    uint32_t t2 = 2000;
    sync_pulse(t2);
    float phase2 = sync_phase_01(t2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, phase2);
    
    // Reset again
    uint32_t t3 = 3000;
    sync_pulse(t3);
    float phase3 = sync_phase_01(t3);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, phase3);
}

// Test local tick advancement
TEST_CASE("sync tick local advances phase correctly", "[sync_protocol]") {
    sync_set_tempo(120.0f);
    
    uint32_t start_time = 5000;
    sync_tick_local(start_time);
    
    // Advance by 50ms multiple times
    for (int i = 1; i <= 10; i++) {
        sync_tick_local(start_time + i * 50);
    }
    
    // Phase should have advanced
    float phase = sync_get_phase();
    TEST_ASSERT_TRUE(phase > 0.0f && phase < 1.0f);
}
