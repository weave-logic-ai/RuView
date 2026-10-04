# Getting started with RuView

RuView turns the Wi-Fi channel state information (CSI) captured by small ESP32
boards into presence, motion and coarse vital-sign signals. It is camera-free,
and it is not camera-grade: read [What's real](whats-real.md) before you rely on
any number.

These pages give one tested path for each job. Their commands were run against
real nodes where that was possible, and each page says what was not tested.

## Pick your job

| I want to… | Read |
|---|---|
| Go from an ESP32-S3 board to a live dashboard (install, flash, provision, run the server, open the UI) | [Quickstart: ESP32-S3 to a live dashboard](quickstart-esp32-s3.md) |
| Run the sensing server in Docker | [Run the sensing server in Docker](docker.md) |
| Set Wi-Fi, node IDs and other settings on a node, run several nodes, or update and roll back firmware | [Provisioning and OTA](provisioning-and-ota.md) |
| Fix something that is not working | [Troubleshooting](troubleshooting.md) (symptom first) |
| Know which capabilities are measured, claimed or synthetic | [What's real](whats-real.md) |
| Calibrate a room | [Calibration guide](../calibration-guide.md) |
| Use the dashboard and its data sources | [UI README](../../ui/README.md) |
| Connect Home Assistant | [Home Assistant integration](../integrations/home-assistant.md) (MQTT; see [What's wired](../../README.md#whats-wired) for what is built) |
| Record data and train a model | Not yet documented as a tested path. Read [Record and train: current limitations](whats-real.md#record-and-train-current-limitations) first; published models are described in the [model card](../huggingface/MODEL_CARD.md) |
| Report a security problem | [SECURITY.md](../../SECURITY.md) (private reporting, not a public issue) |

## What you need

- An ESP32-S3 board (the quickstart's tested path) or an ESP32-C6 (see
  [Provisioning and OTA](provisioning-and-ota.md) for the C6 image).
- A 2.4 GHz Wi-Fi network shared by the boards and the computer running the
  server.
- macOS or Linux for the native server, or Docker. The pages note where a
  platform was not tested.

## Two things that trip up almost everyone

- **The server listens for node data on loopback only by default.** A board on
  your Wi-Fi cannot reach it until you start the server with a UDP bind and an
  allowlist. The quickstart and the Docker page show how.
- **"Simulated" means no board is connected yet.** With the default data source
  the dashboard shows data tagged `simulated` until the first real frame
  arrives. See [Troubleshooting](troubleshooting.md).

## More reference

- [User guide](../user-guide.md): the full reference, including the API, the
  Python package and other data sources.
- [Firmware README](../../firmware/esp32-csi-node/README.md): firmware build
  options, NVS keys and wire formats.
- [Architecture decision records](../adr/README.md).
