# ESP-NOW illuminator CSI spike: bench procedure

Go/no-go hardware spike for the "illuminator" design, where a dedicated node
transmits known frames and the receivers take CSI from them. It answers three
questions that nothing has measured on real silicon yet:

1. Does an ESP32-S3 running RuView firmware produce a CSI callback for an
   ESP-NOW broadcast from an ESP32-C6 **at a forced OFDM rate** (6 Mbps 11g,
   HT20 MCS0), while it is STA-associated to an HT40 AP on channel 5?
2. Does it produce CSI for the same frames at **1 Mbps DSSS** (the ESP-NOW
   default)? Espressif's CSI documentation says CSI comes only from OFDM
   training fields, so the expected answer is no. That is CLAIMED, not
   measured.
3. Is the frame body reachable through `wifi_csi_info_t::payload`, so a
   receiver can read the sender's counter?

Every number this procedure produces is MEASURED only once it comes from a
captured serial log of real boards. A clean build is not evidence.

## What the spike firmware does

Both options below default to `n`. A normal build compiles none of this code
(`main/CMakeLists.txt` adds `espnow_illum.c` only when either option is set).

| Kconfig | Overlay | Role |
|---|---|---|
| `CONFIG_ESPNOW_ILLUM_TX` | `sdkconfig.defaults.espnow-spike-tx` | C6 sender |
| `CONFIG_ESPNOW_ILLUM_RATE_{CYCLE,11B_1M,11G_6M,HT20_MCS0}` | tx overlay: `CYCLE` | forced PHY rate |
| `CONFIG_ESPNOW_ILLUM_CYCLE_S` | tx overlay: `10` | seconds per rate when cycling |
| `CONFIG_ESPNOW_ILLUM_HZ` | tx overlay: `50` | send rate, paced by `esp_timer` |
| `CONFIG_ESPNOW_CSI_DIAG` | `sdkconfig.defaults.espnow-spike-rx` | S3 receive diagnostic |
| `CONFIG_ESPNOW_CSI_DIAG_SENDER_MAC` | rx overlay: `02:00:00:00:00:02` | C6 node 5 STA MAC |

**Sender.** Broadcasts a 20-byte payload (`main/espnow_illum_proto.h`: magic
`ILUM`, rate id, node id, u32 counter, u64 mesh epoch) 50 times a second
through the ESP-NOW instance that `c6_sync_espnow` already owns. In cycle mode
the rate rotates every 10 s: 1M DSSS, then 6M 11g, then HT20 MCS0. The rate id
travels in the payload. Once a second it logs:

```
ILLUM_TX t=23s rate=11g-6M cfg=ESP_OK ctr=1150 sent=50 send_err=0 cb_ok=50 cb_fail=0 cb_other=10 cb_rate=0x0b
```

`cfg` is the result of `esp_now_set_peer_rate_config()`. `cb_rate` is the rate
the driver reported in the send callback for our frames. Expect `0x00` for 1M,
`0x0b` for 6M and `0x10` for MCS0. `cb_other` counts the sync beacons.

The rate is set on the shared broadcast peer, so the 10 Hz sync beacons change
rate with it while the spike runs.

**Receiver.** Every CSI callback is inspected before the 50 Hz gate and the
ADR-060 MAC filter can drop it. Once a second it logs three lines:

```
ILLUM_RX csi=.. mac=.. illum=.. illum_othermac=.. sync=.. other=.. pl_null=.. len63=.. mac_phy[dsss/ofdm/ht/other]=a/b/c/d gate_won=.. gate_lost=.. filt_drop=..
ILLUM_RX rid[unset/1M/6M/mcs0]=a/b/c/d phy[dsss/ofdm/ht/other]=a/b/c/d rssi=min/mean/max rate=0x.. mode=.. mcs=.. cwb=.. csi_len=.. pl_len=.. off=..
ILLUM_RX cum ctr_last=.. ctr[+1/miss/dup/reord]=a/b/c/d rxseq[+1/miss/dup/reord]=a/b/c/d
```

