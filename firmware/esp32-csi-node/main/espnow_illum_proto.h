/**
 * @file espnow_illum_proto.h
 * @brief ESP-NOW illuminator spike: payload format and pure receive-side logic.
 *
 * Spike, not a protocol. The illuminator (an ESP32-C6 running the normal node
 * firmware with CONFIG_ESPNOW_ILLUM_TX) broadcasts one of these at a fixed
 * cadence and a forced PHY rate; receivers with CONFIG_ESPNOW_CSI_DIAG look
 * for it in the CSI callback to answer two questions on real silicon:
 *   1. does a forced-rate ESP-NOW broadcast produce a CSI callback at all, and
 *   2. is the frame body reachable from wifi_csi_info_t::payload?
 *
 * Payload (20 bytes, little-endian, carried as the ESP-NOW vendor body):
 *   [0..3]   magic     0x4D554C49 ('ILUM' in memory order)
 *   [4]      version   0x01
 *   [5]      rate_id   espnow_illum_rate_id_t the sender had configured
 *   [6]      node_id   sender's RuView node id
 *   [7]      reserved  0
 *   [8..11]  counter   u32, +1 per send attempt
 *   [12..19] epoch_us  u64, sender's mesh epoch (c6_sync_espnow) at send
 *
 * Everything here is pure (no ESP-IDF, no globals) so test/ can exercise the
 * exact functions the device build runs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ESPNOW_ILLUM_MAGIC        0x4D554C49u
#define ESPNOW_ILLUM_VERSION      0x01u
#define ESPNOW_ILLUM_PAYLOAD_LEN  20u

/* Expected over-the-air MPDU length (rx_ctrl.sig_len, FCS included) of an
 * illuminator frame, as documented for ESP-NOW v1: 24 B MAC header + 15 B
 * action/vendor-IE preamble (category, OUI, 4 random bytes, IE id, IE len,
 * OUI, type, version) + payload + 4 B FCS. Lets the diagnostic fingerprint
 * the sender's illuminator frames even if wifi_csi_info_t::payload turns out
 * to be unavailable. Unverified on silicon; the spike measures it. */
#define ESPNOW_ILLUM_MAC_HDR_LEN   24u
#define ESPNOW_ILLUM_VENDOR_HDR_LEN 15u
#define ESPNOW_ILLUM_FCS_LEN        4u
#define ESPNOW_ILLUM_FRAME_LEN (ESPNOW_ILLUM_MAC_HDR_LEN + ESPNOW_ILLUM_VENDOR_HDR_LEN + \
                                ESPNOW_ILLUM_PAYLOAD_LEN + ESPNOW_ILLUM_FCS_LEN)

/* 'SENP' beacon from c6_sync_espnow.c. Recognised only so the diagnostic can
 * separate the sender's 10 Hz sync beacons from its illuminator frames. */
#define ESPNOW_ILLUM_SYNC_MAGIC   0x53454E50u

typedef enum {
    ESPNOW_ILLUM_RATE_UNSET     = 0,  /* IDF default, never configured */
    ESPNOW_ILLUM_RATE_11B_1M    = 1,  /* DSSS: expected NOT to yield CSI */
    ESPNOW_ILLUM_RATE_11G_6M    = 2,  /* legacy OFDM, L-LTF only */
    ESPNOW_ILLUM_RATE_HT20_MCS0 = 3,  /* HT OFDM, L-LTF + HT-LTF */
    ESPNOW_ILLUM_RATE_COUNT     = 4,
} espnow_illum_rate_id_t;

typedef struct {
    uint8_t  version;
    uint8_t  rate_id;
    uint8_t  node_id;
    uint32_t counter;
    uint64_t epoch_us;
} espnow_illum_payload_t;

static inline uint32_t espnow_illum_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void espnow_illum_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/** Encode into buf. Returns bytes written, or 0 if buf_len is too small. */
static inline size_t espnow_illum_encode(uint8_t *buf, size_t buf_len,
                                         const espnow_illum_payload_t *pl)
{
    if (buf == NULL || pl == NULL || buf_len < ESPNOW_ILLUM_PAYLOAD_LEN) return 0;
    espnow_illum_wr32(&buf[0], ESPNOW_ILLUM_MAGIC);
    buf[4] = ESPNOW_ILLUM_VERSION;
    buf[5] = pl->rate_id;
    buf[6] = pl->node_id;
    buf[7] = 0;
    espnow_illum_wr32(&buf[8], pl->counter);
    espnow_illum_wr32(&buf[12], (uint32_t)pl->epoch_us);
    espnow_illum_wr32(&buf[16], (uint32_t)(pl->epoch_us >> 32));
    return ESPNOW_ILLUM_PAYLOAD_LEN;
}

/**
 * Find and decode an illuminator payload anywhere in buf.
 *
 * Scans rather than assuming an offset because where the ESP-NOW body starts
 * inside wifi_csi_info_t::payload (action category, OUI, random value, vendor
 * IE header) is one of the things the spike is measuring.
 *
 * @return offset of the magic within buf, or -1 if not found. A magic with an
 *         unknown version or too few trailing bytes is skipped.
 */
