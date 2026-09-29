/* Host tests for the ESP-NOW illuminator spike's pure logic
 * (../main/espnow_illum_proto.h): payload round trip, magic search at an
 * unknown offset, MAC parsing, PHY classification and sequence continuity. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "espnow_illum_proto.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
                                          __FILE__, __LINE__, #c); failures++; } } while (0)

static void test_roundtrip_and_layout(void)
{
    espnow_illum_payload_t in = {
        .rate_id = ESPNOW_ILLUM_RATE_HT20_MCS0, .node_id = 5,
        .counter = 0xA1B2C3D4u, .epoch_us = 0x0102030405060708ull,
    };
    uint8_t buf[ESPNOW_ILLUM_PAYLOAD_LEN];
    CHECK(espnow_illum_encode(buf, sizeof(buf), &in) == ESPNOW_ILLUM_PAYLOAD_LEN);
    CHECK(espnow_illum_encode(buf, sizeof(buf) - 1, &in) == 0);

    /* Wire bytes are fixed little-endian, independent of host order. */
    const uint8_t want[ESPNOW_ILLUM_PAYLOAD_LEN] = {
        'I', 'L', 'U', 'M', 0x01, 0x03, 0x05, 0x00,
        0xD4, 0xC3, 0xB2, 0xA1,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    };
    CHECK(memcmp(buf, want, sizeof(want)) == 0);

    espnow_illum_payload_t out;
    CHECK(espnow_illum_find(buf, sizeof(buf), &out) == 0);
    CHECK(out.version == ESPNOW_ILLUM_VERSION);
    CHECK(out.rate_id == in.rate_id && out.node_id == in.node_id);
    CHECK(out.counter == in.counter && out.epoch_us == in.epoch_us);
}

static void test_find_at_offset(void)
{
    /* Shaped like an ESP-NOW action body: category 0x7F, Espressif OUI, 4
     * random bytes, vendor IE (DD len OUI type ver), then our payload. */
    uint8_t frame[64];
    memset(frame, 0xEE, sizeof(frame));
    const uint8_t pre[15] = {0x7F, 0x18, 0xFE, 0x34, 1, 2, 3, 4,
                             0xDD, 0x19, 0x18, 0xFE, 0x34, 0x04, 0x01};
    memcpy(frame, pre, sizeof(pre));
    espnow_illum_payload_t in = {.rate_id = 2, .node_id = 5, .counter = 7, .epoch_us = 9};
    espnow_illum_encode(&frame[15], sizeof(frame) - 15, &in);

    espnow_illum_payload_t out;
    CHECK(espnow_illum_find(frame, sizeof(frame), &out) == 15);
    CHECK(out.counter == 7 && out.rate_id == 2);
    CHECK(espnow_illum_find(frame, 15 + ESPNOW_ILLUM_PAYLOAD_LEN, &out) == 15);
    /* Truncated: magic present but not enough trailing bytes. */
    CHECK(espnow_illum_find(frame, 15 + ESPNOW_ILLUM_PAYLOAD_LEN - 1, &out) == -1);
    CHECK(espnow_illum_find(NULL, 64, &out) == -1);

    /* Wrong version is skipped, a later valid copy is still found. */
    uint8_t two[2 * ESPNOW_ILLUM_PAYLOAD_LEN];
    espnow_illum_encode(two, ESPNOW_ILLUM_PAYLOAD_LEN, &in);
    two[4] = 0x02;
    in.counter = 99;
    espnow_illum_encode(&two[ESPNOW_ILLUM_PAYLOAD_LEN], ESPNOW_ILLUM_PAYLOAD_LEN, &in);
    CHECK(espnow_illum_find(two, sizeof(two), &out) == (int)ESPNOW_ILLUM_PAYLOAD_LEN);
    CHECK(out.counter == 99);

    /* The sync beacon is not mistaken for an illuminator frame. */
    uint8_t senp[16] = {0x50, 0x4E, 0x45, 0x53, 0x01, 0x01};
    CHECK(espnow_illum_find(senp, sizeof(senp), NULL) == -1);
    CHECK(espnow_illum_has_sync_magic(senp, sizeof(senp)));
    CHECK(!espnow_illum_has_sync_magic(frame, sizeof(frame)));

    /* The sig_len fingerprint matches the preamble laid out above. */
    CHECK(ESPNOW_ILLUM_VENDOR_HDR_LEN == sizeof(pre));
    CHECK(ESPNOW_ILLUM_FRAME_LEN == 63);
}

