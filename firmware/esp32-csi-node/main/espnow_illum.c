/**
 * @file espnow_illum.c
 * @brief ESP-NOW illuminator feasibility spike — see espnow_illum.h.
 *
 * Sender (CONFIG_ESPNOW_ILLUM_TX): broadcasts an espnow_illum_proto.h payload
 * at CONFIG_ESPNOW_ILLUM_HZ, paced by esp_timer, at a forced PHY rate set with
 * esp_now_set_peer_rate_config() on the broadcast peer. In cycle mode the rate
 * rotates 1M DSSS -> 6M 11g -> HT20 MCS0 so one capture covers all three; the
 * payload carries the rate id so the receiver can attribute each frame.
 *
 * NOTE: the broadcast peer is shared with c6_sync_espnow, so its 10 Hz sync
 * beacons go out at the same forced rate while this runs.
 *
 * Receiver (CONFIG_ESPNOW_CSI_DIAG): counts CSI callbacks from the configured
 * sender MAC and/or carrying the payload magic, and logs a 1 Hz summary line
 * prefixed ILLUM_RX. Nothing here changes the ADR-018 stream.
 */

#include "espnow_illum.h"
#include "espnow_illum_proto.h"
#include "c6_sync_espnow.h"
#include "csi_collector.h"

#include <inttypes.h>
#include <string.h>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)
#error "ESP-NOW illuminator spike needs ESP-IDF >= 5.5 (esp_now_send_info_t, per-peer rate config)"
#endif

static const char *TAG = "espnow_illum";

static const char *rate_name(uint8_t id)
{
    switch (id) {
    case ESPNOW_ILLUM_RATE_11B_1M:    return "11b-1M";
    case ESPNOW_ILLUM_RATE_11G_6M:    return "11g-6M";
    case ESPNOW_ILLUM_RATE_HT20_MCS0: return "ht20-mcs0";
    default:                          return "unset";
    }
}

/* ======================================================================= */
#ifdef CONFIG_ESPNOW_ILLUM_TX

static const uint8_t s_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static TaskHandle_t       s_tx_task;
static esp_timer_handle_t s_tx_timer;

/* Written by the WiFi task (send callback), read by the TX task. Single
 * writer per counter; 32-bit loads are atomic on both targets. */
static volatile uint32_t s_cb_ok, s_cb_fail, s_cb_other;
static volatile uint8_t  s_cb_last_rate = 0xFF;

static esp_err_t apply_rate(uint8_t id)
{
    esp_now_rate_config_t cfg = {0};
    switch (id) {
    case ESPNOW_ILLUM_RATE_11B_1M:
        cfg.phymode = WIFI_PHY_MODE_11B;  cfg.rate = WIFI_PHY_RATE_1M_L;     break;
    case ESPNOW_ILLUM_RATE_11G_6M:
        cfg.phymode = WIFI_PHY_MODE_11G;  cfg.rate = WIFI_PHY_RATE_6M;       break;
    case ESPNOW_ILLUM_RATE_HT20_MCS0:
        cfg.phymode = WIFI_PHY_MODE_HT20; cfg.rate = WIFI_PHY_RATE_MCS0_LGI; break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return esp_now_set_peer_rate_config(s_bcast, &cfg);
}

static uint8_t rate_for_elapsed_s(uint32_t elapsed_s)
{
#if defined(CONFIG_ESPNOW_ILLUM_RATE_CYCLE)
    return (uint8_t)(ESPNOW_ILLUM_RATE_11B_1M +
                     (elapsed_s / CONFIG_ESPNOW_ILLUM_CYCLE_S) % 3u);
#elif defined(CONFIG_ESPNOW_ILLUM_RATE_11B_1M)
    (void)elapsed_s; return ESPNOW_ILLUM_RATE_11B_1M;
#elif defined(CONFIG_ESPNOW_ILLUM_RATE_11G_6M)
    (void)elapsed_s; return ESPNOW_ILLUM_RATE_11G_6M;
#else
    (void)elapsed_s; return ESPNOW_ILLUM_RATE_HT20_MCS0;
#endif
}

void espnow_illum_on_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    /* One ESP-NOW send callback serves both this module and the sync beacon,
     * both to the broadcast peer, so attribute by looking for our magic in the
     * transmitted frame. data starts at the MAC header and data_len is
     * documented as the body length; the scan stops at the end of where our
     * payload would be, so it cannot run past the frame whichever way data_len
     * is actually counted. */
    bool ours = false;
    if (tx_info != NULL && tx_info->data != NULL) {
        size_t want = ESPNOW_ILLUM_VENDOR_HDR_LEN + ESPNOW_ILLUM_PAYLOAD_LEN;
        size_t len  = tx_info->data_len < want ? tx_info->data_len : want;
        ours = espnow_illum_find(tx_info->data + ESPNOW_ILLUM_MAC_HDR_LEN, len, NULL) >= 0;
        if (ours) s_cb_last_rate = (uint8_t)tx_info->rate;
    }
    if (!ours)                              s_cb_other++;
    else if (status == ESP_NOW_SEND_SUCCESS) s_cb_ok++;
    else                                    s_cb_fail++;
}

