# Network Layer

## Purpose
- Provide a deterministic, testable network-layer service for LoRa V2V message dissemination using controlled multi-hop broadcast.
- Separate network logic from radio hardware via clear link and location interfaces so the same core logic runs on embedded targets and simulation.
- Balance reachability and channel load using duplicate suppression, directional filtering, and contention-aware deferred relaying.

## Key Components
- **Frame definition (`network_header.h`)**: fixed 45-byte packed `NetworkHeader` plus app payload (`NET_MAX_APP_PAYLOAD`), with origin/last-transmitter metadata and propagation controls.
- **Boundaries/abstractions**:
  - `ILinkLayer` (`link_layer_interface.h`) for send, receive callback registration, and node identity.
  - `ILocationProvider` (`location_provider.h`) for position, speed, heading, and timestamp.
- **Core stateful services**:
  - `DuplicateFilter` (`duplicate_filter.*`): mutex-protected fixed-size LRU cache keyed by `message_id`.
  - `ForwardingQueue` (`forwarding_queue.*`): pending relay slots with per-message holdback timer and implicit cancellation.
- **Core decision logic**:
  - `RoutingEngine` (`routing_engine.*`): evaluates received frames and computes forwarding holdback.
- **Coordinator/runtime**:
  - `NetworkManager` (`network_manager.*`): wires all components, owns queues/threads, handles message origination and inbound processing.
- **Hardware adapter**:
  - `LoraLinkAdapter` (`lora_link_adapter.*`): bridges `LoraRadio` callbacks/sends to `ILinkLayer` semantics and filters by `NET_MSG_TYPE`.

## Data Structures and Message Identity
- `NetworkHeader` fields include:
  - Identity/priority/time: `message_id`, `priority`, `timestamp`.
  - Origin kinematics: `origin_lat/lon`, `origin_speed`, `origin_heading`.
  - Current transmitter kinematics (updated each hop): `tx_lat/lon`, `tx_speed`, `tx_heading`.
  - Propagation controls: `prop_mode`, `target_heading`, `hops_remaining`, `max_distance_m`, `lifetime_s`.
- Message identity is `message_id = (origin_nodeId << 16) | sequence` in `NetworkManager::sendMessage`.
- `sendMessage` marks newly originated `message_id` in `DuplicateFilter` before transmission to prevent self-processing on hearback.
- RX queue item is `RxEvent` (raw bytes + len + RSSI/SNR), buffering from link callback into RX thread.

## Forwarding Pipeline (Receive Path)
1. **Frame intake and buffering** (`NetworkManager::start` lambda): copy inbound frame into bounded `rx_queue_`; drop on queue overflow.
2. **RX thread parse** (`rxTaskLoop`): ignore runt frames, deserialize `NetworkHeader`, split payload.
3. **Implicit cancellation pre-check** (`fwd_queue_.onDuplicateHeard`): if a duplicate was heard from farther progression and this node is “between,” cancel queued relay.
4. **Routing evaluation** (`routing_.evaluate`): returns `DROP`, `DELIVER_ONLY`, or `DELIVER_AND_FORWARD` + holdback.
5. **Delivery**: if not dropped, invoke application callback with header/payload.
6. **Deferred forwarding scheduling**: for `DELIVER_AND_FORWARD`, enqueue in `ForwardingQueue`; if full and no eligible eviction, drop relay.
7. **Forward thread execution** (`fwdTaskLoop`): ticks queue every ~10 ms and fires expired entries.
8. **Relay transmit** (`ForwardingQueue::fireEntry`): decrement `hops_remaining`, stamp current TX location/speed/heading, serialize and broadcast.

## Suppression and Control Strategies
- **Duplicate suppression**:
  - Hard duplicate drop at routing stage via `DuplicateFilter::isDuplicate` (with LRU refresh).
  - Originated message pre-marking via `markSeen` avoids local re-processing.
- **TTL control**:
  - Drop if `hdr.timestamp + hdr.lifetime_s < now` when both timestamps are known (`!= 0`).
- **Hop limit**:
  - `hops_remaining == 0` yields `DELIVER_ONLY` (no relay).
  - Relay decrements hop count at fire time.
- **Distance bound**:
  - If `max_distance_m > 0` and node is beyond origin radius, `DELIVER_ONLY`.
- **Directional propagation**:
  - For `DIRECTIONAL`, node must be within configured cone around `target_heading`; otherwise `DELIVER_ONLY`.
