#ifndef ROUTER_CONFIG_H
#define ROUTER_CONFIG_H

/*
 * HARD-CODED PLUG-AND-PLAY CONFIGURATION
 * Only these four strings should ever need changing.
 */

#define STA_SSID        "Airtel_2.4GHz"
#define STA_PASS        "Kgf@0987"

#define AP_SSID         "ESP32_Router"
#define AP_PASS         "ak@12345"

#define AP_IP           "192.168.4.1"
#define AP_GW           "192.168.4.1"
#define AP_NETMASK      "255.255.255.0"

/*
 * APSTA uses one 2.4-GHz radio. The upstream STA channel has priority,
 * so the SoftAP follows the upstream AP's current channel automatically.
 * This value is only the initial/default AP configuration.
 */
#define AP_CHANNEL      6
#define AP_MAX_CONN     5

#endif
