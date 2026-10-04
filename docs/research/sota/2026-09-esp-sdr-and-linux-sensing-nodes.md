# ESP-SDR raw I/Q and small Linux sensing nodes

Date: 2026-09-30. Status: research note, nothing built. Tags: [V] read in the cited source; [I] inference.

Related: [ADR-042](../../adr/ADR-042-coherent-human-channel-imaging.md) (ESPARGOS phase coherence),
[tools/bfi](../../../tools/bfi/README.md) (Linux BFI capture), and
[2026-Q2-rf-sensing-and-edge-rust.md](2026-Q2-rf-sensing-and-edge-rust.md) section 1.3 ("Nexmon-on-Pi is not obviously a win").

## ESP-SDR

ESPARGOS released ESP-SDR on 2026-09-28. It is firmware that reads **raw I/Q samples** from the Wi-Fi receiver of ordinary ESP32-family chips, through an undocumented debug path that bypasses the Wi-Fi modem. ([project page](https://espargos.net/espsdr/), [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr)) [V]

- **Chips:** ESP32, C3, C5, C6, C61, S2, **S3**, S31. RuView's ESP32-S3 and C6 nodes are both on the list. [V]
- **Rates and format:** 80/40/20/16/10/8/4 MS/s; signed 8-bit (`CAP16`) or packed 10-bit (`CAP20`) I/Q; CRC32 on every burst. [V]
- **Frequency range:** 2.2-2.7 GHz on every chip; 4.8-6.0 GHz on the C5 only. The README says each chip *accepts tuning* from 100 to 6000 MHz; the project page gives the useful range above. [V]
- **Burst capture only.** The modem writes 2,560 Mbit/s into SRAM, but the output links are UART (3 Mbit/s), SPI (about 13 Mbit/s), USB (480 Mbit/s, S31 only) and GbE. Continuous streaming, SoapyESPSDR on the S31 at 8 and 16 MS/s, is still under development. [V]
- **ESPARGOS One**, the 8-antenna C61 array, now captures phase-coherent I/Q with a trigger. That lets it localize any 2.4 GHz ISM emitter (BLE, Zigbee, Wi-Fi), not just Wi-Fi CSI. [V]
- **No licence yet.** `esp-sdr` and `esp-web-sdr` have no licence file; `pyespargos` is LGPL-3.0. Until ESP-SDR is licensed, RuView can study and benchmark against it but should not vendor it. [V for the licence state; I for the consequence]

### What it could add to RuView [I]

- **Raw capture beside CSI.** Vendor CSI gives one estimate per received frame. I/Q allows RuView's own estimators (CFO/SFO correction, super-resolution delay), measurement between frames, and non-Wi-Fi emitters, including ESP-NOW sounding frames and BLE.
- **Better calibration and surveys.** An I/Q burst from an S3 next to a CSI node could characterize interference and AGC behaviour. That supports ADR-366-style frozen controls without changing the CSI firmware.
- **Not a replacement for the CSI stream.** At a low duty cycle it doesn't fit continuous pose or vitals. It is also an undocumented silicon path that an ESP-IDF update could break.

## Small Linux boards as nodes (example: Banana Pi BPI-M4 Zero)

The BPI-M4 Zero is a Pi-Zero-sized Allwinner H618 board (4x A53, 1.5 GHz) with 2-4 GB RAM, 8-32 GB eMMC, 2.4/5 GHz Wi-Fi, and 100 Mbit Ethernet over an FPC adapter. It costs from $24.50. ([wiki](https://wiki.banana-pi.org/Banana_Pi_BPI-M4_Zero), [CNX](https://www.cnx-software.com/2023/12/12/banana-pi-bpi-m4-zero-allwinner-h618-sbc-raspberry-pi-zero-2-w/)) [V]

- **The Wi-Fi chip varies by revision.** Armbian users report newer boards on SDIO **CYW43455** and older boards on Realtek over USB, with Wi-Fi problems on 6.x kernels. ([Armbian forum](https://forum.armbian.com/topic/51743-banana-pi-bpi-m4-zero-standard-support/)) [V, community reports]
- **CSI through Nexmon** needs the `bcm43455c0` with firmware 7_45_189, which is officially supported only on Raspberry Pi 3B+/4B/5. ([nexmon_csi](https://github.com/seemoo-lab/nexmon_csi)) [V] Porting it to an Allwinner board under Armbian is unproven. [I]
- **BFI capture is a better fit.** `tools/bfi` sniffs beamforming feedback in monitor mode and needs no firmware patch. Any Linux node whose Wi-Fi supports monitor mode on 5 GHz is a candidate, so either M4 Zero variant could work, depending on driver monitor-mode support. [I; not tested]
- **As an aggregator.** It could run `wifi-densepose-sensing-server` near a group of ESP32-S3 nodes, or host an ESP-SDR device over USB, with Ethernet carrying the uplink so the radio stays free. [I]
- **Not a replacement for the ESP32-S3 CSI node.** Section 1.3 of the Q2 review still holds: on cost, secure boot and provisioning, the ESP32-S3 mesh wins. [V]
- **A Pi Zero 2 W can't do Nexmon CSI.** Its 43430/43436-family chip is not on nexmon_csi's supported list. [I from that list]

## Suggested order

1. Flash ESP-SDR onto an existing S3 or C5, capture a 2.4 GHz survey next to a CSI node, and record burst length and capture rate.
2. Run `tools/bfi` monitor-mode capture on a small Linux board. Try a Pi 5 first; its Nexmon support is official.
3. Consider a BPI-M4 Zero, CYW43455 revision, only as a cheap aggregator or BFI node after steps 1 and 2.
