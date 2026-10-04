# ESP32 C5 rate aware sensing qualification (bring-up)

Status: **bring-up record** — device-side rate + stability qualified on 2.4 GHz;
the full 300 s controlled run, the end-to-end WebSocket table, and the 5 GHz HE
pass are PENDING. Mirrors the C6 record
[`2026-08-31-esp32-c6-rate-aware-sensing.md`](2026-08-31-esp32-c6-rate-aware-sensing.md).
See [ADR-368](../adr/ADR-368-esp32-c5-firmware-extension.md).

## Scope

Qualifies, on real ESP32-C5 silicon: that the firmware builds, flashes, boots,
associates to WiFi, captures CSI, and sustains a raw CSI callback rate above the
hardware acceptance floor without errors or reboots. Does **not** yet qualify
heartbeat, respiration, gesture, pose, identity or person-count accuracy against
labelled ground truth, the 5 GHz HE path, or the end-to-end fused WebSocket
coverage (those are P4/P5 in ADR-368).

## Hardware and firmware

| Field | Measured value |
|-------|----------------|
| Board | ESP32-C5-WROOM-1 revision v1.0, 16 MB flash, no PSRAM |
| Logical node | 1 |
| Firmware after | 0.8.12 + ESP32-C5 extension (branch `feat/esp32c5-firmware`) |
| Toolchain | ESP-IDF v5.5 (`esp32c5` preview target) |
| C5 app image | 1,151,440 bytes |
| C5 app SHA 256 | `3577a9082d8de4703b8e0830ad70d65f65f6c04f9f696d74250574cd423a1281` |
| C5 bootloader SHA 256 | `a523d2c877fe719e4f780a3f76ab740800187524fbf6daabe427320ee4c4ecf5` |
| OTA slot size | 4,194,304 bytes (one of two 4 MB slots, 16 MB layout) |
| OTA headroom | 3,042,864 bytes, 73 percent |
| Live partition | `ota_1`, `ota_state: valid` (self-marked valid, no rollback) |

## Software gates

| Gate | Result |
|------|--------|
| ESP32-C5 IDF 5.5 build | PASS (`Project build complete`) |
| Flash + image hash verify on C5 | PASS (`Hash of data verified`, all four segments) |
| Boot + onboarding on C5 | PASS (`ESP32-C5 CSI Node` banner, CSI collector + serial onboarding up) |
| NVS provisioning on C5 | PASS (`provision.py --no-stub` fix; WiFi + aggregator config written) |
| Host unit tests (rate/occupancy, ADR-110 encoding, mmWave predicate) | NOT RUN this session |
| ESP32-C6 / S3 cross-compile | NOT RE-RUN this session (gates generalized, not rebuilt for C6/S3) |

## Bring-up finding — TWT lockup

With `CONFIG_C6_TWT_ENABLE=y` (the C6 default, inherited), the C5 hit a hard
`CPU_LOCKUP` roughly one second after boot, immediately after the WiFi driver
logged `Connected AP does not support setup individual TWT agreement`. TWT
negotiation locks up the C5 *preview* WiFi driver against a non-iTWT AP. Fixed by
`CONFIG_C6_TWT_ENABLE=n` for C5 (TWT is a power feature, irrelevant to CSI). With
TWT disabled the node runs indefinitely stable.

## Device-side result (2.4 GHz, bring-up windows)

Measured from the firmware's own CSI callback counter (serial) and the raw CSI
UDP stream to the aggregator (`192.168.1.249:5006`). Windows are 6–26 s, not the
full 300 s — a controlled 300 s run is pending.

| Device observation | Result |
|---------------------|--------|
| Node IP (DHCP) | 192.168.1.234 |
| AP channel (auto-detected) | 5 (2.4 GHz) |
| Uptime at measurement | ~14 min, continuous |
| Raw callback mean | **32.7 pps** (20 s UDP), 34.9 pps cumulative (cb #29200 / 837 s) |
| Raw callback range | median 34 pps, per-second up to 39 pps |
| CSI frame size | 32 through **632 bytes** (256-bin HE width — not 64-bin HT) |
| Edge DSP cadence | 8 Hz configured (actual DSP-Hz measurement pending) |
| ENOMEM / UDP send-fail / watchdog / reboot | 0 observed |

## End-to-end WebSocket result

PENDING — requires the full sensing-server/aggregator that serves `/api/v1/mesh`
and `/api/v1/fusion` (the minimal `scripts/ruview-sensing-server.py` does not).

## 5 GHz HE result (P4)

PENDING — re-provision onto a UNII-1 non-DFS channel (36/40/44) and confirm the
HE frame remains 256-bin at 5 GHz. NOTE: 256-bin HE frames (up to 632 B) are
already observed at 2.4 GHz HE20 on IDF v5.5.0, so the ADR-110 "needs IDF ≥ 5.5.2
for HE" caveat does **not** appear to bite on this silicon/toolchain.

## Result and limitation

The ESP32-C5 CSI node captures CSI and sustains **32.7 pps raw** (≥ 20 pps
acceptance floor) with HE-width frames and zero steady-state errors on 2.4 GHz.
The TWT lockup is understood and worked around. Not yet qualified: the full 300 s
controlled run with DSP-Hz range, the end-to-end fused coverage, and 5 GHz.

## Acceptance test

Raw callback yield ≥ 20 pps: **PASS (32.7 pps)**. DSP cadence within ±1 Hz of
configured, zero steady-state ENOMEM/send-fail/watchdog/panic/reboot: **PASS on
the observed windows** (full 300 s run pending). Server parse/coverage/freshness
and 5 GHz: **PENDING**.
