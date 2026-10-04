# WiFi DensePose UI

A modular, modern web interface for the WiFi DensePose human tracking system. Provides real-time monitoring, WiFi sensing visualization, and pose estimation from CSI (Channel State Information).

## Architecture

The UI follows a modular architecture with clear separation of concerns:

```
ui/
├── app.js                    # Main application entry point
├── index.html                # HTML shell with tab structure
├── style.css                 # Complete CSS design system
├── config/
│   └── api.config.js         # API endpoints and configuration
├── services/
│   ├── api.service.js        # HTTP API client
│   ├── websocket.service.js  # WebSocket connection manager
│   ├── websocket-client.js   # Low-level WebSocket client
│   ├── pose.service.js       # Pose estimation API wrapper
│   ├── sensing.service.js    # WiFi sensing data service (live + simulation fallback)
│   ├── health.service.js     # Health monitoring API wrapper
│   ├── stream.service.js     # Streaming API wrapper
│   └── data-processor.js     # Signal data processing utilities
├── components/
│   ├── TabManager.js         # Tab navigation component
│   ├── DashboardTab.js       # Dashboard with live system metrics
│   ├── SensingTab.js         # WiFi sensing visualization (3D signal field, metrics)
│   ├── LiveDemoTab.js        # Live pose detection with setup guide
│   ├── HardwareTab.js        # Hardware configuration
│   ├── SettingsPanel.js      # Settings panel
│   ├── PoseDetectionCanvas.js # Canvas-based pose skeleton renderer
│   ├── gaussian-splats.js    # 3D Gaussian splat signal field renderer (Three.js)
│   ├── body-model.js         # 3D body model
│   ├── scene.js              # Three.js scene management
│   ├── signal-viz.js         # Signal visualization utilities
│   ├── environment.js        # Environment/room visualization
│   └── dashboard-hud.js      # Dashboard heads-up display
├── utils/
│   ├── backend-detector.js   # Auto-detect backend availability
│   ├── mock-server.js        # Mock server for testing
│   └── pose-renderer.js      # Pose rendering utilities
└── tests/
    ├── test-runner.html       # Test runner UI
    ├── test-runner.js         # Test framework and cases
    └── integration-test.html  # Integration testing page
```

## Features

### WiFi Sensing Tab
- 3D Gaussian-splat signal field visualization (Three.js)
- Real-time RSSI, variance, motion band, breathing band metrics
- Presence/motion classification with confidence scores
- **Data source banner**: green "LIVE - ESP32", yellow "RECONNECTING...", or red "SIMULATED DATA"
- Sparkline RSSI history graph
- "About This Data" card explaining CSI capabilities per sensor count

### Live Demo Tab
- WebSocket-based real-time pose skeleton rendering
- **Estimation Mode badge**: green "Signal-Derived" or blue "Model Inference"
- **Setup Guide panel** showing what each ESP32 count provides:
  - 1 ESP32: presence, breathing, gross motion
  - 2-3 ESP32s: body localization, motion direction
  - 4+ ESP32s + trained model: individual limb tracking, full pose
- Debug mode with log export
- Zone selection and force-reconnect controls
- Performance metrics sidebar (frames, uptime, errors)

### Dashboard
- Live system health monitoring
- Real-time pose detection statistics
- Zone occupancy tracking
- System metrics (CPU, memory, disk)
- API status indicators

### Hardware Configuration
- Interactive antenna array visualization
- Real-time CSI data display
- Configuration panels
- Hardware status monitoring

## Data Sources

The sensing service (`sensing.service.js`) reports these connection states:

| State | Banner Color | Description |
|-------|-------------|-------------|
| **LIVE - ESP32** | Green | Connected to the Rust sensing server receiving real CSI data |
| **RECONNECTING** | Yellow (pulsing) | WebSocket disconnected, retrying (up to 20 attempts) |
| **SIMULATED DATA** | Red | Fallback to client-side simulation after 5+ failed reconnects |
| **API TOKEN REQUIRED** | Red | The server has `RUVIEW_API_TOKEN` set and this browser has no valid token. Retries stop; click the banner or open Settings → API Access |

Simulated frames include a `_simulated: true` marker so code can detect synthetic data.

## Backends

### Rust Sensing Server (primary)
The Rust-based `wifi-densepose-sensing-server` serves the UI and provides:
- `GET /health` — server health
- `GET /api/v1/sensing/latest` — latest sensing features
- `GET /api/v1/vital-signs` — vital sign estimates (HR/RR)
- `GET /api/v1/model/info` — RVF model container info
- `WS /ws/sensing` — real-time sensing data stream
- `WS /api/v1/stream/pose` — real-time pose keypoint stream

The UI talks to the server that serves it: REST calls go to the page's own origin (`config/api.config.js`), and `utils/backend-detector.js` probes `/health/live` there. The archived v1 Python FastAPI backend ([`archive/v1`](../archive/v1/), ADR-187) is not a backend for this UI.

## Quick Start

### With Docker
Follow [`docs/getting-started/docker.md`](../docs/getting-started/docker.md). The container refuses to start without `RUVIEW_API_TOKEN` or an explicit `RUVIEW_ALLOW_UNAUTHENTICATED=1`, and it needs the `RUVIEW_UDP_*` settings to receive frames from a node, so a bare `docker-compose up` does not work as shipped. In Docker the UI is at http://localhost:3000/ui/index.html.

### With local Rust binary
```bash
cd v2
cargo build -p wifi-densepose-sensing-server

# Run with simulated data
target/debug/sensing-server --source simulated --ui-path ../ui

# Run with a real ESP32 on your LAN. UDP binds to loopback by default, and a
# routable bind is refused unless you allow the node's subnet.
target/debug/sensing-server --source esp32 --ui-path ../ui \
  --udp-bind 0.0.0.0 --udp-allow <node-subnet-cidr>
```
Open http://localhost:8080/ui/index.html. The native defaults are HTTP 8080 and WebSocket 8765; the UI maps 8080 to 8765 itself. For the full ESP32-S3 walkthrough, see [`docs/getting-started/quickstart-esp32-s3.md`](../docs/getting-started/quickstart-esp32-s3.md).

## Pose Estimation Modes

| Mode | Badge | Requirements | Accuracy |
|------|-------|-------------|----------|
| **Signal-Derived** | Green | 1+ ESP32, no model needed | Presence, breathing, gross motion |
| **Model Inference** | Blue | 4+ ESP32s + trained `.rvf` model | Full 17-keypoint COCO pose |

To use model inference, start the server with a trained model:
```bash
sensing-server --source esp32 --model path/to/model.rvf --ui-path ./ui
```

## Configuration

### API Configuration
Edit `config/api.config.js`:

```javascript
export const API_CONFIG = {
  BASE_URL: window.location.origin,
  API_VERSION: '/api/v1',
  WS_CONFIG: {
    RECONNECT_DELAY: 5000,
    MAX_RECONNECT_ATTEMPTS: 20,
    PING_INTERVAL: 30000
  }
};
```

## Testing

Open `tests/test-runner.html` to run the test suite:

```bash
cd ui/
python -m http.server 3000
# Open http://localhost:3000/tests/test-runner.html
```

Test categories: API configuration, API service, WebSocket, pose service, health service, UI components, integration.

## Styling

Uses a CSS design system with custom properties, dark/light mode, responsive layout, and component-based styling. Key variables in `:root` of `style.css`.

## License

Part of the WiFi-DensePose system. See the main project LICENSE file.
