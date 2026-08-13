# ESP32 NAT Router — High Performance Minimal Build

## Goal

A hard-coded, plug-and-play ESP32 NAT router with no web server, no OTA,
no dashboard, no LED task, no statistics task and no runtime configuration.

Only the upstream STA credentials and the ESP32 SoftAP credentials are
compile-time constants in `main/router_config.h`.

## Data path

- ESP32 classic dual-core @ 240 MHz.
- Wi-Fi driver/radio remains under the ESP32 Wi-Fi subsystem.
- lwIP TCP/IP task is pinned to Core 1.
- IPv4 NAPT performs the translation.
- Wi-Fi power save is disabled.
- Wi-Fi 802.11g/n is used for the speed-oriented 2.4-GHz configuration.
- AMPDU TX/RX is enabled.
- Fixed Wi-Fi RX buffers are used.
- TCP windows/mailboxes are enlarged for sustained throughput.
- TCP timestamps and keepalive are disabled to avoid unnecessary overhead.
- No application-level periodic task runs.

## Upstream channel changes

The ESP32 is APSTA and has one 2.4-GHz radio. In APSTA mode the external
upstream AP's channel has priority, so the ESP32 SoftAP follows the upstream
STA channel.

The STA is **not locked to a BSSID** and starts with no fixed channel on first
boot. It uses `WIFI_FAST_SCAN`, so it can locate the hard-coded SSID on the
current channel without scanning every channel after a match is found.

After a successful connection, the last channel is remembered in RAM as a
reconnect hint. If the upstream AP changes channel and the STA disconnects,
the Wi-Fi event path calls `esp_wifi_connect()` again; the driver can scan
for the same SSID on another channel. No custom polling task is needed.

A fundamental APSTA limitation remains: when the upstream radio changes
channel, the ESP32's SoftAP must follow that single radio channel. Connected
downstream clients may therefore need to reassociate. This is a hardware/
Wi-Fi architecture limitation, not a NAT software bug.

## Build

ESP-IDF 5.1.2:

```text
idf.py set-target esp32
idf.py build
```

## What is intentionally absent

- Web server
- Web configuration
- OTA
- NVS runtime configuration
- LED subsystem
- Statistics/RSSI polling
- Dashboard
- Serial status loop
- Application reconnect task
- Bluetooth

## Important performance principle

The goal is not to keep both CPUs at 100% utilization. The goal is maximum
sustained Mbps with low packet loss and stable memory/CPU behavior. Unused
CPU headroom is useful because it prevents queue starvation and watchdog
problems under bursts.
