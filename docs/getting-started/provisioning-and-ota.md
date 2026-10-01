# Provisioning and OTA

How to put Wi-Fi settings and a node identity onto an ESP32 CSI node, run several nodes together, pick the right firmware image, and update firmware over the air.

Everything here describes `main` at `b90b592d` (firmware 0.8.12). Evidence tags: **SRC** means read from source and not run; **HW** means confirmed by an `hw-validator` log. Statements tagged SRC are not hardware evidence.

> **Validation status.** Not yet run on stock hardware: a real provision-and-connect on an S3 and a C6, a real USB flash of each bundle, `POST /ota` with a key, and the check that re-provisioning erases a stored OTA key. The page states what they should do from source only. `GET /ota/status` was observed on live 0.8.12 nodes, but those were a local 16 MB build, not a stock image.

> **Known issues (as of `b90b592d`)**
>
> Over-the-air (OTA) update does not work end to end on `main`. Use a USB reflash until all three are fixed.
>
> | Gap | What happens | Tracking |
> |---|---|---|
> | `provision.py` cannot set the OTA key | The firmware tells you to run `provision.py --ota-psk <hex>`, but that flag does not exist. With no key in NVS, every `POST /ota` returns 403. | Issue [#1753](https://github.com/ruvnet/RuView/issues/1753). Not yet available; open PRs add it, for example [#1760](https://github.com/ruvnet/RuView/pull/1760). |
> | The OTA upload can overflow the httpd stack | The upload can crash the node in `esp_ota_end`. | Issue [#1893](https://github.com/ruvnet/RuView/issues/1893). Fix in PR [#1594](https://github.com/ruvnet/RuView/pull/1594). |
> | Nothing marks a new image valid | If rollback is enabled in the bootloader, the node reverts to the old image on the next reset. No released image enables rollback today (see [Images](#choose-a-firmware-image)), so this bites only custom builds. | No issue yet. A fix is pending. |
>
> When these land, update this box and the [OTA section](#ota-updates).

## Provision a node

Provisioning writes settings into the node's NVS partition over USB. It does not touch the app, so a firmware reflash that skips `0x9000` leaves the settings in place.

Install the tools once:

```bash
pip install 'esptool>=5.0' esp-idf-nvs-partition-gen
```

Then, from `firmware/esp32-csi-node/`:

```bash
python provision.py --port <port> \
  --ssid <ssid> --password <password> \
  --target-ip <host-ip> --node-id 1
```

- `<port>` is the serial port, such as `/dev/cu.usbmodem*` on macOS or `COMx` on Windows.
- `<host-ip>` is the machine running `sensing-server`. The node sends UDP frames to it on port 5005 unless you pass `--target-port`.
- `--chip` defaults to `auto`. Pass `--chip esp32c6` if auto-detect fails on a C6.
- `--dry-run` shows what would be written without flashing. It still saves the merged settings record and writes `nvs_provision.bin` into the current directory.

The first run for a board needs `--ssid`, `--password` and `--target-ip`, or `--force-partial` (deprecated) to skip them.

### Settings are additive

`provision.py` remembers what you gave it per serial port and merges new flags into that record. Running it again with only `--node-id 2` keeps the Wi-Fi settings from the first run.

| Flag | Use |
|---|---|
| `--state` | Print the merged settings that would be flashed, then exit. The output includes the Wi-Fi password in plain text, so do not paste it into issues. |
| `--reset` | Delete the saved record for this port and start fresh. Pass every key you want, including Wi-Fi: the record is deleted first, so `--reset --node-id 3` alone removes it and then exits with an error. |
| `--state-dir <dir>` | Keep the records somewhere other than the default. |

The record lives on the machine you run the script from, not on the node. A different laptop starts with no record.

**The record holds your Wi-Fi password in plain text, and on `main` the file is world-readable (mode 0644 under a default umask)** (issue [#1754](https://github.com/ruvnet/RuView/issues/1754)). Do not share it, commit it, or leave it on a shared machine. Delete it with `--reset` when you are done, or point `--state-dir` at a private directory.

### What gets written

The script builds an NVS image and writes it at `0x9000`, size `0x6000`. The settings live in the `csi_cfg` namespace (SRC: `provision.py:178-238`, `main/nvs_config.c`): Wi-Fi, target address and port, node id, TDM slot, channel and hopping, MAC filter, edge-processing tuning, and swarm/seed options. The full flag and key table stays next to the script in the [firmware README](../../firmware/esp32-csi-node/README.md) so it cannot drift from the code. `python provision.py --help` is the authoritative list.

Firmware defaults when NVS is empty: node id 1, target `192.168.1.100:5005`, SSID `wifi-densepose`.

Two things the script cannot write today: the OTA key (`security/ota_psk`; `--ota-psk` is not yet available, PR [#1760](https://github.com/ruvnet/RuView/pull/1760)) and the WASM signing key (`wasm_pubkey`).

**Re-provisioning replaces the whole NVS partition (SRC).** Because the script writes a complete image at `0x9000`, anything in other namespaces, including a previously set OTA key, should be lost. Provision everything in one pass. `hw-validator` should confirm this.

### Serial onboarding

Firmware 0.8.12 also accepts `ssid`, `password`, `target_ip`, `target_port` and `node_id` over the serial port, using the protocol the Mac app speaks (SRC: `main/serial_onboarding.c`). It writes the same `csi_cfg` keys. `provision.py` is the scripted route.

## Run more than one node

Give every node its own `--node-id`, and tell the server where each one is.

```bash
python provision.py --port <port-a> --ssid <ssid> --password <password> \
  --target-ip <host-ip> --node-id 11 --tdm-slot 0 --tdm-total 3
python provision.py --port <port-b> --ssid <ssid> --password <password> \
  --target-ip <host-ip> --node-id 12 --tdm-slot 1 --tdm-total 3
python provision.py --port <port-c> --ssid <ssid> --password <password> \
  --target-ip <host-ip> --node-id 13 --tdm-slot 2 --tdm-total 3
```

- **Node ids** must be unique. If two nodes share the default id 1, the server sees one node (SRC).
- **TDM slots** (ADR-029) are 0-based and must be below `--tdm-total`. `provision.py` rejects a slot at or above the total, and requires both flags together. The firmware clamps an out-of-range slot to 0 at boot.
- **In firmware 0.8.12 the TDM slot has no effect on air timing.** The values are stored and range-checked, and nothing else in `main/` reads them. Setting them is harmless and prepares for later firmware, but do not expect staggered transmit times (source check by `hw-validator`).

### Tell the server where the nodes are

The server fuses nodes using their positions in metres. Pass them with `--node-positions` (or `SENSING_NODE_POSITIONS`):

```bash
sensing-server --source esp32 --udp-bind 0.0.0.0 --udp-allow <node-subnet> \
  --node-positions "11:0,0,2.5;12:4,0,2.5;13:2,3,2.5"
```

Format: `node_id:x,y,z` entries separated by `;` (SRC: `field_bridge.rs:356-396`; the `--help` text shows only the plain `x,y,z` form).

- Always use the `node_id:` prefix. Without it, an entry takes its list position as its id, so the first entry applies to node 0, which is wrong for ids like 11, 12, 13.
- **Default position warning.** A node with no matching entry is reported at `[2.0, 0.0, 1.5]` (SRC: `main.rs:9939`). Every unlisted node lands on that same point, which makes positions look plausible and wrong. If you see several nodes at the same coordinates, the node ids in `--node-positions` do not match the ids you provisioned.
- A malformed entry is skipped with a log warning, not an error.
- The boot line `Configured N node positions` counts parsed entries, not matches with live nodes. To check, read `nodes[].position` in `GET /api/v1/sensing/latest`.
- Without the prefix, entries key by list index (0, 1, 2), so node 1 gets the second entry.

The `--udp-bind` and `--udp-allow` flags matter too: by default the server listens on loopback only, so a node on the LAN is ignored. See the quickstart for the full network setup.

## Choose a firmware image

CI builds three images, published as `esp32-csi-node-firmware-<variant>.tar.gz` on `v*-esp32` tags (SRC: `.github/workflows/firmware-ci.yml:49-77`).

| Variant | Chip | Flash | Partition table | App slot | Use for |
|---|---|---|---|---|---|
| `8mb` | ESP32-S3 | 8 MB | `partitions_display.csv` | 2 MiB x 2 | Most S3 boards. |
| `4mb` | ESP32-S3 | 4 MB | `partitions_4mb.csv` | 0x1D0000 x 2 | S3 boards with 4 MB flash. |
| `c6-4mb` | ESP32-C6 | 4 MB | `partitions_4mb.csv` | 0x1D0000 x 2 | C6 boards. |

Match the image to the flash size on your board. The `8mb` image is unverified on 16 MB boards. An 8 MB layout does not fit a 4 MB chip.

**There is no 16 MB image.** `sdkconfig.defaults.16mb` and `partitions_16mb.csv` exist in the repo but CI never builds them and no release contains them. That config is also the only place `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is set, so rollback is off in every released image. `firmware/esp32-csi-node/RUNBOOK.md` describes a 16 MB fleet. Treat that as unreleased: you would have to build it yourself.

Which version you get:

- **Stable release (the primary path):** `v0.8.8-esp32`, the latest. It predates the bundle names above and ships `esp32-csi-node-v0.8.8-<variant>-flash-bundle.zip`, with variants `s3-8mb`, `s3-4mb` and `c6-4mb`.
- **`main`:** firmware 0.8.12, unreleased. To run it, build from source (see the firmware README).
- **`v0.8.13-esp32`:** an opt-in pre-release built from unmerged PR #2050. It is not on `main`.

This page describes `main` (0.8.12) behaviour. Where it differs from v0.8.8, you will see the difference only if you flash the stable zip.

### Flash over USB

Offsets are the same for all three images (SRC: `partitions_*.csv`, firmware README):

```bash
python -m esptool --chip <esp32s3|esp32c6> -b 460800 \
  --before default_reset --after hard_reset write_flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0xf000  ota_data_initial.bin \
  0x20000 esp32-csi-node.bin
```

The list leaves out `0x9000`, so provisioned settings survive. The v0.8.8 zips use these exact inner names for all three variants, each with its own `FLASHING.md`. The CI `.tar.gz` bundles name the 4 MB and C6 files with suffixes (`esp32-csi-node-4mb.bin`, `partition-table-4mb.bin`, `esp32-csi-node-c6.bin`, `partition-table-c6.bin`), so use the names in your bundle. Newer `esptool` versions warn that `write_flash` is deprecated in favour of `write-flash`; both work.

## OTA updates

> OTA is unusable on a stock-provisioned node today. Read the Known issues box first. Use USB reflash.

For reference, this is how OTA is meant to work (SRC: `main/ota_update.c`).

- The node runs an HTTP server on port **8032**.
- `GET /ota/status` needs no authentication. It returns the firmware version, the running and next partition, and the maximum image size.
- `POST /ota` takes the raw app binary and needs `Authorization: Bearer <ota-psk-hex>`. The key is compared to NVS `security/ota_psk` in constant time.
- **Fail closed:** if no key is stored, every `POST /ota` returns 403. A fresh node therefore rejects all OTA, by design.

```bash
curl http://<node-ip>:8032/ota/status

curl -X POST http://<node-ip>:8032/ota \
  -H "Authorization: Bearer <ota-psk-hex>" \
  --data-binary @esp32-csi-node.bin
```

The key is up to 64 hex characters. Keep it out of shell history and out of the repo. Until `provision.py` can set it (#1753), the only way to store one is to write the `security/ota_psk` NVS entry yourself, which this guide does not cover.

What OTA can reach: only the app partition (`ota_0`/`ota_1`) and `otadata`. A bootloader change, a partition-table change, or a move to a different flash layout needs USB on every board. The WASM endpoints on the same port (`/wasm/*`) are signature-checked and are not covered here.

Roll out to one node first, let it run, then the rest. For the contributor fleet procedure, including a table of what OTA can and cannot reach, see [RUNBOOK §2-3](../../firmware/esp32-csi-node/RUNBOOK.md). Note that the RUNBOOK describes a private 16 MB build and names a rollback function that does not exist on `main`.
