// components/protocols/ArtnetDiscovery.cpp
#include "ArtnetDiscovery.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include "esp_netif.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace lg {
namespace {
constexpr char kTag[] = "artnet";
constexpr uint16_t kArtnetPort = 6454;
constexpr uint16_t kOpPoll = 0x2000;
constexpr uint16_t kOpPollReply = 0x2100;
constexpr uint16_t kOpDmx = 0x5000;

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>(p[1]) |
           (static_cast<uint16_t>(p[0]) << 8);
}

void write_le16(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>(val & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

void copy_padded(uint8_t* dst, size_t length, const std::string& src) {
    std::memset(dst, 0, length);
    std::memcpy(dst, src.data(), std::min(length - 1, src.length()));
}

} // namespace

size_t ArtnetDiscovery::serialize_poll_reply(const ArtnetNodeInfo& info, uint8_t* reply, size_t max_len) {
    if (!reply || max_len < 239) return 0;
    std::memset(reply, 0, 239);

    // --- Header (0-9) ---
    std::memcpy(reply, "Art-Net\0", 8);         // 0-7:   ID
    write_le16(reply + 8, kOpPollReply);         // 8-9:   OpCode 0x2100 LE

    // --- IP Address (10-13) ---
    reply[10] = info.ip[0];
    reply[11] = info.ip[1];
    reply[12] = info.ip[2];
    reply[13] = info.ip[3];

    // --- Port (14-15) ---
    reply[14] = 0x36;                            // 14-15: Port 0x1936 LE
    reply[15] = 0x19;

    // --- Firmware Version (16-17, big-endian) ---
    reply[16] = static_cast<uint8_t>((info.version >> 8) & 0xFF); // 16: VersInfoH
    reply[17] = static_cast<uint8_t>(info.version & 0xFF);        // 17: VersInfoL

    // --- Net/Sub/OEM (18-21) ---
    reply[18] = info.net;                       // 18: NetSwitch (bits 14..8 of 15-bit Port-Address)
    reply[19] = info.subnet;                    // 19: SubSwitch (bits 7..4 of 15-bit Port-Address)
    reply[20] = static_cast<uint8_t>((info.oem >> 8) & 0xFF); // 20: OemHi (big-endian)
    reply[21] = static_cast<uint8_t>(info.oem & 0xFF);        // 21: OemLo

    // --- Status/ESTA (22-25) ---
    reply[22] = 0x00;                            // 22: UbeaVersion
    reply[23] = info.status1;                   // 23: Status1
    write_le16(reply + 24, info.esta);          // 24-25: EstaMan (little-endian per spec)

    // --- Names & Report (26-171) ---
    copy_padded(reply + 26, 18, info.short_name);  // 26-43:   ShortName[18]
    copy_padded(reply + 44, 64, info.long_name);   // 44-107:  LongName[64]
    copy_padded(reply + 108, 64, info.report);      // 108-171: NodeReport[64]

    // --- Port Info (172-177) ---
    uint8_t ports = std::min<uint8_t>(info.num_ports > 0 ? info.num_ports : 1, 4);
    reply[172] = 0x00;                           // 172: NumPortsHi
    reply[173] = ports;                          // 173: NumPortsLo

    for (uint8_t p = 0; p < 4; ++p) {
        reply[174 + p] = (p < ports) ? 0x80 : 0x00; // 174-177: PortTypes (0x80 = DMX512 output)
        reply[182 + p] = (p < ports) ? 0x80 : 0x00; // 182-185: GoodOutput (0x80 = transmitting)
        reply[190 + p] = (p < ports) ? info.sw_out[p] : 0x00; // 190-193: SwOut (bits 3..0 of Port-Address)
    }

    // --- Misc (194-199) ---
    reply[194] = 0;                              // 194: AcnPriority = 0
    reply[195] = 0;                              // 195: SwMacro = 0
    reply[196] = 0;                              // 196: SwRemote = 0
    reply[197] = 0;                              // 197-199: Spare[3] = 0
    reply[198] = 0;
    reply[199] = 0;

    // --- Style (200) ---
    reply[200] = info.style;                    // 200: Style

    // --- MAC (201-206) ---
    for (int i = 0; i < 6; ++i) {
        reply[201 + i] = info.mac[i];           // 201-206: MAC[6]
    }

    // --- Bind/Status2 (207-212) ---
    reply[211] = 1;                              // 211: BindIndex (1 = root device)
    reply[212] = info.status2;                  // 212: Status2

    return 239;
}

esp_err_t ArtnetDiscovery::start(const ArtnetNodeInfo& info, ArtnetDmxCallback dmx_callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }
    info_ = info;
    uint8_t net = 0, subnet = 0, univ = 0;
    decode_port_address(info_.universe, net, subnet, univ);
    info_.net = net;
    info_.subnet = subnet;
    info_.sw_out[0] = univ;
    dmx_callback_ = std::move(dmx_callback);
    running_ = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        task_entry,
        "lg_artnet",
        4096,
        this,
        4,
        &task_handle_,
        0
    );
    if (res != pdPASS) {
        running_ = false;
        task_handle_ = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ArtnetDiscovery::stop() {
    TaskHandle_t task_to_wait = nullptr;
    int sock_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!running_) return ESP_OK;
        running_ = false;
        sock_to_close = sock_;
        sock_ = -1;
        task_to_wait = task_handle_;
    }
    if (sock_to_close >= 0) {
        ::close(sock_to_close);
    }
    // Wait for the worker task to finish before returning, preventing
    // use-after-free if this object is destroyed or restarted immediately.
    if (task_to_wait) {
        // The task sets task_handle_ = nullptr before vTaskDelete.
        // Poll briefly; recvfrom timeout is 50ms max so it exits quickly.
        for (int i = 0; i < 40; ++i) {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (task_handle_ == nullptr) break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    return ESP_OK;
}

void ArtnetDiscovery::set_output_universes(const uint16_t* universes, uint8_t count) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!universes || count == 0) {
        info_.num_ports = 1;
        uint8_t net = 0, subnet = 0, univ = 0;
        decode_port_address(info_.universe, net, subnet, univ);
        info_.net = net;
        info_.subnet = subnet;
        info_.sw_out[0] = univ;
        return;
    }
    uint8_t n = (count < 4) ? count : 4;
    info_.num_ports = n;
    uint8_t net = 0, subnet = 0, univ = 0;
    decode_port_address(universes[0], net, subnet, univ);
    info_.net = net;
    info_.subnet = subnet;
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t p_net = 0, p_sub = 0, p_univ = 0;
        decode_port_address(universes[i], p_net, p_sub, p_univ);
        info_.sw_out[i] = p_univ;
    }
}