| field | meaning |
|---|---|
| `csi` | all CSI callbacks this second, any source |
| `mac` | callbacks whose `info->mac` is the sender MAC |
| `illum` | callbacks whose payload decoded as an illuminator frame (any MAC) |
| `illum_othermac` | decoded, but `info->mac` is not the configured sender (wrong MAC in the overlay) |
| `sync` / `other` | sender-MAC callbacks carrying a sync beacon / neither magic (its data frames to the AP, which also produce CSI) |
| `pl_null` | sender-MAC callbacks where `payload` was NULL or empty |
| `len63` | sender-MAC callbacks with `rx_ctrl.sig_len == 63`, the expected illuminator MPDU length. This fingerprint still works if `payload` is unavailable |
| `mac_phy[...]` | sender-MAC callbacks by PHY class |
| `rid[...]` | decoded illuminator frames by the rate the sender had set |
| `phy[...]` | decoded illuminator frames by the PHY class the receiver observed |
| `gate_won` / `gate_lost` / `filt_drop` | decoded frames the 50 Hz gate kept / dropped / kept but the NVS `filter_mac` then dropped |
| `ctr[...]` | continuity of the decoded payload counter since boot. `miss` is real loss |
| `rxseq[...]` | continuity of `rx_seq` over decoded frames. The sync beacons share the sequence space, so expect about one `miss` per five frames. Not a loss metric |

The first decoded frame (or failing that, the first sender-MAC frame with a
payload) is also hex-dumped once as `ILLUM_RX sample ...` to show where the
body sits inside `payload`.

The ADR-018 stream is unchanged. Both boards keep streaming to the sensing
server as usual and reboot once when flashed.

## Build (already done, and how to redo it)

Built with local ESP-IDF v5.5, out of tree. `OUT` below is where the images
live; `/private/tmp` does not survive a reboot, so rebuild if it is gone.

```bash
source ~/esp/esp-idf/export.sh
P=<repo>/firmware/esp32-csi-node
OUT=<scratch>
printf 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n' > "$OUT/rollback.defaults"

# S3 receiver (16 MB layout)
D="$P/sdkconfig.defaults;$P/sdkconfig.defaults.16mb;$P/sdkconfig.defaults.espnow-spike-rx"
idf.py -C "$P" -B "$OUT/build-s3-rx" -D SDKCONFIG="$OUT/sdkconfig.s3-rx" -D SDKCONFIG_DEFAULTS="$D" set-target esp32s3
idf.py -C "$P" -B "$OUT/build-s3-rx" -D SDKCONFIG="$OUT/sdkconfig.s3-rx" -D SDKCONFIG_DEFAULTS="$D" build

# C6 sender (4 MB layout, matching node 5's v0.8.12 image; sdkconfig.defaults.esp32c6 is picked up automatically)
D="$P/sdkconfig.defaults;$OUT/rollback.defaults;$P/sdkconfig.defaults.espnow-spike-tx"
idf.py -C "$P" -B "$OUT/build-c6-tx" -D SDKCONFIG="$OUT/sdkconfig.c6-tx" -D SDKCONFIG_DEFAULTS="$D" set-target esp32c6
idf.py -C "$P" -B "$OUT/build-c6-tx" -D SDKCONFIG="$OUT/sdkconfig.c6-tx" -D SDKCONFIG_DEFAULTS="$D" build
```

Check before flashing. Both must show the spike option, rollback, and the
expected layout:

```bash
grep -E "^CONFIG_(IDF_TARGET|ESPTOOLPY_FLASHSIZE|PARTITION_TABLE_CUSTOM_FILENAME|BOOTLOADER_APP_ROLLBACK_ENABLE|ESPNOW_[A-Z_]+)=" \
  "$OUT/sdkconfig.s3-rx" "$OUT/sdkconfig.c6-tx"
```

