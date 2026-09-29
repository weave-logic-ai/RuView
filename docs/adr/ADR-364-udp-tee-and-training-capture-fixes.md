# ADR 364: Loopback UDP tee and training capture fixes

## Status

Proposed. Implemented on `feat/training-phase0-udp-tee`. Unit tested, and
smoke tested on loopback: a datagram sent to the server arrived byte for byte
at the tee target, and a `../` recording id was refused. Not yet exercised
against the live five node fleet.

Date: 2026-09-28

## Context

A five node deployment (four ESP32 S3, one ESP32 C6) streams ADR 018 frames to
one sensing server. Two gaps block the next steps.

**A second consumer cannot see the raw stream.** Each node sends to a single
`target_ip` (`firmware/esp32-csi-node/main/stream_sender.c`). The server's
WebSocket and REST outputs carry per node amplitude only: no phase, no I/Q, no
sequence number, and no `0xC511A110` sync packets. A fusion host that aligns
CSI with camera, radar, or lidar needs all of those. Binding a second listener
to the CSI port is not an option, and an external relay in front of the port
would make every source loopback, which the ADR 296 allowlist always admits.

**Training capture produced data that could not train a model for this
deployment.**

1. `POST /api/v1/recording/start` built its file path from an unvalidated `id`
   (issue #615 class) and truncated any earlier recording with the same id.
2. The adaptive classifier trained on the room level fused features plus the
   amplitudes of whichever node a `HashMap` iterated first, while runtime
   classification is per node from that node's own features and amplitudes.
3. Recordings keep the first 56 subcarriers per node, but runtime computed the
   classifier's subcarrier statistics over the full frame.
4. The trainer stamped new models `version: 1`, so the server replaced the
   measured temporal `dominant_freq_hz` (ADR 356) with the legacy subcarrier
   proxy at runtime, although the recordings carry the measured value.
5. `scripts/collect-ground-truth.py` sent no bearer token and treated any HTTP
   200 as success, so with authentication enabled it captured camera
   keypoints with no paired CSI and only printed a warning.

## Decision

1. `--udp-tee` (env `RUVIEW_UDP_TEE`) copies every datagram the source
   allowlist admits, byte for byte, to up to four `ip:port` targets. Targets
   must be loopback, so raw CSI never leaves the host through the tee, and
   may not use port 0 or the listener's own port, which would loop. Sends are
   non blocking; a copy that cannot be queued is dropped and counted. The tee
   is off unless configured.
2. Recording ids must pass `path_safety::safe_id`, and a recording is created
   with `create_new`, so a reused id is refused instead of overwriting data.
3. Training takes one sample per node per new frame. Each broadcast carries a
   snapshot of every node, so a node contributes only when its amplitude
   vector changed since the previous line. Stale nodes and nodes without
   amplitudes are skipped. Lines without `node_features` fall back to the
   previous frame level behaviour.
4. `RECORDED_AMPLITUDE_LEN` (56) is shared by the broadcast and the runtime
   feature extractor.
5. The trainer stamps `TRAINED_MODEL_VERSION` (2).
6. The ground truth collector reads the token from `--token-file` or
   `RUVIEW_API_TOKEN`, checks the response body, names the CSI recording
   `gt_<timestamp>` to match its keypoints file, and exits unless
   `--allow-no-csi` is given when the recording cannot start.

## Consequences

A fusion host or Cognitum cog can subscribe to the full data plane by reading
a loopback port, and `cog-sensor-sources` can keep its default bind by moving
to the tee port via `COG_CSI_BIND`.

Adaptive models trained after this change match the runtime inputs. Models
trained earlier, including the committed `v2/data/adaptive_model.json`, were
trained on first node amplitudes; their runtime inputs now use the recorded
56 subcarrier window, which matches how they were trained more closely than
before.

`training_accuracy` remains an in sample figure and is now labelled as one. It
is not an evaluation. A held out, session split evaluation is the next step
and is out of scope here.

Not addressed: the server trainer (`training_api.rs`) reads a flat per frame
schema that recordings do not produce and trains on heuristic targets; the
standalone Python UDP recorders predate the 20 byte ADR 018 header.

## Validation

```bash
cd v2
cargo test -p wifi-densepose-sensing-server --no-default-features --lib udp_tee
cargo test -p wifi-densepose-sensing-server --no-default-features --lib adaptive_classifier
```
