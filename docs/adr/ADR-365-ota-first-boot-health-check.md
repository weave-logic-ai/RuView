# ADR-365: An OTA'd image confirms itself with a bounded health check, or rolls back

**Status:** Accepted (host-tested and built for S3 and C6; not yet hardware-verified)
**Date:** 2026-09-29
**Numbering:** ADR-364 is the highest number on any branch at the time of
writing. If another branch also takes 365, renumber whichever merges second.

## Context

Every 16 MB node was flashed with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
(`sdkconfig.defaults.16mb`). With that option the bootloader starts a freshly
OTA'd image in `ESP_OTA_IMG_PENDING_VERIFY`. The app must then call
`esp_ota_mark_app_valid_cancel_rollback()`. If it doesn't, the bootloader marks
the image `ABORTED` at the next reset and boots the previous slot.

Nothing in `firmware/esp32-csi-node/main` made that call, and `nm` on the ELF
confirmed the symbol was not linked. RUNBOOK.md said an
`ota_rollback_boot_check()` "sits in the app", but no such function existed
anywhere in the tree. The consequences:

- An image delivered by `POST /ota` ran until the next reset, then reverted
  silently. OTA updates were not durable.
- While the image was pending, `esp_ota_begin()` refused a second OTA with
  `ESP_ERR_OTA_ROLLBACK_INVALID_STATE`.
- Nothing checked health, so a bad image was never rolled back on purpose. It
  only reverted if it happened to crash or be power-cycled.

## Decision

At boot, `ota_health_start()` (in `main/ota_health.c`, called early in
`app_main`) reads the running partition's state:

| state | meaning here | action |
|---|---|---|
| `PENDING_VERIFY` | just OTA'd, rollback armed | run the health check |
| `VALID` | USB-flashed with `ota_data_initial.bin` (the bootloader writes VALID), or already confirmed | nothing |
| `UNDEFINED` | selected by a build without rollback; boots unconditionally | nothing |
| `NEW` | bootloader lacks rollback, so it never promoted the image | nothing |
| error (`NOT_FOUND`/`NOT_SUPPORTED`) | no otadata entry, or factory partition | nothing |

For this table I read the ESP-IDF v5.5 source: `bootloader_utility.c`
(`set_actual_ota_seq`, the NEW to PENDING_VERIFY to ABORTED transitions) and
`esp_ota_ops.c`.

**The health check.** A low-priority task polls once a second and feeds
`ota_health_step()`, which is a pure function in `ota_health.h`:

- **Signals, both latched:** the STA got an IP (`IP_EVENT_STA_GOT_IP`, via
  `ota_health_note_got_ip()` in `main.c`'s handler), and the stream sender
  accepted at least one CSI frame (`csi_collector_get_send_ok_count() > 0`).
  The second signal covers driver callback, serialization and `sendto()`
  together.
- **Minimum uptime** `CONFIG_OTA_HEALTH_MIN_UPTIME_S` (default 30 s): the image
  is not confirmed before this point even with both signals present. An image
  that crashes soon after starting its pipelines still reverts, because a crash
  while pending is a rollback.
- **Deadline** `CONFIG_OTA_HEALTH_TIMEOUT_S` (default 120 s, measured from
  boot). Both signals are required by then. The minimum uptime is clamped to
  the deadline.
- **Pass:** `esp_ota_mark_app_valid_cancel_rollback()`, which logs
  `marked valid after <reason> in <ms> ms`.
- **Fail:** logs `health check failed: <reason>, rolling back`, then calls
  `esp_ota_mark_app_invalid_rollback_and_reboot()`. If no other bootable slot
  exists, that call returns an error. The node then logs and stays up rather
  than reboot-looping.
- **Terminal verdict:** once the check has confirmed or rolled back, it never
  changes.

**Negative-test hook.** `CONFIG_OTA_HEALTH_FORCE_FAIL` (default n, test only)
makes the check roll back at the point where it would have confirmed. Use it to
prove rollback end to end.

**Remote visibility.** `GET /ota/status` now includes `ota_state` (`valid`,
`pending_verify`, `new`, `undefined`, `other`, or `none`). An operator can
confirm durability without a serial console.

When the build has no `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (the 8 MB S3 and
4 MB C6 CI lanes), both entry points compile to no-ops.

## Consequences

- OTA updates become durable on rollback-enabled nodes once the image passes
  the check. A second OTA is accepted after confirmation, which can come as
  early as `MIN_UPTIME_S`.
- A bad image now reverts instead of limping, but "bad" only means it failed
  to reach the network or deliver CSI. An image that passes and then
  misbehaves later is not caught.
- If the AP is down when a node boots an OTA'd image, a good image rolls back.
  That is the safe direction: push it again later.
- Mock/QEMU builds that skip Wi-Fi can never pass. They must not enable
  bootloader rollback, and none do today.
- Bootloader capability is still invisible remotely (RUNBOOK §2). A node
  whose bootloader lacks rollback reports `ota_state: "new"` after an OTA. That
  is the first remote hint of which bootloader a board has.

## Evidence

- Host test `firmware/esp32-csi-node/test/test_ota_health.c` runs in
  `make host_tests`, which CI runs. It covers pass, each timeout reason, the
  terminal verdict, force-fail, the clamp, and zero soak. SYNTHETIC: it
  exercises the decision function, not a device.
- ESP-IDF v5.5 S3 and C6 builds with rollback enabled link
  `esp_ota_mark_app_valid_cancel_rollback` (verified with `nm`). A build is not
  hardware evidence.
- Hardware verification is pending. The procedure is in RUNBOOK §2.1.
