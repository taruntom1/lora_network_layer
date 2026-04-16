## 1. Simulation Design Objectives and Scope

The host simulation subsystem is designed as a deterministic runtime for validating the LoRa network layer without dependence on ESP-IDF hardware timing behavior. Its stated purpose is to compose portable network-layer components with host-side adapters, so identical forwarding logic can be exercised under controlled channel, time, and mobility conditions. This design makes simulation a verification environment rather than an alternate protocol implementation.

Scope-wise, the simulation targets both framework-level transport behavior and network-layer forwarding semantics. The runtime models virtual time progression, node kinematics, channel fan-out, and configurable impairments, while each node still executes the same `NetworkManager` pipeline used by the network layer itself. As a result, observed outcomes directly reflect interactions among duplicate suppression, relay scheduling, and routing decisions under reproducible scenarios.

The test suite is organized in phases that progressively extend validation breadth: baseline channel correctness and timing behavior, forwarding-semantic checks (TTL, directional forwarding, multi-hop, duplicate handling), and large-scale determinism/performance stress. Accordingly, this report section presents simulation design first, then methodology, then results grouped by behavioral theme, and finally interpretation and limitations.

## 2. Runtime Architecture and Deterministic Execution Model

### 2.1 Scenario Composition and Node Instantiation
The runtime is assembled by `SimulationScenario`, which owns one shared virtual clock, one shared channel model, and an indexed collection of per-node runtimes. For each configured device, construction follows a fixed dependency order: create a `SimulatedLocationProvider`, create a `SimulatedLinkLayer`, register the node in `SimulatedNetwork`, and then instantiate a `NetworkManager` with the configured network-layer capacities. This wiring ensures each node runs the production network-layer pipeline while exchanging frames through host adapters instead of hardware drivers.

The scenario constructor also validates node identity uniqueness and normalizes waypoint order before runtime execution begins. Lifecycle control is explicit and idempotent (`start`, `stop`), and start/stop operations simply fan out to all node-local `NetworkManager` instances. Consequently, simulation orchestration remains centralized while protocol behavior remains distributed across node managers.

### 2.2 Virtual Clock and Event Scheduling Semantics
Time in the simulation is virtual and monotonic. `SimulationClock` exposes only `reset`, `step`, and reads, and does not depend on host wall time. Scenario progression therefore occurs only when test code or a driver explicitly calls `step(delta_ms)`, which eliminates host scheduling jitter from causal ordering of events.

Channel-side events are scheduled in microseconds via `SimulationEventQueue`, which uses a deterministic comparator: earlier `time_us` first; at equal timestamp, lower `priority` first; at equal timestamp and priority, lower `sequence_id` first. This stable ordering is essential when multiple transmissions, retries, and deliveries are due in the same horizon.

`SimulationScenario::step` applies a two-boundary processing model: process all due channel events at the pre-step time, advance clock and node kinematics, apply any due waypoints, then process all due events up to the post-step horizon. This sequencing creates deterministic integration between mobility updates and channel event execution.

### 2.3 Configuration and Reproducibility Controls
Scenario definition is explicit in `SimulationConfig`, split into runtime parameters and device definitions. Runtime fields include channel timing parameters, collision/PER/congestion toggles, random seed, and shared network-layer capacities (`NetworkConfig`). Device entries define node id, initial position/kinematics, radio profile, and optional waypoint schedules.

For file-driven scenarios, `ConfigLoader` enforces a strict YAML-like grammar with line-level diagnostics. The parser rejects malformed indentation, unknown keys, missing top-level sections, and out-of-range conversions, and it sorts waypoint entries by activation time. This strictness reduces ambiguity in scenario interpretation and prevents silent configuration drift across runs.

Reproducibility is further controlled by deterministic pseudo-random sampling in the channel model. Functions such as fading, noise jitter, and logistic-PER outcomes are keyed by `random_seed` and event/node identifiers, so identical seeds and workloads reproduce identical stochastic outcomes, while seed changes intentionally alter those outcomes in a controlled way.

