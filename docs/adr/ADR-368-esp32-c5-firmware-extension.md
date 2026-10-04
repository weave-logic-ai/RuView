# ADR-368: ESP32-C5 firmware extension — dual-band (2.4 + 5 GHz) Wi-Fi 6 CSI

| Field | Value |
|-------|-------|
| **Status** | Proposed — firmware **builds, flashes, and boots/runs on real C5 silicon with dual-band WiFi active** (2026-10-04, P1+P2 done); physical CSI rate/accuracy qualification (P3–P4) + validation record (P5) PENDING |
| **Date** | 2026-10-04 (created) |
| **Deciders** | Mathew Beane (WeaveLogic) |
| **Codename** | **C5-DUALBAND** |
| **Extends** | [ADR-110](ADR-110-esp32-c6-firmware-extension.md) (ESP32-C6 firmware extension — the template this mirrors) |
| **Relates to** | ADR-018 (CSI binary frame format), ADR-029 (RuvSense multistatic — "5 GHz unavailable on S3; C6 for dual-band"), ADR-347 (rate-aware sensing), ADR-346 (fail-closed occupancy), ADR-357 (raw-CSI calibration integrity), ADR-304 (evidence engine), ADR-182 (harness hardening) |
| **Hardware** | ESP32-C5-WROOM-1 (rev v1.0), 16 MB flash, no PSRAM, native USB-Serial/JTAG, on an ESP32-C5-DevKitC-1 |
| **Toolchain** | ESP-IDF **v5.5** (`esp32c5` is a *preview* target — build with `idf.py --preview set-target esp32c5`) |

---

## 1. Context

ADR-110 brought the RuView CSI node to the ESP32-C6: Wi-Fi 6 (HE) CSI, 802.15.4,
TWT and an LP-core, qualified at 8 Hz on-device DSP. The C6's one hard limit for
sensing is that its Wi-Fi is **2.4 GHz only** — ADR-029 explicitly notes "5 GHz
CSI unavailable on S3; ESP32-C6 for dual-band", but the C6 cannot actually
*source* 5 GHz. The host/provisioning side already understands 5 GHz channels
(`provision.py` accepts 36–177; the firmware README ships a `5ghz-channel`
preset; `csi_collector.c` already hops `{1,6,11,36,40,44}`), so the whole stack
has been waiting for a chip that can transmit there.

The **ESP32-C5** is that chip: dual-band Wi-Fi 6 (2.4 **+ 5 GHz**), BT 5 LE,
IEEE 802.15.4, a 240 MHz HP RISC-V core + LP core. It is the same
`SOC_WIFI_HE_SUPPORT` HE class as the C6, which is why most of the C6 firmware
applies unchanged — and the 5 GHz band's ~6 cm wavelength (vs 12.5 cm at
2.4 GHz) is finer motion detail for sensing, on much cleaner air.

### 1.1 What this ADR is *not*

Not a new CSI pipeline, frame format, evidence methodology or qualification
procedure. The C5 image is a *frozen artifact*; the SAME ADR-110 qualification
harness (`harness/ruview/`, `run_arms.py`, the dated validation `.md`) runs
against it and emits the SAME evidence. This ADR is the bring-up of a new build
target, nothing more.

## 2. Decision

Add `esp32c5` as a third build target alongside `esp32s3` (production) and
`esp32c6` (research), by generalizing the C6's HE-class feature gates to the
C6/C5 class rather than forking the firmware.

### 2.1 Target overlay