- **Deferred relay + contention mitigation**:
  - Holdback computed from distance to previous transmitter and signal metrics (RSSI/SNR), then priority-scaled.
  - Higher urgency (lower enum value) gets shorter delay via multipliers.
- **Implicit cancellation**:
  - On hearing same `message_id`, queued relay is canceled if node lies between origin and heard transmitter within threshold; avoids redundant rebroadcast from less-progressive nodes.

## Holdback Computation Details
- Implemented in `RoutingEngine::computeHoldback`.
- Inputs: distance from node to `hdr.txPoint()`, normalized SNR and RSSI.
- Weighted quality score: `0.5*SNR_norm + 0.5*RSSI_norm`.
- Combined factor: `0.7*(1 - dist_ratio) + 0.3*signal_quality`.
- Delay interpolation between `CONFIG_NET_HOLDBACK_MIN_MS` and `CONFIG_NET_HOLDBACK_MAX_MS`.
- Priority multiplier array (`EMERGENCY` fastest, `LOW` slowest) scales final holdback.

## Concurrency and Synchronization Model
- `NetworkManager` owns two worker threads:
  - **RX thread** (`rxTaskLoop`) blocked on condition variable with timed wakeups.
  - **Forwarding thread** (`fwdTaskLoop`) periodic polling/tick every 10 ms.
- RX callback context only enqueues events (short critical section under `rx_queue_mutex_`), then notifies RX thread.
- Application callback assignment/use guarded by `app_cb_mutex_`.
- `DuplicateFilter` uses internal mutex for all public operations.
- `ForwardingQueue` uses internal mutex to protect slot pool and state transitions.
- Stop flow uses atomics (`running_`, `started_`) plus condition-variable notify and thread joins for clean shutdown.

## Configurable Parameters (Compile-time and Runtime)
- Runtime capacities (`NetworkConfig`):
  - `duplicate_cache_size`, `forwarding_queue_size`, `rx_queue_depth`.
- Compile-time defaults/macros:
  - Queue/cache limits: `CONFIG_NET_DUPLICATE_CACHE_SIZE`, `CONFIG_NET_FORWARDING_QUEUE_SIZE`, `CONFIG_NET_RX_QUEUE_DEPTH`.
  - Holdback behavior: `CONFIG_NET_HOLDBACK_MIN_MS`, `CONFIG_NET_HOLDBACK_MAX_MS`, `CONFIG_NET_ESTIMATED_RADIO_RANGE_M`.
  - Directionality and cancellation: `CONFIG_NET_DIRECTIONAL_HALF_ANGLE`, `CONFIG_NET_BETWEENNESS_THRESHOLD_M`.
- Protocol constants:
  - `BROADCAST_ADDR`, `NET_MSG_TYPE`, `LORA_MAX_PAYLOAD`, fixed `NetworkHeader` size.

## Geometric Utilities Used by Routing/Queue Logic
- `geo::haversine_m`: geodesic distance for radius and distance-ratio checks.
- `geo::bearing_deg` + `geo::isInsideCone`: directional eligibility.
- `geo::isBetween`: local-projection betweenness test for implicit cancellation.

## Simulation Framework

### Simulation Architecture
- The simulation is built around `SimulationScenario` (`simulation_builder.*`), which owns:
  - One shared `SimulatedNetwork` channel instance.
  - One shared `SimulationClock` virtual time source.
  - Per-node runtime bundles (`SimulatedLocationProvider`, `SimulatedLinkLayer`, `NetworkManager`).
- `SimulationBuilder` constructs `SimulationConfig` programmatically or from file (`ConfigLoader`) and materializes a scenario with strict validation (e.g., at least one device, unique node IDs).
- Runtime progression is explicit and deterministic via `SimulationScenario::step(delta_ms)` rather than wall-clock-driven scheduling.

### Node Model and Embedding of Network Layer
- Each simulated node is assembled as:
  1. `SimulatedLocationProvider` implementing `ILocationProvider`.
  2. `SimulatedLinkLayer` implementing `ILinkLayer`.
  3. `NetworkManager` instantiated with shared runtime `NetworkConfig`.
- The real network-layer pipeline (duplicate filter, routing engine, forwarding queue, RX/FWD threads) runs unchanged inside every simulated node.
- Node app-level receives are observed in tests through `NetworkManager::setAppRxCallback`, captured by `SimulationTestBase`.

