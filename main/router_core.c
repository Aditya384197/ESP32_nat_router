#include "router_core.h"
#include "router_config.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"


#include "freertos/FreeRTOS.h"

#include <string.h>

/*
 * Minimal high-performance NAT router:
 *
 *   Core 0 : ESP32 Wi-Fi driver/radio path (driver controlled)
 *   Core 1 : lwIP TCP/IP + IPv4 NAPT (Kconfig affinity)
 *
 * No web server, no statistics task, no LED task, no runtime configuration,
 * no OTA, no periodic polling and no application logging in the hot path.
 *
 * The ESP32 APSTA radio is single-channel. The external STA AP channel has
 * priority, so the SoftAP follows the upstream AP channel. The STA is NOT
 * locked to a BSSID or channel, allowing the upstream AP to move among
 * channels 1/6/11 and reconnect using the normal Wi-Fi driver scan logic.
 * The STA channel is kept at 0 so a reconnect after an upstream channel
 * change performs a fresh scan instead of being trapped on the old channel.
 */

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;
static bool s_napt_enabled = false;

/* Keep the last successfully used channel as a reconnect hint.
 * If the upstream AP moves, the driver can fall back to its normal scan.
 */

static void configure_ap_netif(void)
{
    esp_netif_ip_info_t ip_info = {0};

    ESP_ERROR_CHECK(esp_netif_str_to_ip4(AP_IP, &ip_info.ip));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(AP_GW, &ip_info.gw));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(AP_NETMASK, &ip_info.netmask));

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
}

static void apply_radio_performance(void)
{
    /* No modem power-save: this router is always-on. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /*
     * 84 quarter-dBm is a request for the highest supported level.
     * The Wi-Fi driver/PHY/regulatory domain clamps it to the legal
     * hardware limit.
     */
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));

    /*
     * Do not advertise legacy 802.11b rates. 11g/n is the speed-oriented
     * 2.4-GHz configuration. We intentionally do not force HT40 because
     * APSTA/channel coexistence and RF interference can make forced HT40
     * slower and less stable than automatic bandwidth selection.
     */
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_STA, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_AP, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            /*
             * The first connection is initiated by the driver event path.
             * No reconnect task is needed.
             */
            (void)esp_wifi_clear_fast_connect();
            (void)esp_wifi_connect();
            return;
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            /*
             * Stop forwarding while there is no upstream route.
             * Reconnect immediately through the Wi-Fi driver's own event
             * mechanism. The STA is not BSSID/channel locked.
             */
            if (s_napt_enabled) {
                (void)esp_netif_napt_disable(s_ap_netif);
                s_napt_enabled = false;
            }

            (void)esp_wifi_connect();
            return;
        }

        if (event_id == WIFI_EVENT_AP_STACONNECTED ||
            event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            /*
             * Intentionally no per-client accounting or logging.
             */
            return;
        }

        return;
    }

    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /*
         * In APSTA, the external AP's channel has priority. The SoftAP
         * therefore follows whatever channel the upstream AP is using.
         */
        /* Enable NAPT on the ESP-NETIF AP interface.  This is the public
         * ESP-IDF 5.1 API and avoids depending on an internal lwIP netif
         * pointer. */
        if (esp_netif_napt_enable(s_ap_netif) == ESP_OK) {
            s_napt_enabled = true;
        }
    }
}

esp_err_t router_core_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    if (s_ap_netif == NULL || s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * Fixed RX buffers avoid runtime allocation in the Wi-Fi receive path.
     * 16 is deliberately retained as a conservative high-throughput/stability
     * point; increasing it blindly only consumes RAM and is not guaranteed to
     * increase sustained NAT throughput.
     */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_cfg.static_rx_buf_num = 16;

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_cfg = {0};
    memcpy(ap_cfg.ap.ssid, AP_SSID, strlen(AP_SSID));
    memcpy(ap_cfg.ap.password, AP_PASS, strlen(AP_PASS));
    ap_cfg.ap.ssid_len = strlen(AP_SSID);
    ap_cfg.ap.channel = AP_CHANNEL;
    ap_cfg.ap.max_connection = AP_MAX_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    wifi_config_t sta_cfg = {0};
    memcpy(sta_cfg.sta.ssid, STA_SSID, strlen(STA_SSID));
    memcpy(sta_cfg.sta.password, STA_PASS, strlen(STA_PASS));

    /*
     * FAST_SCAN is preferable for a single known upstream SSID:
     * it stops as soon as a matching AP is found. Since bssid_set=0 and
     * channel=0, a change among 1/6/11 does not permanently lock the STA.
     */
    sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
    sta_cfg.sta.bssid_set = false;
    sta_cfg.sta.channel = 0; /* never lock the STA to a channel */
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_cfg.sta.threshold.rssi = -127;
    sta_cfg.sta.failure_retry_cnt = 5;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    configure_ap_netif();

    ESP_ERROR_CHECK(esp_wifi_start());

    /*
     * Country policy and radio performance are applied after start.
     * This keeps the regulatory channel/power table authoritative.
     */
    ESP_ERROR_CHECK(esp_wifi_set_country_code("IN", true));
    apply_radio_performance();

    /*
     * Do not use modem sleep. For a mains-powered router, keeping the radio
     * continuously awake gives the most deterministic latency/throughput.
     *
     * Protocol is already restricted to 11g/n. We deliberately do NOT force
     * HT40: on a busy 2.4-GHz environment automatic bandwidth selection can
     * sustain more useful throughput than a forced 40-MHz channel.
     */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /*
     * Explicitly start the connection. If the Airtel AP later changes
     * channel, WIFI_EVENT_STA_DISCONNECTED triggers another connect.
     */
    (void)esp_wifi_connect();

    return ESP_OK;
}