| | S3 (`sdkconfig.s3-rx`) | C6 (`sdkconfig.c6-tx`) |
|---|---|---|
| `IDF_TARGET` | `"esp32s3"` | `"esp32c6"` |
| `ESPTOOLPY_FLASHSIZE` | `"16MB"` | `"4MB"` |
| `PARTITION_TABLE_CUSTOM_FILENAME` | `"partitions_16mb.csv"` | `"partitions_4mb.csv"` |
| `BOOTLOADER_APP_ROLLBACK_ENABLE` | `y` | `y` |
| spike | `ESPNOW_CSI_DIAG=y`, sender MAC set | `ESPNOW_ILLUM_TX=y`, `RATE_CYCLE=y`, `HZ=50` |

## Bench procedure (the part a person does)

Do **not** touch `/dev/cu.usbserial-02E47B28` (the LD6002 radar), the sensing
server, or any other node. Every command below names a `/dev/cu.usbmodem*`
port explicitly.

### 1. Connect and identify

Unplug the C6 (node 5) and one S3 (any of nodes 1-4) from their wall power
and connect both to the Mac by USB (native USB port on each board). Then:

```bash
source ~/esp/esp-idf/export.sh
OUT=<scratch>
ls /dev/cu.usbmodem*
for p in /dev/cu.usbmodem*; do echo "== $p"; esptool.py --port "$p" chip_id 2>&1 | grep -E "Chip is|MAC:"; done
```

`chip_id` resets each board, which is harmless here. Set the two ports from
the output:

```bash
C6=/dev/cu.usbmodemXXXX   # "Chip is ESP32-C6", MAC 02:00:00:00:00:02
S3=/dev/cu.usbmodemYYYY   # "Chip is ESP32-S3"
```

If the C6 MAC is **not** `02:00:00:00:00:02`, carry on anyway: the
diagnostic also matches on the payload magic, and the wrong MAC will show as
`illum_othermac > 0` with `mac = 0`. Note the real MAC in the results.

### 2. Back up each board (recommended; this is the exact revert)

A full read captures the bootloader, the partition table, both app slots,
`otadata` (which slot is live) and NVS. The backup files contain the Wi-Fi
credentials from NVS: keep them in `$OUT`, never in the repo, and delete them
once the boards are restored.

```bash
esptool.py --chip esp32c6 --port "$C6" -b 921600 read_flash 0 0x800000  "$OUT/backup-c6-node5.bin"   # 8 MB part
esptool.py --chip esp32s3 --port "$S3" -b 921600 read_flash 0 0x1000000 "$OUT/backup-s3.bin"         # 16 MB part
```

### 3. Confirm the partition tables match (stop if they do not)

The spike images write their own partition table. It must be identical to the
one on the board, or NVS could move and provisioning would be lost.

```bash
esptool.py --chip esp32c6 --port "$C6" read_flash 0x8000 0xC00 "$OUT/pt-c6-onboard.bin"
esptool.py --chip esp32s3 --port "$S3" read_flash 0x8000 0xC00 "$OUT/pt-s3-onboard.bin"
cmp "$OUT/build-c6-tx/partition_table/partition-table.bin" "$OUT/pt-c6-onboard.bin" && echo C6 PT OK
cmp "$OUT/build-s3-rx/partition_table/partition-table.bin" "$OUT/pt-s3-onboard.bin" && echo S3 PT OK
```

Both must print `PT OK`. If either differs, stop and report it. Do not flash.
(`cmp` compares the table's MD5 trailer too, so equal output means an identical
table.)

### 4. Flash the spike images (NVS at 0x9000 is not written)

`flash_args` lists only the bootloader (0x0), the partition table (0x8000),
`otadata` (0xf000) and the app (0x20000). NVS is not in the list, so SSID,
password, node id and target IP survive. The fresh `otadata` boots `ota_0`.

```bash
(cd "$OUT/build-c6-tx" && esptool.py --chip esp32c6 --port "$C6" -b 460800 \
   --before default_reset --after hard_reset write_flash @flash_args)
(cd "$OUT/build-s3-rx" && esptool.py --chip esp32s3 --port "$S3" -b 460800 \
   --before default_reset --after hard_reset write_flash @flash_args)
```