### Channel Model (Transmission, Reception, Impairments)
- `SimulatedNetwork` acts as an in-memory radio channel and event scheduler.
- Delivery path uses free-space path loss and SNR:
  - RSSI from tx power, carrier frequency, and distance (`computeRssiDbm`).
  - SNR from RSSI minus receiver noise floor (`computeSnrDb`).
  - Delivery only if RSSI ≥ receiver sensitivity.
- Two timing modes:
  - **Compatibility immediate delivery**: enqueue RxDelivery and process immediately at current sim time.
  - **Timed/event mode**: explicit `TxStart`, `TxEnd`, `RetryBackoff`, and `RxDelivery` events with microsecond timestamps.
- MAC/contention and impairment modeling (`configureMac`):
  - Busy-channel deferral with DIFS and contention window backoff.
  - Retry limit (`max_retries`), retransmission counting.
  - Optional collision model marking overlapping transmissions as collided.
  - Optional PER models: disabled, threshold, logistic.
  - Optional deterministic fading and noise-jitter perturbations.
  - Optional utilization-threshold-based congestion drops.
- Propagation delay is distance-based using speed-of-light estimate plus configurable minimum delay clamp.

### Clock and Event Model
- `SimulationClock` is a monotonic atomic millisecond clock with explicit reset/step.
- `SimulationEventQueue` is a mutex-protected min-heap ordered by `(time_us, priority, sequence_id)` to ensure deterministic event ordering.
- `SimulationScenario::step` progression order:
  1. Process due channel events at step start timestamp.
  2. Advance global virtual clock by `delta_ms`.
  3. Advance each node’s kinematics (`SimulatedLocationProvider::advance`).
  4. Apply due waypoint overrides (time-indexed state replacements).
  5. Process channel events due up to step end timestamp.
- This ordering provides deterministic interleaving of channel events and mobility updates.

### Mobility and Topology Scenarios
- Mobility is represented by per-node initial kinematics plus optional sorted waypoints (`at_s`, position, speed, heading).
- `applyDueWaypoints` applies all waypoints with activation time <= current scenario second.
- Test topologies include:
  - Small static groups (2–4 nodes) for reachability/path/TTL behavior.
  - Dynamic movement scenarios where waypoints alter connectivity over time.
  - Medium/large grids/rings for stress and repeatability experiments (20, 80, 100 nodes).

### Test Harness and Result Collection
- `SimulationTestBase` provides reusable lifecycle and assertion helpers:
  - start/stop scenario, send messages from specific nodes, step until predicate/timeout.
  - per-node capture buffers of delivered `NetworkHeader` + payload (`CapturedMessage`).
- Result evidence channels:
  - Message delivery counts and payload presence checks.
  - Header-field checks (e.g., relay-updated `txPoint`, decremented `hops_remaining`).
  - Channel/runtime metrics snapshots (`SimulationMetricsSnapshot`): attempts, successes, collision/PER failures, retransmissions, delivered/dropped RX, latency, utilization.

### What the Tests Verify
- **Phase 3 (`test_simulation_phase3.cpp`)** validates simulation-core behavior and timed channel effects:
  - Broadcast fan-out and payload integrity.
  - Distance/sensitivity-based reachability.
  - Mobility waypoint impact on connectivity.
  - Necessity of stepping virtual time for timed delivery.
  - Busy-channel deferral and retransmission accounting.
  - Collision drops for simultaneous transmissions.
  - Propagation-delay ordering (near receiver before far receiver).
  - PER threshold drop/allow behavior.
  - Utilization metric increase with traffic.
  - Congestion-drop mode forcing drops under configured conditions.
- **Phase 4 (`test_simulation_phase4.cpp`)** validates network-layer forwarding semantics inside simulation:
  - TTL expiry prevents stale forwarded delivery.
  - Directional propagation path behavior and relay transmitter stamping.
  - Multi-hop relay to nodes unreachable by direct link.
  - Duplicate suppression at receiver despite multiple relay opportunities.
- **Phase 5 (`test_simulation_phase5.cpp`)** validates scalability, determinism, and stochastic controls:
  - Large-scale burst delivery without loss in configured stress scenario.
  - Snapshot repeatability across identical deterministic runs.
  - Step-time budget under 100-node / 1000-step workload.
  - Stochastic PER reproducibility with same seed and divergence with different seeds.
  - Congestion-threshold behavior under light load (no drops when utilization remains below threshold).