void ArtnetDiscovery::task_entry(void* arg) {
    static_cast<ArtnetDiscovery*>(arg)->task();
}

void ArtnetDiscovery::task() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(kTag, "socket failed");
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kArtnetPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(kTag, "bind failed");
        ::close(sock);
        vTaskDelete(nullptr);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        sock_ = sock;
    }

    uint8_t buffer[1024];

    while (running_) {
        sockaddr_in source = {};
        socklen_t source_len = sizeof(source);

        const int received = ::recvfrom(
            sock,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &source_len
        );

        if (received <= 0) {
            if (!running_) break;
            continue;
        }

        if (received < 10) continue;
        if (std::memcmp(buffer, "Art-Net\0", 8) != 0) continue;

        const uint16_t opcode = le16(buffer + 8);
        if (opcode == kOpPoll) {
            handle_poll(buffer, received, source, sock);
        } else if (opcode == kOpDmx) {
            handle_dmx(buffer, received, source);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        // Only close if stop() didn't already take ownership (sock_ == -1).
        if (sock_ == sock) {
            ::close(sock);
            sock_ = -1;
        }
        task_handle_ = nullptr;
    }
    vTaskDelete(nullptr);
}

bool ArtnetDiscovery::handle_poll(const uint8_t* packet, size_t length, const sockaddr_in& source, int sock) {
    if (length < 14) return false;
    
    sockaddr_in bcast = {};
    bcast.sin_family = AF_INET;
    bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    
    send_poll_reply(bcast, sock);
    return true;
}

bool ArtnetDiscovery::handle_dmx(const uint8_t* packet, size_t length, const sockaddr_in& source) {
    if (length < 18) return false;

    ArtnetDmxPacket out;
    out.sequence = packet[12];
    out.universe = le16(packet + 14);
    out.length = be16(packet + 16);
    out.source_ip = ntohl(source.sin_addr.s_addr);

    if (out.length == 0 || out.length > 512) return false;
    if (18 + out.length > length) return false;

    std::memcpy(out.data, packet + 18, out.length);
    
    if (dmx_callback_) {
        dmx_callback_(out);
    }
    return true;
}

void ArtnetDiscovery::send_poll_reply(const sockaddr_in& destination, int sock) {
    uint8_t reply[239] = {};
    ArtnetNodeInfo info_copy;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        info_copy = info_;
    }

    esp_netif_ip_info_t ip_info = {};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif) esp_netif_get_ip_info(netif, &ip_info);
    }

    if (netif) {
        const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&ip_info.ip.addr);
        info_copy.ip[0] = ip_bytes[0];
        info_copy.ip[1] = ip_bytes[1];
        info_copy.ip[2] = ip_bytes[2];
        info_copy.ip[3] = ip_bytes[3];
    }

    serialize_poll_reply(info_copy, reply, sizeof(reply));

    sockaddr_in dest = destination;
    dest.sin_port = htons(kArtnetPort);
    sendto(sock, reply, sizeof(reply), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

} // namespace lg
