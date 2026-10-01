# What's real

RuView is a camera-free RF perception system. WiFi sensing is not camera-grade,
and this page says what each capability has and has not been shown to do.

Every row carries one tag:

- **MEASURED**: reproduced on real hardware or a pinned test, with a command or
  log you can check.
- **CLAIMED**: stated by a source (README, model card, external result), not
  reproduced in this repository's harness. Treat it as a claim.
- **SYNTHETIC**: checked only against generated or fixture data. It shows the
  code works, not that it works on a room.

Where a capability has no measured number, the page says so rather than filling
the cell.

## Capabilities

| Capability | Status | What is there | Reproducer / source |
|---|---|---|---|
| Presence | CLAIMED | Heuristic motion and phase-variance detection with about 30 s of ambient calibration, plus a trained head on Hugging Face. No accuracy number has been measured. The old "100% presence" figure was retracted: it came from a single-class recording. | Pipeline checks: [`PROOF.md`](../../PROOF.md) (`bash scripts/prove.sh`; stop any local `sensing-server` on UDP 5005 first, because the workspace tests send synthetic frames to `127.0.0.1:5005`). No presence-accuracy reproducer |
| Person count | CLAIMED (heuristic, unmeasured) | One scalar-score heuristic. It is floored at 1 and cannot report an empty room. The eigenvalue occupancy path is compiled out of the shipped server. Nothing measures its accuracy, and overcounting is reported. | #2058, #1940; unit tests only: `cd v2 && cargo test -p wifi-densepose-sensing-server --bin sensing-server --no-default-features person_count_tests` (a test is not a measured room) |
| Breathing | SYNTHETIC | A 0.1 to 0.5 Hz band-pass extractor. Tests use generated signals. No real-CSI accuracy has been measured. | `cd v2 && cargo test -p wifi-densepose-vitals`; [vitals README](../../v2/crates/wifi-densepose-vitals/README.md) |
| Heart rate | SYNTHETIC | Fixture-based software checks (breathing without a pulse, mixed signals, noise, band edges). These are not clinical BPM accuracy and not real-CSI accuracy. Open issue #2057 covers the extractor. | `cd v2 && cargo test -p wifi-densepose-vitals` |
| Embeddings (v2 CSI encoder) | CLAIMED | 82.3% is a held-out temporal-triplet **embedding-retrieval** score. It is not presence, occupancy or detection accuracy. | Model card: `ruvnet/wifi-densepose-pretrained` on Hugging Face. No reproducer in the repo |
| Pose, MM-Fi benchmark (17 keypoints) | CLAIMED | 82.69% torso-PCK@20 (83.59% ensemble, 74.30% micro variant) on the MM-Fi `random_split`. That split is not subject-disjoint and no mean-pose baseline is reported, so it is not a leakage-free result. It is an external benchmark, not the live sensor. | `ruvnet/wifi-densepose-mmfi-pose` on Hugging Face; [study](../benchmarks/mmfi-wifi-sensing-study.md) |
| Pose, on-device single ESP32 | CLAIMED (weak) | The committed `pose_v1` scores PCK@20 = 3.0% and PCK@50 = 18.5% on a 217-sample holdout, below the 35% target in ADR-079. Not tagged MEASURED: no reproducer command, mean-pose baseline or split definition is cited. The runtime inference path is a stub that returns confidence 0, so the weights are not used. First-cut, below target, runtime stub. | `v2/crates/cog-pose-estimation/cog/artifacts/train_results.json`; [cog README](../../v2/crates/cog-pose-estimation/cog/README.md). The mean-pose baseline and split definition are not given here, so do not read this as a pose accuracy claim |
| Multi-node fusion | CLAIMED | Multiple nodes feed the server and per-node vitals use best-node selection. The multistatic bridge sets phase to a zero vector and coherence to 1.0, so phase-coherent fusion is not in use (#1752). The timestamp alignment between nodes is an open question (#1710). Whether more than one node runs stably is open (#1894). | No reproducer. See [troubleshooting](troubleshooting.md#9-processing-loop-freezes-with-more-than-one-node) |
| ESP-NOW time sync (C6) | MEASURED (narrow) | 99.56% cross-board match and about 104 us smoothed stdev over a 5 minute two-board soak, on ESP32-C6. This applies to timestamp sync only. It does not carry over to occupancy, vitals, TWT or CSI rate. | [`docs/WITNESS-LOG-110.md`](../WITNESS-LOG-110.md) (needs two C6 boards; no one-line command) |
| TWT and low-power operation (C6) | CLAIMED | Target-wake-time setup and a microamp-level draw still need a hardware log. | [README](../../README.md) says so itself |
| mmWave fusion | CLAIMED | The README describes camera depth, WiFi and mmWave radar feeding one spatial model. No reproducer or measured number backs it here. | None |
| Simulated mode (`--source auto` with no frames) | SYNTHETIC | With no real frames at boot, `auto` serves generated data tagged `simulated` until the first real frame arrives, then switches to `esp32`. Explicit `--source simulated` stays simulated. Nothing shown while the source reads `simulated` was sensed. | `main.rs` source resolution (#1004); check `source` in `/api/v1/status`. See [troubleshooting](troubleshooting.md#15-dashboard-shows-data-but-no-node-is-connected-simulated) |
| CSI frame rate | CLAIMED | Source-built nodes have been seen at about 30 pps. Prebuilt `release_bins/` and some C6/S3 images do not hold a usable rate (#1499, #1899, #1941). Do not assume a rate without a log from your own node. | [troubleshooting](troubleshooting.md#4-0-or-1-frames-per-second-from-the-release-binaries) |

## Integrations

"Wired" means code exists that carries sensing data out. Work through a user's own
Home Assistant counts as Home Assistant, not as the other system.

| Integration | Wired? | Build flag | Evidence |
|---|---|---|---|
| Home Assistant (MQTT discovery) | Yes, with a feature flag | `--features mqtt` at build, then `--mqtt` at run. Without the feature, `--mqtt` logs a warning and publishes nothing. The Docker image builds with it. | `v2/crates/wifi-densepose-sensing-server/Cargo.toml` (features), `src/mqtt/` |
| Google Home, Alexa | Indirect only | None. No in-tree client. They appear only if you enable them inside your own Home Assistant. | [`docs/integrations/home-assistant.md`](../integrations/home-assistant.md) |
| SmartThings | No | None | No in-tree code found |
| Matter | Planned | The `matter` feature in the sensing server is empty; the bridge is a no-op and the stack is planned (P7). The `--matter` flags are not in the parser the binary uses. `cog-ha-matter` is a scaffold with an empty data channel. | `Cargo.toml` `matter = []` comment; ADR-116, ADR-122 (proposed) |
| Apple HomeKit | Partly | `hap-server` feature on `homecore-server`, run with `--hap-bind`. It mirrors HomeCore entities. It does not receive CSI frames from the sensing server, and the RuView-to-HAP mapper is exercised only by tests. A separate Python script path (`scripts/ruview-hap-bridge.py`) can put a presence bit into Apple Home. | `v2/crates/homecore-hap`, `homecore-server/src/hap.rs` |

## Record and train: current limitations

On `main`, the record-then-train path has gaps. Check these before you trust a
trained result.

- **Dashboard recordings are not read by training.** The recorder writes
  `<id>.jsonl`, but training looks for `<id>.csi.jsonl`. It does not find the
  file and silently trains on the in-memory frame history instead (#1703; fix
  pending). A training run can therefore succeed without using your recording.
- **CLI `--train` and `--pretrain` fall back to SYNTHETIC data** when the dataset
  path is empty or missing. Check the path; a "successful" run on a missing
  dataset trained on generated data.
- **`--export-rvf` on its own writes placeholder weights.** It is not an export of
  a trained model.
- **Deleting through the API does not work.** `DELETE /api/v1/recording/{id}`
  and `DELETE /api/v1/models/{id}` return 404 for a real id and the file stays.
  Delete the file by hand. Recordings are written under `data/recordings/` relative
  to the directory the server was started from, not under `--data-dir`.
- **Simulated mode is SYNTHETIC.** Data served while the source reads
  `simulated` (see [troubleshooting](troubleshooting.md#15-dashboard-shows-data-but-no-node-is-connected-simulated))
  is generated, not sensed. Do not record or train on it expecting real results.

## Related notes

- Heart-rate fixtures, ESP-NOW sync and embedding retrieval are three different
  numbers. None of them is a room-level accuracy figure.
- Older percentages such as 87.2% in `references/README.md` and
  `plans/phase1-specification/` have no reproducer. Do not cite them.
- The pose model published for the MM-Fi benchmark and the model in the on-device
  cog are different models with different results.
- Hardware validation needs a captured boot or runtime log from real silicon. A
  successful build or a simulator run is not hardware evidence.
- To reproduce the claims that can be reproduced, start with
  [`PROOF.md`](../../PROOF.md) and [`docs/proof-of-capabilities.md`](../proof-of-capabilities.md).