static void tx_timer_cb(void *arg)
{
    (void)arg;
    xTaskNotifyGive(s_tx_task);
}

static void tx_task(void *arg)
{
    (void)arg;
    const uint32_t hz = CONFIG_ESPNOW_ILLUM_HZ;
    int64_t  t0 = esp_timer_get_time();
    uint32_t counter = 0, sent = 0, send_err = 0, ticks = 0;
    uint8_t  cur_rate = 0;
    esp_err_t rate_err = ESP_OK;
    uint32_t prev_ok = 0, prev_fail = 0, prev_other = 0, prev_sent = 0, prev_err = 0;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t elapsed_s = (uint32_t)((esp_timer_get_time() - t0) / 1000000);
        uint8_t want = rate_for_elapsed_s(elapsed_s);
        if (want != cur_rate) {
            rate_err = apply_rate(want);
            ESP_LOGI(TAG, "ILLUM_TX rate -> %s (esp_now_set_peer_rate_config=%s)",
                     rate_name(want), esp_err_to_name(rate_err));
            cur_rate = want;
        }

        espnow_illum_payload_t pl = {
            .rate_id  = cur_rate,
            .node_id  = csi_collector_get_node_id(),
            .counter  = counter++,
            .epoch_us = c6_sync_espnow_get_epoch_us(),
        };
        uint8_t buf[ESPNOW_ILLUM_PAYLOAD_LEN];
        espnow_illum_encode(buf, sizeof(buf), &pl);
        if (esp_now_send(s_bcast, buf, sizeof(buf)) == ESP_OK) sent++;
        else                                                  send_err++;

        if (++ticks % hz == 0) {
            uint32_t ok = s_cb_ok, fail = s_cb_fail, other = s_cb_other;
            ESP_LOGI(TAG, "ILLUM_TX t=%" PRIu32 "s rate=%s cfg=%s ctr=%" PRIu32
                          " sent=%" PRIu32 " send_err=%" PRIu32
                          " cb_ok=%" PRIu32 " cb_fail=%" PRIu32 " cb_other=%" PRIu32
                          " cb_rate=0x%02x",
                     elapsed_s, rate_name(cur_rate), esp_err_to_name(rate_err), counter,
                     sent - prev_sent, send_err - prev_err,
                     ok - prev_ok, fail - prev_fail, other - prev_other,
                     (unsigned)s_cb_last_rate);
            prev_sent = sent; prev_err = send_err;
            prev_ok = ok; prev_fail = fail; prev_other = other;
        }
    }
}