static void test_mac(void)
{
    uint8_t m[6];
    const uint8_t c6[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    CHECK(espnow_illum_parse_mac("02:00:00:00:00:02", m) && espnow_illum_mac_eq(m, c6));
    CHECK(espnow_illum_parse_mac("02-00-00-00-00-02", m) && espnow_illum_mac_eq(m, c6));
    CHECK(!espnow_illum_parse_mac("", m));
    CHECK(!espnow_illum_parse_mac(NULL, m));
    CHECK(!espnow_illum_parse_mac("02:00:00:00:00", m));
    CHECK(!espnow_illum_parse_mac("02:00:00:00:00:02 ", m));
    CHECK(!espnow_illum_parse_mac("02:00:00:00:00:0g", m));
    CHECK(!espnow_illum_parse_mac("48.f6.ee.c5.30.c8", m));
    const uint8_t other[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};
    CHECK(!espnow_illum_mac_eq(c6, other));
}

static void test_phy_class(void)
{
    CHECK(espnow_illum_phy_from_sig_mode(0, 0x00) == ESPNOW_ILLUM_PHY_DSSS);   /* 1M */
    CHECK(espnow_illum_phy_from_sig_mode(0, 0x07) == ESPNOW_ILLUM_PHY_DSSS);   /* 11M S */
    CHECK(espnow_illum_phy_from_sig_mode(0, 0x0B) == ESPNOW_ILLUM_PHY_OFDM_G); /* 6M */
    CHECK(espnow_illum_phy_from_sig_mode(1, 0x00) == ESPNOW_ILLUM_PHY_HT);
    CHECK(espnow_illum_phy_from_sig_mode(3, 0x00) == ESPNOW_ILLUM_PHY_OTHER);
    CHECK(espnow_illum_phy_from_bb_format(0) == ESPNOW_ILLUM_PHY_DSSS);
    CHECK(espnow_illum_phy_from_bb_format(1) == ESPNOW_ILLUM_PHY_OFDM_G);
    CHECK(espnow_illum_phy_from_bb_format(2) == ESPNOW_ILLUM_PHY_HT);
    CHECK(espnow_illum_phy_from_bb_format(4) == ESPNOW_ILLUM_PHY_OTHER);
}

static void test_seq(void)
{
    espnow_illum_seq_t t = {0};
    espnow_illum_seq_update(&t, 4094, 12);
    espnow_illum_seq_update(&t, 4095, 12);  /* +1 */
    espnow_illum_seq_update(&t, 0, 12);     /* +1 across the 12-bit wrap */
    espnow_illum_seq_update(&t, 3, 12);     /* 2 missing */
    espnow_illum_seq_update(&t, 3, 12);     /* dup */
    espnow_illum_seq_update(&t, 1, 12);     /* backwards: reorder */
    espnow_illum_seq_update(&t, 0x1002, 12);/* masked to 2: +1 */
    CHECK(t.contiguous == 3 && t.missing == 2 && t.dup == 1 && t.reorder == 1);

    espnow_illum_seq_t c = {0};
    espnow_illum_seq_update(&c, 0xFFFFFFFFu, 32);
    espnow_illum_seq_update(&c, 0, 32);     /* 32-bit wrap */
    espnow_illum_seq_update(&c, 10, 32);    /* 9 missing */
    CHECK(c.contiguous == 1 && c.missing == 9 && c.dup == 0 && c.reorder == 0);

    espnow_illum_seq_t bad = {0};
    espnow_illum_seq_update(&bad, 1, 0);    /* invalid width: ignored */
    espnow_illum_seq_update(NULL, 1, 12);
    CHECK(!bad.have_prev);
}

int main(void)
{
    test_roundtrip_and_layout();
    test_find_at_offset();
    test_mac();
    test_phy_class();
    test_seq();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: ESP-NOW illuminator payload, MAC match, PHY class, seq continuity");
    return 0;
}