`sdkconfig.defaults.esp32c5` (new), layered by `idf.py set-target esp32c5`:
- **16 MB flash**, `partitions_16mb.csv` (two 4 MB OTA slots — real dual-image
  OTA, vs the C6's single 4 MB), with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
  + coredump folded in (the `sdkconfig.defaults.16mb` settings, which IDF does
  **not** auto-apply beside a target overlay).
- **240 MHz** CPU ceiling (vs the C6's 160).
- USB-Serial/JTAG console (required for `RUVIEW_HELLO_V1` onboarding), CSI
  enabled, WPA3, LP-core, ESP-NOW time-sync (802.15.4 PHY available but kept off
  at runtime for the same reason as the C6 — see ADR-110 D1 / #762).
- `CONFIG_EDGE_DSP_SAMPLE_HZ=8` — **provisional**; the 240 MHz core should
  sustain more, but the qualifying number is MEASURED, not assumed (ADR-347).

### 2.2 Gate generalization (the mechanical core)

The C6 modules were gated `#if defined(CONFIG_IDF_TARGET_ESP32C6)`. Every gate
for a feature the C5 *also* has was widened to
`defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C5)` — 38
sites across 18 files: `c6_softap_he`, `c6_lp_core`, `c6_twt`, `c6_timesync`,
`thermal`, the `edge_processing`/`csi_collector` DSP defaults, and `main.c`
feature inits. Distinct-value sites got their own C5 branch (`serial_onboarding`
chip name → `"esp32c5"`, `main.c` target name → `"ESP32-C5"`, LED GPIO). The
Kconfig capability menu and `THERMAL_MONITOR`/`EDGE_DSP_SAMPLE_HZ` gates widened
to `IDF_TARGET_ESP32C6 || IDF_TARGET_ESP32C5`. `main/CMakeLists.txt` adds the
`ieee802154 ulp esp_hw_support` requirements and the LP-core `ulp_embed_binary`
step for C5 as for C6.

**Left C6-only:** `c6_antenna_select.c` + `CONFIG_C6_XIAO_ANTENNA_SELECT` — that
is the Seeed **XIAO** ESP32-C6 RF-switch, board-specific to that module, not our
WROOM-1 DevKitC.

**Deferred:** the mmWave companion (`mmwave_sensor.c` C5 pin map) — out of scope
for CSI bring-up; it is a separate board.

### 2.3 5 GHz

The host side already understands 5 GHz. Qualify the C5 on a **non-DFS** channel
(UNII-1: 36/40/44) — the firmware has **no DFS/radar-avoidance**, so UNII-2
(52–144) is out until that is added. Provision the C5 node onto a 5 GHz channel
to exercise the band that is its reason for being.

## 3. Consequences

### 3.1 Wins

- Buildable, flashable C5 CSI node with the full C6 pipeline — **no fork**.
- 16 MB → two 4 MB OTA slots: **73 % app headroom** (vs the C6's 45 % in 4 MB).
- First in-house chip that can source **5 GHz CSI**.

### 3.2 Costs / risks

- `esp32c5` is an IDF **preview** target on v5.5 — pinned to `--preview` and may
  shift as IDF stabilizes it.
- **HE-frame risk**: ADR-110 (issue #1005) records that true 256-bin HE-LTF CSI
  needs IDF **≥ 5.5.2** — the v5.4 blob silently downconverts HE→64-bin HT. We
  are on **v5.5.0**; this must be confirmed empirically at capture time, and IDF
  bumped to 5.5.2+ if the first HE frame comes back 64-bin.
- DSP cadence is provisional until measured on C5 silicon.
- C5-DevKitC-1 LED GPIO is a best-guess (27) pending schematic confirmation —
  cosmetic only.

### 3.3 Verification

| Gate | State | Evidence |
|---|---|---|
| `idf.py --preview set-target esp32c5` | **PASS** | `CONFIG_IDF_TARGET="esp32c5"` in sdkconfig (2026-10-04) |
| `idf.py build` (full) | **PASS** | `Project build complete`; `esp32-csi-node.bin` = **1,151,440 B**, 73 % free in 4 MB OTA slot |
| app image SHA-256 | recorded | `3577a9082d8de4703b8e0830ad70d65f65f6c04f9f696d74250574cd423a1281` |
| bootloader SHA-256 | recorded | `a523d2c877fe719e4f780a3f76ab740800187524fbf6daabe427320ee4c4ecf5` |
| flash + hash verify on silicon | **PASS** | `Wrote 1,152,000 B @ 0x20000 … Hash of data verified`; `--chip esp32c5` (2026-10-04) |
| boots + runs on C5 | **PASS** | serial: `ESP32-C5 CSI Node (ADR-018 / ADR-110) — v0.8.12 — Node ID: 1`; CSI collector + bounded serial onboarding up; 240 MHz; coredump partition live; **WiFi `band mode:0x3` (dual-band 2.4+5 GHz active)** (2026-10-04) |
| WiFi join + CSI capture on silicon | **PASS (partial)** | provisioned (NVS), joined WiFi (`Got IP 192.168.1.238`), auto-detected AP ch 5, promiscuous CSI up, **first CSI callback fired** (`CSI cb #1: len=106 rssi=-42 ch=5`) (2026-10-04) |
| continuous-run stability | **FIXED (needs re-verify)** | hit `CPU_LOCKUP` ~1 s in, immediately after `AP does not support setup individual TWT agreement` → **TWT on the C5 preview WiFi driver locks up against a non-iTWT AP**. Fixed: `CONFIG_C6_TWT_ENABLE=n` in the C5 overlay. Re-verify after a board power-cycle (preview silicon wedges in a ROM-stage `TG0_WDT` loop after many rapid flash/reset cycles). |
| physical CSI rate (≥ 20 pps raw) | **PENDING** | 5-min `:8032` poll once the TWT-off image runs stably (needs power-cycle) |
| DSP cadence (±1 Hz of configured) | **PENDING** | measure, then pin `CONFIG_EDGE_DSP_SAMPLE_HZ` |
| 5 GHz HE CSI frame (256-bin, PPDU 0x01) | **PENDING** | capture on UNII-1; confirms IDF 5.5.0-vs-5.5.2 HE path |

Provisioning note: `provision.py flash_nvs` needs `--no-stub` for the C5 preview
target (the flasher stub isn't available; without it the NVS write silently
fails MD5 verify and leaves the partition at 0xFF, which then ROM-loops). Fixed
in `provision.py` (ADR-368). Flash/reset state on the preview silicon is
fragile — a clean power-cycle recovers a wedged board.

## 4. Implementation phases

- **P1 — Build target (DONE, 2026-10-04):** overlay + gate generalization +
  CMake; builds, flashes, hash-verifies on C5 silicon.
- **P2 — Boot + onboarding (DONE, 2026-10-04):** image boots on C5 silicon,
  prints the `ESP32-C5 CSI Node` banner, brings up the CSI collector and bounded
  serial onboarding, runs at 240 MHz with the WiFi stack in dual-band mode
  (`band mode:0x3`). `RUVIEW_HELLO_V1` handshake over USB-JTAG still to exercise.
- **P3 — CSI capture on 2.4 GHz (in progress):** provisioned via `provision.py`
  (`--no-stub` fix); joined WiFi, CSI callback confirmed firing on C5. Blocked on
  continuous-run stability by the TWT lockup (now fixed: `C6_TWT_ENABLE=n`) — after
  a power-cycle, run 5 min and confirm raw yield ≥ 20 pps + stable DSP, then pin
  `CONFIG_EDGE_DSP_SAMPLE_HZ`.
- **P4 — 5 GHz dual-band CSI:** provision a UNII-1 channel (36/40/44); confirm HE
  frame is 256-bin (bump IDF to 5.5.2+ if it is 64-bin HT).
- **P5 — Validation record + evidence gate:** emit
  `docs/validation/<date>-esp32-c5-*.md` in the ADR-110 format (5-min dual-table
  physical result against the pass bar), run `ruview_claim_check` / `ruview_verify`,
  write the ADR-304 EvidenceRecord. Only then does this ADR move to **Accepted**.

## 5. Open questions

1. IDF 5.5.0 vs 5.5.2 for true HE CSI on C5 — resolve empirically at P4.
2. C5 sustainable DSP cadence — measure at P3.
3. C5-DevKitC-1 LED GPIO — confirm against schematic.
4. Does C5 802.15.4 RX behave differently from the C6's (which never delivered a
   frame, #762)? Re-test before ever enabling the 15.4 time-sync path on C5.
