@page simulation_architecture Host Simulation Architecture

@tableofcontents

## 1. Purpose and Scope

The host simulation subsystem provides a deterministic runtime for exercising
network-layer behavior on desktop platforms without ESP-IDF dependencies.
It is designed for:

- fast functional verification of routing and forwarding behavior,
- reproducible regression tests under controlled virtual time,
- scale and performance stress runs in CI.

The simulation integrates portable network-layer code (`NetworkManager`,
`RoutingEngine`, `DuplicateFilter`, `ForwardingQueue`) with host adapters for
link transport, location, and channel propagation.

## 2. Layered Architecture

The architecture is intentionally split into API-facing composition and internal
execution mechanisms.

### 2.1 Public API Layer

Primary entry points:

- @ref SimulationBuilder - fluent and file-driven scenario construction.
- @ref SimulationScenario - runtime ownership and lifecycle control.
- @ref SimulationClock - virtual time source.
- @ref SimulatedLocationProvider - deterministic kinematics provider.
- @ref SimulatedLinkLayer - in-memory `ILinkLayer` implementation.
- @ref SimulatedNetwork - channel fan-out and propagation checks.
- @ref ConfigLoader - strict parser for YAML-like config text/files.

Configuration model types:

- @ref SimulationConfig
- @ref SimulationRuntimeConfig
- @ref SimulationDeviceConfig
- @ref SimulationWaypointConfig

### 2.2 Internal Mechanics Layer

Internal behavior centers around three pipelines:

- scenario assembly pipeline (builder -> runtime objects -> manager startup),
- time/kinematics pipeline (step -> clock advance -> location advance -> waypoint apply),
- radio delivery pipeline (send -> channel snapshot -> path-loss filter -> callback delivery).

## 3. Scenario Lifecycle

### 3.1 Build Phase

`SimulationBuilder::build()` validates base constraints and creates
`SimulationScenario`.

Inside `SimulationScenario` construction:

1. Initialize `SimulationClock` from `runtime.start_time_s`.
2. For each device config:
   - create `SimulatedLocationProvider` with initial state,
   - create `SimulatedLinkLayer` bound to shared `SimulatedNetwork`,
   - register node and radio profile in `SimulatedNetwork`,
   - create `NetworkManager` using shared runtime queue/cache settings,
   - normalize waypoint ordering by timestamp.
3. Apply all waypoints already due at start time.

### 3.2 Run Phase

`SimulationScenario::start()` starts each node's `NetworkManager` threads.

`SimulationScenario::step(delta_ms)` performs deterministic progression:

1. `clock_.step(delta_ms)`
2. advance all node location providers by `delta_ms`
3. apply newly due waypoints to each node

### 3.3 Stop Phase

`SimulationScenario::stop()` stops each `NetworkManager`.
The scenario destructor calls `stop()` to enforce safe teardown.

## 4. Data and Time Model

### 4.1 Time Semantics

- Canonical simulation time is stored in milliseconds (`SimulationClock`).
- Components that require second precision consume `nowSeconds()`.
- Location providers track sub-second remainder to preserve timestamp monotonicity.

### 4.2 Position and Motion Semantics

`SimulatedLocationProvider::advance()` computes displacement from:

- speed in cm/s,
- heading in centi-degrees,
- elapsed delta in ms.

Heading conventions:

- `0` cdeg: north,
- `9000` cdeg: east.

The implementation decomposes movement into north/east components and converts
back to latitude/longitude delta using a spherical Earth approximation.

### 4.3 Waypoint Semantics

Waypoints are discrete state overrides keyed by `at_s`.
When simulation time reaches or exceeds the waypoint timestamp:

- position, speed, and heading are replaced,
- waypoint index advances,
- later waypoints remain pending.

## 5. Radio Channel Model

`SimulatedNetwork` models deterministic in-memory delivery with per-node radio
profiles.

For each transmit call:

1. Validate payload pointer and length bounds.
2. Under mutex, snapshot sender and recipient metadata and locations.
3. Release mutex before callback delivery.
4. For each recipient:
   - compute great-circle distance (`geo::haversine_m`),
   - clamp to minimum distance,
   - compute RSSI via free-space path-loss,
   - drop if below receiver sensitivity,
   - compute SNR (`RSSI - noise floor`),
   - invoke recipient link callback.

Path-loss equation (text form):

`RSSI_dBm = P_tx_dBm - (32.44 + 20*log10(f_MHz) + 20*log10(d_km))`

SNR equation (text form):

`SNR_dB = RSSI_dBm - NoiseFloor_dBm`

## 6. Config Parsing Architecture

`ConfigLoader` uses a strict staged parser to ensure predictable config behavior.

Parsing stages:

1. Preprocess lines
   - remove comments (`# ...`),
   - skip empty lines,
   - validate indentation is multiples of 2 spaces.
2. Parse top-level sections
   - `runtime:`
   - `devices:`
3. Parse typed fields with explicit range checks.
4. Parse nested waypoint lists per device.
5. Sort waypoints by activation timestamp.

Failure behavior is fail-fast with explicit line-number diagnostics.

## 7. Concurrency and Threading

The simulation runtime combines deterministic stepping with asynchronous manager
processing threads.

Thread-safety strategy:

- `SimulatedNetwork` uses a mutex to protect node registry and snapshot creation.
- `SimulatedLinkLayer` protects RX callback installation/read with a mutex.
- `SimulatedLocationProvider` guards kinematic state with a mutex.
- `SimulationClock` uses atomic reads/writes.

Determinism considerations:

- virtual time advances only through explicit `step` calls,
- message processing still uses manager worker threads,
- test helpers should use stepped waits rather than immediate assertions.

## 8. Test Harness Architecture

`SimulationTestBase` encapsulates common integration-test mechanics:

- scenario lifecycle (`start`, `stop`),
- source-node traffic injection (`sendFromDevice` overloads),
- callback capture and message storage by node,
- polling helpers (`waitForMessageCount`, `stepUntil`),
- payload/header assertion helpers.

Phase test executables build on this harness:

- phase 3: baseline connectivity and movement,
- phase 4: relay, TTL, directional, duplicate handling,
- phase 5: stress, repeatability, and timing budget.

## 9. Extension Points

Recommended extension points for future simulation features:

- richer channel effects (collision, fading, stochastic loss),
- configurable asynchronous delay/jitter model,
- scripted mobility model beyond waypoint overrides,
- scenario metrics export for CI dashboards.

## 10. Current Limitations

Known simplifications in current implementation:

- no collision/interference model,
- no random packet-loss model,
- no terrain/obstacle attenuation,
- deterministic path-loss-only propagation,
- strict custom YAML subset rather than full YAML parser.
