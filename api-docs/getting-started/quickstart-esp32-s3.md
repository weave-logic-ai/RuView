# Quickstart: ESP32-S3 to a live dashboard

This page takes you from one ESP32-S3 board to a dashboard showing live Wi-Fi
CSI frames. It uses the native `sensing-server` on macOS or Linux. If you
prefer containers, use [docker.md](docker.md) instead.

Not everything here was tested on a device; see
[Tested and not tested](#tested-and-not-tested) at the end.

Read [whats-real.md](whats-real.md) before you trust any number the dashboard
shows. A working dashboard means frames are arriving. It does not mean
camera-grade sensing.

## What you need

- One ESP32-S3 board with a USB data cable.
- A 2.4 GHz Wi-Fi network the board and your computer share. The ESP32-S3
  radio has no 5 GHz support (see the Espressif ESP32-S3 datasheet).
- A checkout of this repository, Rust (the repo pins 1.89 in
  `v2/rust-toolchain.toml`, and `rustup` picks it up), and Python 3.
- `pip install 'esptool>=5.0' esp-idf-nvs-partition-gen`

Placeholders used below: `<your-ssid>`, `<your-password>`, `<host-ip>` (your
computer's address on the Wi-Fi network), `<port>` (the board's serial
device). Replace them with your own values. Never paste real credentials into
an issue or a commit.

## 1. Get the firmware

Download the bundle for your chip and flash size from the
[v0.8.8 ESP32 release](https://github.com/ruvnet/RuView/releases/tag/v0.8.8-esp32).
This is the latest stable release (as of 2026-10-01). The `main` branch is at
0.8.12, which is unreleased.

| Board | Bundle |
|---|---|
| ESP32-S3, 8 MB flash | `esp32-csi-node-v0.8.8-s3-8mb-flash-bundle.zip` |
| ESP32-S3, 4 MB flash | `esp32-csi-node-v0.8.8-s3-4mb-flash-bundle.zip` |

Download `SHA256SUMS.txt` from the same release page and check the file you
downloaded. On macOS:

```bash
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

On Linux:

```bash
sha256sum -c SHA256SUMS.txt --ignore-missing
```

Unzip it. Never flash an S3 bundle onto a different chip. Bundles exist only
for 8 MB and 4 MB boards; for other flash sizes see
[Choose a firmware image](provisioning-and-ota.md#choose-a-firmware-image).

<details>
<summary>Alternative: build 0.8.12 from source (main)</summary>

From the repository root, with Docker running:

```bash
docker run --rm \
  -v "$(pwd)/firmware/esp32-csi-node:/project" -w /project \
  espressif/idf:v5.4 bash -c \
  "rm -rf build sdkconfig && idf.py set-target esp32s3 && idf.py build"
```

Then flash the files from `firmware/esp32-csi-node/build/` at the same four
offsets as step 2 (`build/bootloader/bootloader.bin`,
`build/partition_table/partition-table.bin`, `build/ota_data_initial.bin`,
`build/esp32-csi-node.bin`).

Boards with no display (for example ESP32-S3-DevKitC-1) need the
`sdkconfig.defaults.devkitc` overlay. The default build can drop CSI yield to
zero on them. Follow the header of that file in
`firmware/esp32-csi-node/` for the exact build command.
</details>

<details>
<summary>Note: release naming changes after 0.8.8, and a pre-release preview</summary>

Releases after 0.8.8 are published by CI as one
`esp32-csi-node-firmware-<variant>.tar.gz` per variant (`8mb`, `4mb`,
`c6-4mb`) with a top-level `SHA256SUMS.txt`, instead of the zip names above.
The file names inside the bundle also differ (for example
`esp32-csi-node-4mb.bin` in the 4 MB variant).
`v0.8.13-esp32` is a pre-release built from unmerged PR #2050 and uses that
scheme. It is an opt-in preview, not what this page tests.
</details>

## 2. Flash

Find the serial port. On macOS it looks like `/dev/cu.usbmodem*`. On Linux it
looks like `/dev/ttyACM0` or `/dev/ttyUSB0`.

From the unzipped folder, for an 8 MB board:

```bash
python3 -m esptool --chip esp32s3 --port <port> --baud 460800 \
  write-flash --flash-mode dio --flash-size 8MB \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0xf000  ota_data_initial.bin \
  0x20000 esp32-csi-node.bin
```

For a 4 MB board, use `--flash-size 4MB`. The file names and the four
offsets stay the same.

## 3. Provision Wi-Fi and the target

The firmware needs your Wi-Fi details and the address of the computer that
will run the server. From the repository root:

```bash
python3 firmware/esp32-csi-node/provision.py --port <port> \
  --ssid "<your-ssid>" --password "<your-password>" \
  --target-ip <host-ip> --target-port 5005 --node-id 1
```

`<host-ip>` is your computer's address on the Wi-Fi network, for example
`192.168.x.y`. The script keeps what you set between runs, so you can change
one value later without repeating the others. `--reset` clears it.

For more than one node, OTA updates and the other options, see
[provisioning-and-ota.md](provisioning-and-ota.md).

## 4. Build and start the server

Build once:

```bash
cd v2
cargo build --release -p wifi-densepose-sensing-server
cd ..
```

Pick an API token and start the server from the repository root:

```bash
export RUVIEW_API_TOKEN="$(openssl rand -hex 24)"
echo "$RUVIEW_API_TOKEN"   # keep this, you enter it in the UI

./v2/target/release/sensing-server \
  --source esp32 \
  --udp-bind 0.0.0.0 \
  --udp-allow <node-subnet> \
  --ui-path ui
```

Why each flag matters:

- `--source esp32` makes the server listen for the board and nothing else.
  The default `auto` also works, but it starts with data tagged `simulated`
  and only switches once a real frame arrives, which can hide a setup
  problem. Setting it explicitly keeps the status honest.
- `--udp-bind 0.0.0.0` lets frames from another machine in. The default is
  loopback only, so a board on your network cannot reach the server without
  it.
- `--udp-allow` lists the addresses allowed to send frames. Use your
  network's range, for example `192.168.x.0/24`. A routable bind with no
  allowlist makes the server exit with an error. For a throwaway test you can
  use `--udp-insecure-lan` instead, which accepts frames from any address.
  Do not use it on a network you do not trust. Even with an allowlist, the
  UDP stream itself is unauthenticated: the allowlist only filters by sender
  address.
- With the default `--source auto` the server shows data tagged
  `simulated` until the first real frame arrives. Simulated output is not
  sensing. If you see `simulated`, no frames are reaching the server.
- `RUVIEW_API_TOKEN` turns on token auth for the API. Without it the server
  runs with auth off and logs `API auth: OFF`.

The server listens for the dashboard on `127.0.0.1:8080`, so only your own
computer can open it.

## 5. Open the dashboard

Open <http://localhost:8080/ui/> in a browser on the same computer. The
trailing `/ui/` matters; `/` is only an info page.

A first-run welcome tour may cover the dashboard; click "Skip tour". Until you
enter the token the page shows "Connecting...".

Click the QuickSettings panel, find **API Access**, paste the token and click
**Save & Apply**. The page reloads and the header shows Live. Data Source should read
"ESP32 — Real hardware connected". The Streaming card may still say "IDLE,
0 client(s)" while data flows; that is a known display quirk, so trust the
header and the nodes check below.

## 6. Check that it is working

Power the board. Within a few seconds the dashboard should show data. To
check from a terminal:

```bash
curl -s -H "Authorization: Bearer $RUVIEW_API_TOKEN" \
  http://localhost:8080/api/v1/status
```

Status alone cannot tell "never connected" from "working". With
`--source esp32` it reports `"source": "esp32"` and a `source_state` of
`live_unverified` even before the first frame. `esp32:offline` or
`disconnected` appears only after frames arrived and then stopped. So use the
nodes endpoint as the real check. Call it twice, a few seconds apart:

```bash
curl -s -H "Authorization: Bearer $RUVIEW_API_TOKEN" \
  http://localhost:8080/api/v1/nodes
```

An empty list (`"nodes":[]`) means no frames have arrived. For your node
you want `csi_status` of `active`, `csi_last_seen_ms` under
5000, and a `csi_sequence` that grows between calls. The growth divided by
the seconds between calls is the rate the board is sending. Frames lost on
the network are not subtracted, so treat it as an upper bound on what the
server received.

If nothing arrives, see [troubleshooting.md](troubleshooting.md).

## What is real and what is not

A live dashboard proves that CSI frames travel from the board to the server.
It does not validate any capability. Presence, pose, vital signs and counts
each have a status tagged `MEASURED`, `CLAIMED` or `SYNTHETIC` in
[whats-real.md](whats-real.md).

## Tested and not tested

Validated for this guide (as of 2026-10-01):
the checksum step, the from-source firmware build, the server build and
start on macOS, the bind and allowlist behaviour, token auth, the dashboard
and the QuickSettings token entry, and the `/api/v1/nodes` check against
live ESP32 nodes (the nodes ran a non-stock 0.8.12 build).

Not tested:

- The flash and provision steps were not run on a device for this guide; they
  need supervised hardware. The flash command was only checked to parse.
- The native server was tested on macOS. Linux native is untested.
- There is no bundle for flash sizes other than 8 MB and 4 MB, and the 8 MB
  bundle is unverified on 16 MB boards.

## Next steps

- Calibrate: wait about 10 seconds of live frames, then
  `POST /api/v1/calibration/start`. See the
  [calibration guide](../calibration-guide.md). The `--calibrate` boot flag
  is not supported and exits with an error.
- Other targets, build options and QEMU:
  [firmware README](../../firmware/esp32-csi-node/README.md).
- Full flag list:
  [sensing-server README](../../v2/crates/wifi-densepose-sensing-server/README.md).
  Bind policy:
  [SECURITY.md](../../v2/crates/wifi-densepose-sensing-server/SECURITY.md).