## 3. Channel and Mobility Modeling

### 3.1 Channel Propagation and Reception Decisions
The channel model is implemented by `SimulatedNetwork`, which stores per-node link/location pointers and a per-node radio profile (`tx_power_dbm`, `noise_floor_dbm`, `sensitivity_dbm`). During delivery, sender and receiver states are snapshotted, inter-node distance is computed from geographic coordinates, and free-space path-loss is applied. The implementation follows the stated model `RSSI = P_tx - (32.44 + 20 log10(f_MHz) + 20 log10(d_km))`, then derives `SNR = RSSI - noise_floor`.

Reception is admitted only if the computed RSSI meets receiver sensitivity. Destination filtering is performed before propagation checks: the source node is excluded, and non-broadcast transmissions are delivered only to the addressed node id. In immediate-compatibility mode, these checks happen in a single delivery event at the current virtual time; in timed mode, they occur after transmission lifecycle processing.

### 3.2 Timed MAC Behavior, Collisions, and Retransmission
In timed mode, transmission is represented as event chains (`TxStart`, `TxEnd`, `RetryBackoff`, `RxDelivery`). `TxStart` computes on-air duration from payload size and configured data rate, checks current channel occupancy, and either starts transmission or schedules deferred retry according to DIFS and contention-window backoff. Contention windows are node-local, expanded on deferral up to `cw_max`, and reset after successful transmission start.

If collision modeling is enabled, overlapping active transmissions are marked collided, including previously active transmissions that overlap the new one. Delivery events associated with collided transmissions are then resolved as failures unless another independent success condition applies. Retransmission attempts are explicitly counted, and per-transmission state tracks pending recipient deliveries, collision status, and final success/failure consolidation.

Propagation delay is modeled per recipient in timed mode as `distance / c`, clamped by `propagation_min_delay_us`, and added to `tx_end_us`. This creates physically ordered arrivals across receivers at different distances while remaining deterministic under fixed inputs.

### 3.3 Impairment and Congestion Extensions
Beyond RSSI/sensitivity gating, the channel model supports optional PER decisions. Threshold mode applies a deterministic `snr >= threshold` rule; logistic mode samples success probabilistically from a logistic function of SNR. The logistic draw is deterministic for a fixed seed and event identity, because pseudo-random values are generated by a seeded mixing function keyed by transmission and destination identifiers.

Additional impairment knobs include fading and noise jitter terms applied to RSSI before PER evaluation. Fading uses a deterministic approximation to a normal random variable (sum-of-uniforms transform), while noise jitter applies deterministic signed uniform perturbation. Both are disabled when their configured amplitudes are zero.

Congestion drop mode introduces utilization-aware suppression after PER checks. If enabled, the model estimates channel utilization from accumulated busy time over elapsed simulation time and applies probabilistic drops only when utilization exceeds a configured threshold and minimum elapsed interval. This allows controlled stress testing of higher-load regimes without altering core forwarding logic.

### 3.4 Mobility and Topology Evolution
Mobility is modeled by `SimulatedLocationProvider`, which advances position deterministically from speed and heading over virtual-time steps. The update decomposes displacement into north/east components and converts back to latitude/longitude deltas using a spherical-Earth approximation; timestamp progression is maintained at second granularity via sub-second accumulation.

Scenario-level waypoint logic overlays this continuous kinematics model. At each step, `SimulationScenario` applies all waypoints whose activation time is due, atomically replacing node kinematic state. Because waypoint application is integrated into the deterministic step sequence, topology transitions (e.g., out-of-range to in-range) occur at reproducible simulation times and can be asserted directly in tests.

## 4. Methodology for Network-Layer Validation