static esp_err_t tx_start(void)
{
    if (xTaskCreate(tx_task, "illum_tx", 4096, NULL, 5, &s_tx_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    const esp_timer_create_args_t args = {
        .callback = tx_timer_cb,
        .name     = "illum_tx",
    };
    esp_err_t r = esp_timer_create(&args, &s_tx_timer);
    if (r == ESP_OK) {
        r = esp_timer_start_periodic(s_tx_timer, 1000000ULL / CONFIG_ESPNOW_ILLUM_HZ);
    }
    ESP_LOGI(TAG, "ILLUM_TX start: %d Hz, mode=%s, payload=%u B: %s",
             CONFIG_ESPNOW_ILLUM_HZ,
#ifdef CONFIG_ESPNOW_ILLUM_RATE_CYCLE
             "cycle",
#else
             "fixed",
#endif
             (unsigned)ESPNOW_ILLUM_PAYLOAD_LEN, esp_err_to_name(r));
    return r;
}

#endif /* CONFIG_ESPNOW_ILLUM_TX */

/* ======================================================================= */
#ifdef CONFIG_ESPNOW_CSI_DIAG

/* Bytes of wifi_csi_info_t::payload searched for the magic. The ESP-NOW body
 * sits ~15 bytes into an action frame body; 96 also covers a payload pointer
 * that starts at the MAC header instead. */
#define DIAG_SCAN_LEN 96
#define DIAG_DUMP_LEN 48

typedef struct {
    uint32_t csi_total;       /* every CSI callback, any source */
    uint32_t mac_match;       /* info->mac == configured sender */
    uint32_t illum;           /* payload magic decoded (any source MAC) */
    uint32_t illum_other_mac; /* decoded, but info->mac != configured sender */
    uint32_t sync;            /* sender MAC, 'SENP' sync beacon */
    uint32_t other;           /* sender MAC, neither magic (its data frames etc.) */
    uint32_t payload_null;    /* sender MAC, payload NULL or empty */
    uint32_t len_fp;          /* sender MAC, sig_len == ESPNOW_ILLUM_FRAME_LEN */
    uint32_t mac_phy[ESPNOW_ILLUM_PHY_COUNT]; /* sender MAC frames by PHY class */
    uint32_t gate_won;        /* illum frames the 50 Hz gate accepted */
    uint32_t gate_lost;       /* illum frames the gate dropped */
    uint32_t filter_drop;     /* illum frames gate-accepted but ADR-060 filtered */
    uint32_t by_rate[ESPNOW_ILLUM_RATE_COUNT];
    uint32_t by_phy[ESPNOW_ILLUM_PHY_COUNT];
    int32_t  rssi_sum;
    int8_t   rssi_min, rssi_max;
    uint8_t  last_rate, last_mode, last_mcs, last_cwb;
    uint16_t last_len, last_payload_len;
    int16_t  last_magic_off;
} diag_window_t;

static portMUX_TYPE       s_mux = portMUX_INITIALIZER_UNLOCKED;
static diag_window_t      s_win;
static espnow_illum_seq_t s_ctr_seq;   /* payload counter, cumulative */
static espnow_illum_seq_t s_rx_seq;    /* 802.11 rx_seq of illum frames, cumulative */
static uint32_t           s_last_ctr;
static uint8_t            s_sender[6];
static bool               s_sender_set;

/* First-sample capture, printed once by the reporter (never log from the
 * WiFi task). */
static volatile bool s_dump_ready;
static bool          s_dump_taken;
static uint8_t       s_dump_mac[6];
static uint8_t       s_dump_pl[DIAG_DUMP_LEN];
static uint16_t      s_dump_pl_len;
static int16_t       s_dump_off;

static void diag_window_reset(diag_window_t *w)
{
    memset(w, 0, sizeof(*w));
    w->rssi_min = 127;
    w->rssi_max = -128;
    w->last_magic_off = -1;
}

void espnow_illum_diag_on_csi(const wifi_csi_info_t *info, bool taken, bool filter_ok)
{
    if (info == NULL) return;

    bool from_sender = s_sender_set && espnow_illum_mac_eq(info->mac, s_sender);
    bool have_pl = info->payload != NULL && info->payload_len > 0;
    size_t scan = have_pl ? (info->payload_len < DIAG_SCAN_LEN ? info->payload_len
                                                                : DIAG_SCAN_LEN) : 0;
    espnow_illum_payload_t pl;
    int off = -1;
    /* Scan every frame, not just the sender's: a decode from another MAC is
     * how a misconfigured sender MAC shows up (illum_othermac). The scan is
     * capped at DIAG_SCAN_LEN bytes, so the WiFi-task cost stays bounded. */
    if (have_pl) {
        off = espnow_illum_find(info->payload, scan, &pl);
    }
    bool is_sync = from_sender && have_pl && off < 0 &&
                   espnow_illum_has_sync_magic(info->payload, scan);

    unsigned mode, cwb, mcs;
    espnow_illum_phy_t phy;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    mode = info->rx_ctrl.cur_bb_format;
    cwb  = info->rx_ctrl.second != 0;
    mcs  = 0;
    phy  = espnow_illum_phy_from_bb_format(mode);
#else
    mode = info->rx_ctrl.sig_mode;
    cwb  = info->rx_ctrl.cwb;
    mcs  = info->rx_ctrl.mcs;
    phy  = espnow_illum_phy_from_sig_mode(mode, info->rx_ctrl.rate);
#endif

    taskENTER_CRITICAL(&s_mux);
    diag_window_t *w = &s_win;
    w->csi_total++;
    if (from_sender) {
        w->mac_match++;
        w->mac_phy[phy]++;
        if (info->rx_ctrl.sig_len == ESPNOW_ILLUM_FRAME_LEN) w->len_fp++;
        if (!have_pl)     w->payload_null++;
        else if (is_sync) w->sync++;
        else if (off < 0) w->other++;
    }
    if (off >= 0) {
        w->illum++;
        if (s_sender_set && !from_sender) w->illum_other_mac++;
        if (!taken)          w->gate_lost++;
        else if (!filter_ok) w->filter_drop++;
        else                 w->gate_won++;
        w->by_rate[pl.rate_id < ESPNOW_ILLUM_RATE_COUNT ? pl.rate_id : 0]++;
        w->by_phy[phy]++;
        int8_t rssi = (int8_t)info->rx_ctrl.rssi;
        w->rssi_sum += rssi;
        if (rssi < w->rssi_min) w->rssi_min = rssi;
        if (rssi > w->rssi_max) w->rssi_max = rssi;
        w->last_rate = (uint8_t)info->rx_ctrl.rate;
        w->last_mode = (uint8_t)mode;
        w->last_mcs  = (uint8_t)mcs;
        w->last_cwb  = (uint8_t)cwb;
        w->last_len  = info->len;
        w->last_payload_len = info->payload_len;
        w->last_magic_off   = (int16_t)off;
        espnow_illum_seq_update(&s_ctr_seq, pl.counter, 32);
        espnow_illum_seq_update(&s_rx_seq, info->rx_seq, 12);
        s_last_ctr = pl.counter;
    }
    /* Capture the first sender frame that carries our payload, or failing
     * that the first sender frame with any payload, to learn the layout. */
    if (!s_dump_taken && have_pl && (off >= 0 || (from_sender && !s_dump_ready))) {
        memcpy(s_dump_mac, info->mac, 6);
        s_dump_pl_len = info->payload_len;
        memcpy(s_dump_pl, info->payload,
               info->payload_len < DIAG_DUMP_LEN ? info->payload_len : DIAG_DUMP_LEN);
        s_dump_off   = (int16_t)off;
        s_dump_ready = true;
        if (off >= 0) s_dump_taken = true;
    }
    taskEXIT_CRITICAL(&s_mux);
}

static void diag_task(void *arg)
{
    (void)arg;
    bool dumped_illum = false, dumped_any = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        diag_window_t w;
        espnow_illum_seq_t cs, rs;
        uint32_t last_ctr;
        taskENTER_CRITICAL(&s_mux);
        w = s_win;
        diag_window_reset(&s_win);
        cs = s_ctr_seq;
        rs = s_rx_seq;
        last_ctr = s_last_ctr;
        taskEXIT_CRITICAL(&s_mux);

        int rssi_mean = w.illum ? (int)(w.rssi_sum / (int32_t)w.illum) : 0;
        ESP_LOGI(TAG, "ILLUM_RX csi=%" PRIu32 " mac=%" PRIu32 " illum=%" PRIu32
                      " illum_othermac=%" PRIu32 " sync=%" PRIu32 " other=%" PRIu32
                      " pl_null=%" PRIu32 " len%u=%" PRIu32
                      " mac_phy[dsss/ofdm/ht/other]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                      "/%" PRIu32 " gate_won=%" PRIu32 " gate_lost=%" PRIu32
                      " filt_drop=%" PRIu32,
                 w.csi_total, w.mac_match, w.illum, w.illum_other_mac, w.sync,
                 w.other, w.payload_null, (unsigned)ESPNOW_ILLUM_FRAME_LEN, w.len_fp,
                 w.mac_phy[0], w.mac_phy[1], w.mac_phy[2], w.mac_phy[3],
                 w.gate_won, w.gate_lost, w.filter_drop);
        ESP_LOGI(TAG, "ILLUM_RX rid[unset/1M/6M/mcs0]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                      "/%" PRIu32 " phy[dsss/ofdm/ht/other]=%" PRIu32 "/%" PRIu32
                      "/%" PRIu32 "/%" PRIu32 " rssi=%d/%d/%d rate=0x%02x mode=%u"
                      " mcs=%u cwb=%u csi_len=%u pl_len=%u off=%d",
                 w.by_rate[0], w.by_rate[1], w.by_rate[2], w.by_rate[3],
                 w.by_phy[0], w.by_phy[1], w.by_phy[2], w.by_phy[3],
                 w.illum ? w.rssi_min : 0, rssi_mean, w.illum ? w.rssi_max : 0,
                 w.last_rate, w.last_mode, w.last_mcs, w.last_cwb,
                 w.last_len, w.last_payload_len, w.last_magic_off);
        ESP_LOGI(TAG, "ILLUM_RX cum ctr_last=%" PRIu32 " ctr[+1/miss/dup/reord]=%" PRIu32
                      "/%" PRIu32 "/%" PRIu32 "/%" PRIu32
                      " rxseq[+1/miss/dup/reord]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                      "/%" PRIu32,
                 last_ctr, cs.contiguous, cs.missing, cs.dup, cs.reorder,
                 rs.contiguous, rs.missing, rs.dup, rs.reorder);

        if (s_dump_ready && (!dumped_any || (s_dump_off >= 0 && !dumped_illum))) {
            uint8_t mac[6], pl[DIAG_DUMP_LEN];
            uint16_t len; int16_t off;
            taskENTER_CRITICAL(&s_mux);
            memcpy(mac, s_dump_mac, 6);
            memcpy(pl, s_dump_pl, sizeof(pl));
            len = s_dump_pl_len; off = s_dump_off;
            taskEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "ILLUM_RX sample src=%02x:%02x:%02x:%02x:%02x:%02x"
                          " payload_len=%u magic_off=%d first %u bytes:",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                     len, off, (unsigned)(len < DIAG_DUMP_LEN ? len : DIAG_DUMP_LEN));
            ESP_LOG_BUFFER_HEX(TAG, pl, len < DIAG_DUMP_LEN ? len : DIAG_DUMP_LEN);
            dumped_any = true;
            if (off >= 0) dumped_illum = true;
        }
    }
}

