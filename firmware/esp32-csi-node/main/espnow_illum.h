/**
 * @file espnow_illum.h
 * @brief ESP-NOW illuminator feasibility spike (sender + CSI receive diagnostic).
 *
 * Compiled only when CONFIG_ESPNOW_ILLUM_TX or CONFIG_ESPNOW_CSI_DIAG is set;
 * both default off, so a normal build contains none of this. Procedure and
 * pass/fail criteria: firmware/esp32-csi-node/docs/espnow-csi-spike.md.
 *
 * ESP-NOW itself is initialised by c6_sync_espnow_init(); this module only
 * adds sends to its broadcast peer and never calls esp_now_init().
 */
#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start whichever roles are compiled in. Call after c6_sync_espnow_init()
 * succeeded (the sender needs ESP-NOW and the broadcast peer to exist).
 */
esp_err_t espnow_illum_start(void);

#ifdef CONFIG_ESPNOW_CSI_DIAG
/**
 * Called from the CSI callback for EVERY callback, before any early return.
 *
 * @param taken       the 50 Hz gate accepted this callback
 * @param filter_ok   the ADR-060 filter_mac would let it through (true when
 *                    no filter is configured)
 */
void espnow_illum_diag_on_csi(const wifi_csi_info_t *info, bool taken, bool filter_ok);
#endif

#ifdef CONFIG_ESPNOW_ILLUM_TX
/** Forwarded from the single ESP-NOW send callback owned by c6_sync_espnow. */
void espnow_illum_on_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
#endif

#ifdef __cplusplus
}
#endif