### 4.1 Test Harness and Measurement Strategy
Validation is executed through a reusable harness (`SimulationTestBase`) that wraps a fully built `SimulationScenario`. The harness installs per-node application callbacks on each `NetworkManager`, captures delivered `NetworkHeader` and payload bytes into thread-safe buffers, and exposes helper queries for message counts, payload equality, and full per-node delivery traces. This capture path ensures assertions are made at the same API boundary used by real applications.

Temporal assertions use stepped waiting primitives (`waitForMessageCount`, `stepUntil`) that repeatedly advance virtual time in bounded increments until predicates hold or timeouts expire. Because progression is explicit, tests can assert both positive and negative timing properties (e.g., no delivery before sufficient virtual time elapses) without dependence on real-time sleep accuracy.

In addition to message-level assertions, tests consume channel metrics snapshots from the scenario (`SimulationMetricsSnapshot`). The metric set includes transmission attempts/successes, collision and PER failures, retransmissions, delivered/dropped receives, average latency, and channel utilization. This dual strategy—content/structure checks plus aggregate counters—provides both functional and system-level validation evidence.

### 4.2 Scenario Classes and Coverage Matrix
The methodology is phase-structured. Phase 3 targets foundational simulation behavior: broadcast fan-out, distance reachability, waypoint-driven connectivity change, timed delivery semantics, busy-channel retransmission behavior, collision outcomes, propagation-delay ordering, PER threshold behavior, utilization reporting, and forced congestion drops.

Phase 4 targets network-layer semantics under simulation execution: TTL expiry handling, directional forwarding effects, multi-hop relay reach extension, and duplicate suppression at receivers. These tests validate that the network-layer forwarding pipeline remains correct when driven by simulated channel/mobility conditions rather than direct unit inputs.

Phase 5 targets scale and reproducibility: large-node burst delivery, deterministic snapshot stability across repeated runs, bounded runtime for a 100-node stepping workload, and seeded stochastic PER behavior (same-seed repeatability versus cross-seed divergence). Together, the three phases provide broad coverage spanning correctness, timing, resilience, determinism, and practical runtime behavior.

## 5. Results by Validation Theme

### 5.1 Baseline Delivery and Reachability Results
Baseline scenarios confirm correct broadcast fan-out and payload integrity across multiple receivers. In `testBroadcastDelivery`, both non-origin receivers obtain the transmitted payload, while the sender correctly records zero self-deliveries, matching duplicate pre-marking behavior in the network layer.

Reachability behavior tracks configured radio conditions. `testDistanceBasedReachability` shows that a near node receives while a far node remains unreached under the chosen sensitivity configuration. `testWaypointMovementChangesConnectivity` further shows dynamic topology sensitivity: a node initially out of range receives no traffic, then receives correctly after a scheduled waypoint moves it into effective range.

### 5.2 Temporal and Channel-Access Behavior Results
Timed-mode tests demonstrate that delivery is causally tied to virtual-time advancement. `testTimedDeliveryRequiresProgressedVirtualTime` verifies that frames are not delivered immediately after send in timed mode and only appear after sufficient stepped time to cover on-air duration.

Contention behavior is validated in `testBusyChannelDefersSecondTransmission`, where a second transmission from the same source is deferred while the channel is busy and delivered later; the retransmission counter increases accordingly. Collision behavior is validated in `testCollisionModelDropsSimultaneousTransmissions`, where overlapping sends from two sources produce zero delivery at the receiver and increment collision-related failure/drop metrics.

### 5.3 Propagation/Impairment/Congestion Results
Propagation-delay ordering is verified in `testPropagationDelayDeliversNearBeforeFar`: the near receiver obtains the frame first while a far receiver remains pending, then receives later as expected from distance-based delay scheduling.

PER threshold behavior is validated in both directions by `testPerThresholdModelDropAndAllow`: an unrealistically high threshold suppresses delivery and increments `tx_fail_per`, while a lower threshold restores delivery. Congestion behavior is exercised in `testCongestionDropModeCanForceDrops`, where forced-drop settings suppress reception and increment dropped-receive counters. Complementarily, phase 5’s `testCongestionThresholdPreventsDropsUnderLightLoad` shows that under light utilization below threshold, full delivery is preserved despite congestion-drop mode being enabled.