Both must end with `Hash of data verified.` for every region.

### 5. Capture serial logs

Leave both boards on USB, where they are powered and close together. Wait
about 30 s after flashing so both have joined the AP and started ESP-NOW. Then
capture 90 s from **both boards at the same time**: the S3 log is the result,
and the C6 log shows which rate was on air during each second. Opening the
port this way does not reset the board.

```bash
cap() { python - "$1" "$2" 90 <<'EOF'
import serial, sys, time
port, out, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.5
s.dtr = False; s.rts = False          # do not reset the board on open
s.open(); end = time.time() + secs
with open(out, "wb") as f:
    while time.time() < end:
        f.write(s.read(4096))
EOF
}
cap "$C6" "$OUT/spike-c6-tx.log" & cap "$S3" "$OUT/spike-s3-rx.log" & wait
grep -c "ILLUM_TX t=" "$OUT/spike-c6-tx.log"; grep -c "ILLUM_RX rid" "$OUT/spike-s3-rx.log"
```

Both counts should be around 90. If a count is 0, check that the board is
associated (look for `CSI streaming active` or `ILLUM_* start` in the log; the
`start` line appears only after the first boot's Wi-Fi join) and capture again.
Do not re-flash.

### 6. Summarise

```bash
python - "$OUT/spike-c6-tx.log" "$OUT/spike-s3-rx.log" <<'EOF'
import re, sys
tx, rx = open(sys.argv[1], errors="replace").read(), open(sys.argv[2], errors="replace").read()
secs, sent, cfg, cbr = {}, {}, {}, {}
for m in re.finditer(r"ILLUM_TX t=\d+s rate=(\S+) cfg=(\S+) ctr=\d+ sent=(\d+) send_err=(\d+) cb_ok=(\d+) cb_fail=(\d+) cb_other=\d+ cb_rate=(\S+)", tx):
    r = m[1]; secs[r] = secs.get(r, 0) + 1; sent[r] = sent.get(r, 0) + int(m[3])
    cfg.setdefault(r, set()).add(m[2]); cbr.setdefault(r, set()).add(m[7])
rid = [0, 0, 0, 0]; lines = 0
for m in re.finditer(r"rid\[unset/1M/6M/mcs0\]=(\d+)/(\d+)/(\d+)/(\d+)", rx):
    lines += 1; rid = [a + int(b) for a, b in zip(rid, m.groups())]
names = {"11b-1M": 1, "11g-6M": 2, "ht20-mcs0": 3}
print(f"rx lines: {lines}")
for r, i in names.items():
    s = sent.get(r, 0)
    print(f"{r:10s} tx_secs={secs.get(r,0):3d} sent={s:5d} rx_csi={rid[i]:5d} "
          f"yield={rid[i]/s if s else float('nan'):.2f} cfg={sorted(cfg.get(r,[]))} cb_rate={sorted(cbr.get(r,[]))}")
last = re.findall(r"ILLUM_RX cum .*", rx)
print(last[-1] if last else "no cumulative line")
for k in ("pl_null", "len63", "illum_othermac", "gate_won", "gate_lost", "filt_drop"):
    print(k, sum(int(x) for x in re.findall(rf"\b{k}=(\d+)", rx)))
print("\n".join(re.findall(r"ILLUM_RX sample.*", rx)[:2]))
EOF
```

The two captures are not time-aligned to the second, so the rate at a cycle
boundary can be misattributed for a second. Over 90 s this is well under 10 %
and does not move any verdict below.

## Pass / fail

Judge each rate separately. `yield` = CSI callbacks decoded at the S3 for that
rate divided by frames the C6 sent at that rate.

**Validity first (C6 log).** For each rate, `cfg` must be `ESP_OK` and
`cb_rate` must match the requested rate (`0x00` 1M, `0x0b` 6M, `0x10` MCS0),
with `send_err` near 0. If not, the forced rate never reached the air. Report
that rate as **INVALID**, not FAIL.

| Question | PASS | MARGINAL | FAIL |
|---|---|---|---|
| Q1 CSI from forced OFDM (6M, MCS0) | yield >= 0.8, i.e. >= 40 CSI/s sustained, with no 1 s line in that rate's steady windows below 30 | 0.2 to 0.8 | < 0.2 |
| Q2 payload / counter readable | `illum > 0`, `ctr` dup = reorder = 0, `ctr_last` advances every second, one constant `off` | decoded but `off` varies or `pl_null > 0` for some frames | `illum = 0` while `len63 > 0` (CSI arrives, body does not) |
| DSSS 1M (reported, not gated) | expected yield ~0 (CLAIMED by Espressif). A non-zero result is a finding; note the `phy` class | | |

If `illum = 0` everywhere, Q1 can still be answered from the fingerprint:
`len63` and `mac_phy[ofdm]` / `mac_phy[ht]` should rise by about 50/s during
the 6M / MCS0 windows compared with the 1M windows. Report that as Q1 via
fingerprint, Q2 FAIL.

**Verdict.**
- **GO**: Q1 PASS for at least one OFDM rate, and Q2 PASS.
- **GO, degraded**: Q1 PASS, Q2 FAIL. Attribution then needs MAC plus length,
  with no counter or epoch, and that changes the design.
- **NO-GO**: Q1 FAIL at both OFDM rates while valid.

Also record, as design inputs rather than pass criteria: RSSI, `cwb` (20 vs
40 MHz CSI), `csi_len`, and `gate_won / illum`. The last is how often an
illuminator frame wins the 50 Hz gate slot against ambient traffic. The bench
geometry (boards next to each other on USB) says nothing about range. Tag any
number you quote as MEASURED with the log file as its reproducer.

## Restore normal firmware

**Option A: exact revert from the step 2 backup (preferred).**

```bash
esptool.py --chip esp32c6 --port "$C6" -b 460800 write_flash 0 "$OUT/backup-c6-node5.bin"
esptool.py --chip esp32s3 --port "$S3" -b 460800 write_flash 0 "$OUT/backup-s3.bin"
```

This restores everything byte for byte, including which OTA slot was live.
Delete the backup files once both nodes are back on the server.

**Option B: re-flash the normal v0.8.12 build** (if there is no backup). These
are the builds the fleet was last flashed from:

```bash
N=<scratch>
(cd "$N/build-c6" && esptool.py --chip esp32c6 --port "$C6" -b 460800 --before default_reset --after hard_reset write_flash @flash_args)
(cd "$N/build-s3" && esptool.py --chip esp32s3 --port "$S3" -b 460800 --before default_reset --after hard_reset write_flash @flash_args)
```

If those directories are gone, rebuild them from the main checkout (the
branch the fleet was built from) with the same `idf.py` lines as above, but
without the spike overlay:

```bash
source ~/esp/esp-idf/export.sh
M=<repo>/firmware/esp32-csi-node
printf 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n' > "$N/rollback.defaults"
D="$M/sdkconfig.defaults;$M/sdkconfig.defaults.16mb"
idf.py -C "$M" -B "$N/build-s3" -D SDKCONFIG="$N/sdkconfig.s3" -D SDKCONFIG_DEFAULTS="$D" set-target esp32s3
idf.py -C "$M" -B "$N/build-s3" -D SDKCONFIG="$N/sdkconfig.s3" -D SDKCONFIG_DEFAULTS="$D" build
D="$M/sdkconfig.defaults;$N/rollback.defaults"
idf.py -C "$M" -B "$N/build-c6" -D SDKCONFIG="$N/sdkconfig.c6" -D SDKCONFIG_DEFAULTS="$D" set-target esp32c6
idf.py -C "$M" -B "$N/build-c6" -D SDKCONFIG="$N/sdkconfig.c6" -D SDKCONFIG_DEFAULTS="$D" build
```

Then run the step 4 `write_flash @flash_args` lines from those directories.
Either way, NVS is untouched, so both boards rejoin the AP with their existing
node ids. Put the boards back on wall power and confirm both reappear on the
sensing server.
