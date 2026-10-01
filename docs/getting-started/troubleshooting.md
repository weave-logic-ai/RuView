# Troubleshooting

Find your symptom, check the likely cause, run the check, apply the fix. Every
entry has four parts: **Symptom**, **Likely cause**, **Check**, **Fix**.

Placeholders: `<node-ip>` is the ESP32's address, `<host-ip>` is the machine
running the server, `<port>` is the USB serial port, `<cidr>` is the subnet your
nodes are on (for example the one your router hands out).

Default ports. Native `sensing-server`: UI on HTTP 8080 under `/ui/`, WebSocket
8765, CSI in on UDP 5005. Docker image: UI on 3000, WebSocket on 3001, UDP 5005.
The node OTA server is on 8032.

Status of the checks: causes come from source, open-issue triage and bench runs
(a macOS host with OrbStack Docker and ESP32 nodes). Linux and Windows hosts were
not tested. Where an entry says a cause is not reproduced, treat it as the most
likely one, not a verdict.

Trust-state and engine errors are a separate topic: see
[trust-and-engine-errors.md](../trust-and-engine-errors.md).

## Contents

1. [Dashboard stuck on "Connecting..."](#1-dashboard-stuck-on-connecting)
2. [Server receives 0 frames](#2-server-receives-0-frames)
3. [Server exits at boot](#3-server-exits-at-boot)
4. [0 or 1 frames per second from the release binaries](#4-0-or-1-frames-per-second-from-the-release-binaries)
5. [sendto ENOMEM on the node](#5-sendto-enomem-on-the-node)
6. [OTA fails or reverts](#6-ota-fails-or-reverts)
7. [Person count stuck at 1](#7-person-count-stuck-at-1)
8. [ESP32-C6 stops sending](#8-esp32-c6-stops-sending)
9. [Processing loop freezes with more than one node](#9-processing-loop-freezes-with-more-than-one-node)
10. [A board inherits another board's provisioning](#10-a-board-inherits-another-boards-provisioning)
11. [USB board not detected on macOS](#11-usb-board-not-detected-on-macos)
12. [Browser gets 421 or a 404 on the UI](#12-browser-gets-421-or-a-404-on-the-ui)
13. [Docker Desktop on Windows drops UDP from multiple nodes](#13-docker-desktop-on-windows-drops-udp-from-multiple-nodes)
14. [Node associates but never appears](#14-node-associates-but-never-appears)
15. [Dashboard shows data but no node is connected ("simulated")](#15-dashboard-shows-data-but-no-node-is-connected-simulated)

---

## 1. Dashboard stuck on "Connecting..."

**Symptom.** The UI loads, but the status stays on "Connecting..." and no data
appears.

**Likely cause.** One of three:

- API auth is on (`RUVIEW_API_TOKEN` is set on the server) and the browser has
  no token saved.
  The UI needs the token for REST calls and for the WebSocket ticket request.
- The page is the pose-fusion demo on a native server. It derives the WebSocket
  port as HTTP port + 1, which gives 8081, but the native WebSocket default is
  8765.
- The server is up but nothing is feeding it (see [section 2](#2-server-receives-0-frames)).
  The WebSocket connects, but there are no frames.

**Check.**

```bash
curl -s http://localhost:8080/health          # native; use port 3000 for Docker
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/api/v1/status
```

A `401` from the second command means auth is on. In the browser developer
tools, a failed request to `/api/v1/ws-ticket` or a WebSocket that closes
immediately points at the token.

**Fix.**

1. Open the QuickSettings panel, section **API Access**, paste the token, and
   press **Save & Apply**. The page reloads and uses `Authorization: Bearer` for
   REST and a short-lived ticket for the WebSocket.
2. Use the main dashboard at `/ui/` rather than the pose-fusion page on a native
   server, or start the native server with `--http-port 3000 --ws-port 3001`.
3. If the token step is fine, go to section 2.

## 2. Server receives 0 frames

**Symptom.** The server is running and the UI loads, but there is no CSI data, the
node count is 0, or the live view stays simulated.

**Likely cause**, in the order to check:

1. **UDP bind.** A native server binds UDP to `127.0.0.1` by default. A node on
   your LAN cannot reach that. The boot log says
   `UDP data plane security: ... loopback-only`.
2. **Allowlist.** If you bound to a routable address with `--udp-allow`, the
   node's source address is not in the list. (Loopback is always allowed.)
3. **Wrong target.** The node was provisioned with a different `--target-ip` or
   `--target-port` than the server's address and UDP port (default 5005). The
   firmware's compile-time defaults are `192.168.1.100` and port 5005 when NVS is
   empty.
4. **Host Wi-Fi picked as the source (narrow case).** With `--source auto`, the
   server can choose the host's own Wi-Fi instead of the node, but only when a
   `mac_wifi` helper is on the `PATH`. This was not reproduced on a stock Mac.
5. **Docker.** The container's UDP receiver binds `127.0.0.1` inside the
   container, so published `5005/udp` traffic never reaches it. Even with a
   routable bind, Docker presents every frame as coming from the bridge gateway,
   not from `<node-ip>`, so a node-subnet allowlist drops them (measured on
   OrbStack on macOS; Linux not tested).
6. **Firewall.** The host firewall blocks inbound UDP on 5005.
7. **Different networks or client isolation.** The node and host are on
   different subnets or the access point isolates clients.

**Check.**

```bash
# Native server: look for the UDP bind line in the startup log.
# "loopback-only" means LAN nodes cannot reach it.

# Is anything listening on the CSI port, and on which address?
lsof -nP -iUDP:5005

# What would be flashed for this port? (Prints the saved Wi-Fi password in
# plaintext: do not paste this output into an issue.)
python firmware/esp32-csi-node/provision.py --port <port> --state

# Are datagrams reaching the host at all? (Needs sudo.)
sudo tcpdump -ni any udp port 5005
```

If `tcpdump` shows packets from `<node-ip>` but the server shows none, the
problem is the bind or the allowlist. If `tcpdump` shows nothing, the problem is
the node's target, the network, or a firewall.

**Fix.**

```bash
# Native: listen on all interfaces and allow your node subnet.
sensing-server --source esp32 --udp-bind 0.0.0.0 --udp-allow <cidr>
```

`--udp-insecure-lan` accepts frames from any LAN address; use it only on a
network you trust. For Docker, set `-e RUVIEW_UDP_BIND=0.0.0.0` and either
`-e RUVIEW_UDP_ALLOW=<gateway>/32` or `-e RUVIEW_UDP_INSECURE_LAN=true`. Use the
Docker bridge gateway address, not the node subnet, in the allowlist. The
boolean environment variables take `true` or `false`; `1` is rejected and the
server exits with `invalid value '1' for '--udp-insecure-lan'`. The one
exception is `RUVIEW_ALLOW_UNAUTHENTICATED`, which must be exactly `1`. See
[docker.md](docker.md).

Passing `--source esp32` explicitly removes any doubt about source selection.

Re-provision the node if the target is wrong (see
[provisioning-and-ota.md](provisioning-and-ota.md)).

## 3. Server exits at boot

**Symptom.** The server or container stops within a second of starting. The
dashboard never loads.

**Likely cause**, by exit code:

| Exit | Cause |
|---|---|
| 1 | A routable `--udp-bind` (for example `0.0.0.0`) with neither `--udp-allow` nor `--udp-insecure-lan`; or `RUVIEW_OAUTH_ISSUER` set to an empty value or with an unreachable JWKS endpoint |
| 2 | `--calibrate` was passed. It is not a boot flag |
| 64 | Docker only: no `RUVIEW_API_TOKEN`, no `RUVIEW_ALLOW_UNAUTHENTICATED=1`, and a non-loopback bind (the default `0.0.0.0`) |

Also: an unrecognised `--source` value (for example `macos` or `linux`) does not
exit. The server starts with no UDP socket and no data task, silently, and
`/api/v1/status` still reports `"source_state":"live_unverified"` for it, so it
looks live while doing nothing.

**Check.**

```bash
docker logs <container>          # Docker
sensing-server --help            # confirm which flags exist in your build
```

**Fix.**

- Exit 1 (bind): add `--udp-allow <cidr>`, or `--udp-insecure-lan` on a trusted
  LAN.
- Exit 1 (OAuth): unset `RUVIEW_OAUTH_ISSUER`, or make the JWKS URL reachable
  from the server.
- Exit 2: start the server, wait at least 10 s for live frames, then call
  `POST /api/v1/calibration/start` with one `source_node_id`.
- Exit 64: set `-e RUVIEW_API_TOKEN=<token>`, or for a throwaway local test only,
  `-e RUVIEW_ALLOW_UNAUTHENTICATED=1`.
- Use only `auto`, `esp32`, `wifi` or `simulated` for `--source`.

A plain `docker run -p 3000:3000 ruvnet/wifi-densepose:latest` exits 64 for this
reason, as does the shipped compose service for `sensing-server` (measured with
`docker compose run`).

## 4. 0 or 1 frames per second from the release binaries

**Symptom.** A freshly flashed node sends 0 or about 1 frame per second (pps).
Calibration never gets enough data. Reported in #1499 (S3 DevKitC-1, 0 pps) and
#1899 (S3 display boards, about 1 pps).

**Likely cause.** The prebuilt images in `firmware/esp32-csi-node/release_bins/`
are old: their `version.txt` reads 0.6.7 (built 2026-06-02), while the source is
at 0.8.12. In #1499, the old image falsely detects a display on a board that has
none, and CSI stays at 0 pps. A source build with the DevKitC overlay gave 29 to
38 pps for the same reporter.

**Check.**

```bash
cat firmware/esp32-csi-node/release_bins/version.txt
# On the node's serial console at boot, look for the App version line.
python -m serial.tools.miniterm <port> 115200
```

**Fix.** Do not flash from `release_bins/`. Flash a published release bundle
(`esp32-csi-node-firmware-<variant>.tar.gz` from a `v*-esp32` release) or build
from source with the overlay for your board. Which release carries a bundle for
the current source version is covered in [quickstart-esp32-s3.md](quickstart-esp32-s3.md).
The latest stable firmware release is older than the source on `main`, so check
the release page rather than assuming the versions match.

## 5. sendto ENOMEM on the node

**Symptom.** The serial log shows `sendto` failing with `ENOMEM` and zero UDP
frames reach the server. Reported in #1764 on an S3 N16R8 AMOLED board. Setting
`--edge-tier 1` did not help.

**Likely cause.** Memory pressure, probably from the display probe's DMA
allocation. A proposed fix (#1142) raises the Wi-Fi TX buffer count to 128. The
shipped default is `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64`. It is not
confirmed whether current `main` is fixed.

**Check.**

```bash
python -m serial.tools.miniterm <port> 115200
# Look for repeated "sendto" failures with errno 12 (ENOMEM).
```

**Fix.** The reporter's workaround was to build with the DevKitC overlay (no
display probe). On a board without a display, use that build. If you build from
source, you can raise the TX buffer count in `sdkconfig.defaults` yourself; this
is a workaround, not a confirmed fix.

## 6. OTA fails or reverts

**Symptom.** `POST /ota` returns 403, the connection drops and the node keeps the
old version, or the node updates and then goes back to the old version after a
reset.

**Likely cause.** Three separate gaps, tracked in #1893 and related issues:

1. **No OTA key on the node.** OTA is fail-closed. With no key (PSK) stored in
   NVS, every `POST /ota` gets 403. `provision.py` on `main` has no `--ota-psk`
   option; it is not yet available (PR #1760).
2. **Stack overflow.** The HTTP server task overflows its stack in
   `esp_ota_end()` while validating the image, so the upload drops (#1893,
   reported on S3, v0.8.8).
3. **No mark-valid.** No code calls `esp_ota_mark_app_valid_cancel_rollback`.
   With rollback enabled in a build, the new image reverts after the next reset.
   The released images do not enable rollback, so this applies to builds that
   do (the 16 MB config). A fix is in progress on a branch
   (`fix/ota-mark-app-valid`).

**Check.**

```bash
curl -s --max-time 3 http://<node-ip>:8032/ota/status     # shows running and next partition
```

A 403 on `POST /ota` is cause 1. A dropped connection part-way is cause 2. A
version that flips back after reset is cause 3.

**Fix.** Until the fixes land, update over USB: re-flash with `esptool`, and
re-provision if you erased NVS. See [provisioning-and-ota.md](provisioning-and-ota.md).
Do not rely on OTA for a fleet yet.

## 7. Person count stuck at 1

**Symptom.** `estimated_persons` reads 1 whatever the room holds, or it
fluctuates and does not match who is present (#2058, #1940). It never shows 0.

**Likely cause.** This is how the count is built today, not a misconfiguration.

- The live count comes from a single scalar-score heuristic. The server floors
  the result at 1, so it cannot report an empty room as 0
  (`v2/crates/wifi-densepose-sensing-server/src/main.rs`, `.max(1)` in the
  count path).
- The eigenvalue-based occupancy path is compiled out of the shipped server
  binary (#1940), so it is not used.
- The multi-node aggregation uses a dedup factor that is a tuning guess. Other
  reports show overcounting (several bodies read as more than the real number).
- A recent change (#2060) drops stale nodes from the room count, which can
  change what you see on an existing setup. It is not confirmed on hardware.

No measurement of the count's accuracy exists. See [whats-real.md](whats-real.md).

**Check.**

```bash
curl -s http://localhost:8080/api/v1/sensing/latest   # inspect estimated_persons and per-node data
```

**Fix.** There is no fix on your side. Use presence and motion for "is anyone
there", not the count for "how many". The dedup factor is runtime-tunable at
`/api/v1/config/dedup-factor`, which can reduce overcounting on a given room but
does not make the count a measurement. Calibrate the room with an empty-room
baseline first (see [../calibration-guide.md](../calibration-guide.md)).

## 8. ESP32-C6 stops sending

**Symptom.** A C6 stays connected to Wi-Fi and still answers HTTP requests, but
the frame count stops. Only a hard reset brings frames back (#1941).

**Likely cause.** The CSI capture callback stops while the rest of the node keeps
running. `main` has no capture watchdog. PR #2050 adds one (it re-arms CSI and
then restarts). Separately, #1941 and #1899 report that current C6 and S3 images
do not hold a usable CSI rate in every setup, so do not assume a particular frame
rate.

**Check.** Watch the server's per-node frame counter. If it stops advancing while
`curl http://<node-ip>:8032/ota/status` still answers, this is the stall.

**Fix.** Power-cycle or hard-reset the node (press reset, or unplug and replug
USB). For an unattended C6, a watchdog build (#2050) is the intended answer once
merged. Until then, expect to reset it.

## 9. Processing loop freezes with more than one node

**Symptom.** With two or more nodes, after 5 to 15 minutes the server's `tick`
stops advancing, `/health` still says ok, and the UI shows stale data. Nothing is
logged (#1894).

**Likely cause.** Unknown. The report used Docker Desktop on macOS. A related
change (#1814) rate-limits broadcasts to the tick interval and may be relevant.
A native multi-node setup has run without a freeze at about 30 fps per node, but
that was not a timed soak.

**Check.** Poll the tick and compare:

```bash
curl -s http://localhost:8080/health
sleep 30
curl -s http://localhost:8080/health
```

If the tick has not changed while nodes are sending, you have the freeze.

**Fix.** Restart the server. If you are on Docker Desktop, try a native server to
see whether the freeze follows the Docker UDP path. Please add your platform,
node count and server logs to #1894.

## 10. A board inherits another board's provisioning

**Symptom.** A second board, flashed and provisioned, reports the first board's
`node_id` (or Wi-Fi settings).

**Likely cause.** `provision.py` is additive. It merges new flags into a
per-port state file on your computer, keyed by the serial port path. When you
plug a different board into the same port, it inherits the previous board's
values (#1755). A fix is proposed in PR #1760.

**Check.**

```bash
python firmware/esp32-csi-node/provision.py --port <port> --state
```

This prints what would be flashed, including inherited values. It also prints
the saved Wi-Fi password in plaintext, so redact it before sharing the output.

**Fix.** Wipe the saved state for that port, then provision with every field set:

```bash
python firmware/esp32-csi-node/provision.py --port <port> --reset \
  --ssid "<ssid>" --password "<password>" --target-ip <host-ip> --node-id <n>
```

`--reset` deletes the saved record before it checks your arguments, so a run
that is missing the Wi-Fi fields exits with an error and the record is already
gone. Give every node a unique `--node-id`. Note that the state file stores the Wi-Fi
password in readable form (#1754); do not share it.

## 11. USB board not detected on macOS

**Symptom.** No serial port appears when you plug the board in, or `esptool`
cannot find it.

**Likely cause.** In rough order of frequency:

1. A charge-only USB cable with no data lines.
2. The wrong USB-C port. Many ESP32-S3 DevKit boards have two. One goes to a
   USB-to-UART bridge (CP210x or CH340) and the other to the native USB. Use the
   port your board's documentation marks for flashing and serial; if one shows
   nothing, try the other.
3. A missing driver for the bridge chip, or the board needs to be put in download
   mode (hold BOOT, tap RESET, release BOOT).
4. A hub or adapter that does not pass data.

**Check.**

```bash
ls /dev/cu.*                      # look for usbserial / usbmodem / SLAB / wchusbserial
system_profiler SPUSBDataType | grep -i -E "espressif|cp210|ch340|silicon labs|wch"
python -m esptool --port <port> chip_id
```

If neither command shows the board, the computer does not see it at all, so
rule out the cable and port before the software.

**Fix.** Swap to a known data cable, plug straight into the computer, try the
other USB-C port, then install the CP210x or CH340 driver if your board uses that
bridge. Retry after a cable or port change rather than re-running the same check.

## 12. Browser gets 421 or a 404 on the UI

**Symptom.** A browser on another machine gets `421 Misdirected Request`, or
`http://localhost:8080/` shows only an info page, or the UI path returns 404.

**Likely cause.**

- **421.** Host-header validation is on. By default only `localhost`,
  `127.0.0.1` and `[::1]` are accepted. Browsing by the host's LAN IP is
  rejected. Docker has it on too.
- **Info page at `/`.** The dashboard is under `/ui/`, not `/`.
- **404 under `/ui/`.** The server cannot find the UI files. `--ui-path` is
  resolved relative to the current directory. If it is wrong, the server falls
  back to the first of `../ui`, `./ui`, `../../ui` that exists and logs a
  warning (the UI then works). You get a 404 only when none of them exist.

**Check.** Open `http://localhost:8080/ui/` on the host itself. Read the startup
log for the UI path warning.

**Fix.**

```bash
# Allow your LAN address (also bind HTTP/WS beyond loopback).
sensing-server --bind-addr 0.0.0.0 --allowed-host <host-ip>
# or:  SENSING_ALLOWED_HOSTS=<host-ip>
# UI files not found:
sensing-server --ui-path /absolute/path/to/ui
```

`--bind-addr 0.0.0.0` exposes the API to your network. Set `RUVIEW_API_TOKEN`
when you do. `--disable-host-validation` also works but removes the check; avoid
it.

## 13. Docker Desktop on Windows drops UDP from multiple nodes

**Symptom.** Two or more nodes transmit (visible in `tcpdump` or Wireshark on the
host), but inside the container only one source address arrives. Reported in #374
and #386.

**Likely cause.** Docker Desktop on Windows forwards inbound UDP through a VM and
multiplexes multiple source addresses onto one virtual socket. The first source
wins. This is a Docker Desktop limitation, not a sensing-server bug.

**Check.** `GET /api/v1/sensing/latest` lists one node while several transmit.

**Fix.** Run the bundled relay on the host so every datagram reaches Docker from
the same source:

```powershell
python scripts/udp-relay.py --listen-port 5005 --forward-port 5006
```

Then map the container's UDP port to the relay's forward port (`5006:5005/udp`)
and bring the stack up. Nodes still target `<host-ip>:5005`, so no
re-provisioning is needed. You still need the UDP bind settings from
[section 2](#2-server-receives-0-frames). Linux and macOS hosts are not affected.
Use `--verbose` on the relay to confirm each node's address appears.

## 14. Node associates but never appears

**Symptom.** The node joins Wi-Fi and its LED blinks, but no CSI reaches the
server and it is missing from `/api/v1/nodes`.

**Likely cause.** After a USB flash, a node can end up connected to Wi-Fi with
the UDP sender silently not working. Also see [section 2](#2-server-receives-0-frames).

**Check.** Watch the serial console for CSI and UDP send messages
(`python -m serial.tools.miniterm <port> 115200`).

**Fix.** Power-cycle the node: unplug USB, wait two seconds, replug. Firmware
0.8.0 and later includes a watchdog that resets after 30 s of zero CSI frames.
An older image lacks it, so update the firmware over USB.

## 15. Dashboard shows data but no node is connected ("simulated")

**Symptom.** The dashboard animates and shows poses or readings, but you have no
node running, or your node is running and you suspect the data is not from it.

**Likely cause.** With `--source auto` (the default), the server does not exit
when no real source is present at boot. It serves **simulated** data, tagged
`simulated`, keeps UDP 5005 bound, and switches to `esp32` when the first real
frame arrives (#1004). So seeing data does not mean a node is connected. If it
stays on `simulated` after your node is up, no frames are reaching the server:

1. Frames are not arriving on UDP 5005, usually because of the loopback UDP bind
   (see [section 2](#2-server-receives-0-frames)).
2. Only if a `mac_wifi` helper is on the `PATH`: `auto` can choose host Wi-Fi as
   the source. This did not happen on a stock Mac. Pass `--source esp32` to rule
   it out.

Simulated output is SYNTHETIC by definition. See [whats-real.md](whats-real.md).

**Check.**

```bash
curl -s http://localhost:8080/api/v1/status    # look at "source" and "source_state"
curl -s http://localhost:8080/health           # also carries "source"
```

Also read the startup log: the line `Data source: <source> ...` names the source,
and a warning says "serving SIMULATED data" when none was found at boot.
`simulated` means no live frames; `esp32` means live frames have arrived (the
status endpoint's `source_state` moves from `synthetic` to `live_unverified`).

**Fix.** Work through section 2. For a deliberate offline demo, `--source
simulated` is the explicit setting.

---

## Still stuck

Collect the server startup log, the node's serial log from boot, the firmware
version (App version line), and the output of the check commands, then search
[open issues](https://github.com/ruvnet/RuView/issues) before filing a new one.
Redact SSIDs, passwords, tokens and IP addresses from anything you post.