### 5.4 Network-Layer Forwarding Semantics Results
Phase 4 directly validates forwarding semantics under simulated channel dynamics. In `testTtlExpiryDropsDelayedForward`, a relay receives the original frame, but downstream delivery is prevented after virtual-time advancement causes forwarded copies to exceed lifetime constraints.

Directional propagation behavior is verified in `testDirectionalConeForwardingPath`: an in-cone relay receives and forwards, an out-of-cone node may still deliver locally, and a farther node is reached through relay progression. Header inspection confirms relay stamping, as the destination-observed `txPoint` matches the relay node’s location.

`testMultiHopRelayToUnreachableNode` demonstrates reach extension by relay, including expected decrement of `hops_remaining` and relay-origin transmitter metadata at the destination. `testDuplicateSuppressionAtReceiver` confirms that a receiver exposed to direct plus relayed copies still delivers exactly once at application level.

### 5.5 Determinism and Scalability Results
Large-scale stress behavior is exercised by `testLargeScaleBroadcastBurstNoLoss`, where an 80-node scenario receives all 25 broadcast messages at every non-source node. `testHundredNodeStepBudget` further shows that a 100-node, 1000-step workload meets the configured runtime budget while still delivering expected probe-node traffic.

Determinism is validated by `testRepeatabilitySnapshotStableAcrossRuns`, which compares per-node message-count/hash snapshots across repeated workloads and observes exact equality. For stochastic channels, phase 5 splits expectations: `testStochasticPerSameSeedIsRepeatable` confirms reproducibility with fixed seed, while `testStochasticPerDifferentSeedsDiverge` confirms controlled variability across different seeds. This combination indicates deterministic execution with intentionally parameterized stochastic branches.

## 6. Interpretation and Limitations

The collected outcomes indicate that the simulation framework is effective for validating network-layer behavior under controlled channel, timing, and mobility conditions. In particular, tests show that forwarding semantics (TTL, directional relaying, hop updates, duplicate suppression) remain consistent when the network layer is exercised through realistic frame transport and asynchronous node execution rather than direct unit-level injection.

A central strength is deterministic replay: virtual time progression is explicit, event ordering is stable, and stochastic channel effects are seed-controlled. This enables both strict repeatability claims (same-seed stability) and intentional variability studies (different-seed divergence) without changing core code paths. The framework therefore supports both regression-style verification and sensitivity exploration.

The results should be interpreted with model-scope awareness. Propagation is based on free-space loss plus configurable impairments rather than a full empirical urban channel model; MAC behavior is represented by simplified event-level contention/backoff logic; and compatibility immediate-delivery mode intentionally bypasses timed transmission sequencing for legacy behavior. Accordingly, the reported results provide high confidence in protocol-logic correctness within the implemented simulation assumptions, but do not claim full physical-layer fidelity for all deployment environments.

## 7. Summary of Validation Confidence

Across phase 3, phase 4, and phase 5 test suites, the simulation framework demonstrates broad validation coverage for both simulation-runtime behavior and network-layer forwarding semantics. The combined evidence supports confidence in deterministic execution, correct multi-hop forwarding decisions, and stable behavior under both nominal and stressed operating conditions.

Confidence is strengthened by complementary assertion modes: direct payload/header checks, timing-sensitive stepped assertions, and aggregate metrics checks for retransmission, collision, PER, drop, latency, and utilization behavior. The inclusion of repeatability and stochastic-seed tests further shows that the framework can support rigorous regression while still modeling controlled randomness when required.

Overall, the implemented simulation design and observed test outcomes provide a credible basis for claiming that the network layer has been validated for deterministic correctness, channel-interaction robustness, and practical scalability within the implemented model assumptions.