static esp_err_t diag_start(void)
{
    diag_window_reset(&s_win);
    s_sender_set = espnow_illum_parse_mac(CONFIG_ESPNOW_CSI_DIAG_SENDER_MAC, s_sender);
    if (s_sender_set) {
        ESP_LOGI(TAG, "ILLUM_RX start: sender=%02x:%02x:%02x:%02x:%02x:%02x",
                 s_sender[0], s_sender[1], s_sender[2],
                 s_sender[3], s_sender[4], s_sender[5]);
    } else {
        ESP_LOGW(TAG, "ILLUM_RX start: sender MAC \"%s\" not set/invalid; "
                      "matching on payload magic only", CONFIG_ESPNOW_CSI_DIAG_SENDER_MAC);
    }
    return xTaskCreate(diag_task, "illum_rx", 4096, NULL, 3, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

#endif /* CONFIG_ESPNOW_CSI_DIAG */

esp_err_t espnow_illum_start(void)
{
    esp_err_t r = ESP_OK;
#ifdef CONFIG_ESPNOW_CSI_DIAG
    r = diag_start();
    if (r != ESP_OK) return r;
#endif
#ifdef CONFIG_ESPNOW_ILLUM_TX
    r = tx_start();
#endif
    (void)rate_name;
    return r;
}