static inline int espnow_illum_find(const uint8_t *buf, size_t len,
                                    espnow_illum_payload_t *out)
{
    if (buf == NULL || len < ESPNOW_ILLUM_PAYLOAD_LEN) return -1;
    for (size_t off = 0; off + ESPNOW_ILLUM_PAYLOAD_LEN <= len; off++) {
        const uint8_t *p = &buf[off];
        if (espnow_illum_rd32(p) != ESPNOW_ILLUM_MAGIC) continue;
        if (p[4] != ESPNOW_ILLUM_VERSION) continue;
        if (out != NULL) {
            out->version  = p[4];
            out->rate_id  = p[5];
            out->node_id  = p[6];
            out->counter  = espnow_illum_rd32(&p[8]);
            out->epoch_us = (uint64_t)espnow_illum_rd32(&p[12]) |
                            ((uint64_t)espnow_illum_rd32(&p[16]) << 32);
        }
        return (int)off;
    }
    return -1;
}

/** True if a c6_sync_espnow 'SENP' beacon magic appears anywhere in buf. */
static inline bool espnow_illum_has_sync_magic(const uint8_t *buf, size_t len)
{
    if (buf == NULL) return false;
    for (size_t off = 0; off + 4 <= len; off++) {
        if (espnow_illum_rd32(&buf[off]) == ESPNOW_ILLUM_SYNC_MAGIC) return true;
    }
    return false;
}

static inline int espnow_illum_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Parse "aa:bb:cc:dd:ee:ff" (':' or '-' separators, either case).
 * Strict: exactly 17 characters, no surrounding whitespace.
 */
static inline bool espnow_illum_parse_mac(const char *s, uint8_t mac[6])
{
    if (s == NULL || mac == NULL || strlen(s) != 17) return false;
    for (int i = 0; i < 6; i++) {
        const char *p = &s[i * 3];
        int hi = espnow_illum_hexval(p[0]);
        int lo = espnow_illum_hexval(p[1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5 && p[2] != ':' && p[2] != '-') return false;
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static inline bool espnow_illum_mac_eq(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

/**
 * Coarse PHY class of a received frame, for attributing CSI without relying
 * on the payload. `legacy_rate` is the rx_ctrl rate code on non-HT frames;
 * codes below 8 are the 802.11b (DSSS/CCK) rates in wifi_phy_rate_t.
 */
typedef enum {
    ESPNOW_ILLUM_PHY_DSSS   = 0,
    ESPNOW_ILLUM_PHY_OFDM_G = 1,
    ESPNOW_ILLUM_PHY_HT     = 2,
    ESPNOW_ILLUM_PHY_OTHER  = 3,
    ESPNOW_ILLUM_PHY_COUNT  = 4,
} espnow_illum_phy_t;

/** Pre-HE receivers (ESP32-S3): rx_ctrl.sig_mode + rx_ctrl.rate. */
static inline espnow_illum_phy_t espnow_illum_phy_from_sig_mode(unsigned sig_mode,
                                                                unsigned legacy_rate)
{
    if (sig_mode == 1) return ESPNOW_ILLUM_PHY_HT;
    if (sig_mode == 0) return legacy_rate < 8 ? ESPNOW_ILLUM_PHY_DSSS
                                              : ESPNOW_ILLUM_PHY_OFDM_G;
    return ESPNOW_ILLUM_PHY_OTHER;
}

/** HE receivers (ESP32-C6): rx_ctrl.cur_bb_format (0=11b, 1=11g, 2=HT). */
static inline espnow_illum_phy_t espnow_illum_phy_from_bb_format(unsigned bb_format)
{
    switch (bb_format) {
    case 0:  return ESPNOW_ILLUM_PHY_DSSS;
    case 1:  return ESPNOW_ILLUM_PHY_OFDM_G;
    case 2:  return ESPNOW_ILLUM_PHY_HT;
    default: return ESPNOW_ILLUM_PHY_OTHER;
    }
}

/**
 * Continuity tracker for a monotonically increasing sequence of width `bits`
 * (12 for the 802.11 sequence number, 32 for the payload counter).
 *
 * A step of +1 is contiguous; +k (k>1, below half the space) records k-1
 * missing; 0 is a duplicate; anything in the upper half of the space is
 * treated as reordering/restart rather than a huge loss.
 */
typedef struct {
    bool     have_prev;
    uint32_t prev;
    uint32_t contiguous;
    uint32_t missing;
    uint32_t dup;
    uint32_t reorder;
} espnow_illum_seq_t;

static inline void espnow_illum_seq_update(espnow_illum_seq_t *t, uint32_t cur,
                                           unsigned bits)
{
    if (t == NULL || bits == 0 || bits > 32) return;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    cur &= mask;
    if (t->have_prev) {
        uint32_t step = (cur - t->prev) & mask;
        uint32_t half = (bits == 32) ? 0x80000000u : (1u << (bits - 1));
        if (step == 0)          t->dup++;
        else if (step == 1)     t->contiguous++;
        else if (step < half)   t->missing += step - 1;
        else                    t->reorder++;
    }
    t->prev = cur;
    t->have_prev = true;
}
