## 1. Simulation Design Objectives and Scope
- Define why host simulation is used: deterministic, repeatable validation of the network layer without hardware timing noise.
- Clarify the boundary: simulation validates network-layer behavior via real `NetworkManager` instances and host adapters.
- State that the section focuses on design choices and observed test outcomes rather than theoretical LoRa PHY modeling completeness.
- Evidence: `simulation/include/simulation_docs.h`, `simulation/include/simulation_builder.h`, `simulation/src/simulation_builder.cpp`.

## 2. Runtime Architecture and Deterministic Execution Model
### 2.1 Scenario Composition and Node Instantiation
- Explain `SimulationBuilder`/`SimulationScenario` composition and ownership of node runtimes.
- Describe per-node embedding chain (`SimulatedLocationProvider` + `SimulatedLinkLayer` + `NetworkManager`).
- Highlight one shared `SimulatedNetwork` channel and shared `SimulationClock` across nodes.
- Evidence: `simulation/include/simulation_builder.h`, `simulation/src/simulation_builder.cpp`, `simulation/include/simulated_link_layer.h`.

### 2.2 Virtual Clock and Event Scheduling Semantics
- Describe explicit virtual time progression (`reset`, `step`) and separation from wall-clock time.
- Describe event ordering keys (`time_us`, `priority`, `sequence_id`) and deterministic tie-breaking.
- Document scenario step order: process due events → advance clock/kinematics → apply waypoints → process to new horizon.
- Evidence: `simulation/include/simulation_clock.h`, `simulation/src/simulation_clock.cpp`, `simulation/include/simulation_event_queue.h`, `simulation/src/simulation_builder.cpp`.

### 2.3 Configuration and Reproducibility Controls
- Describe scenario configuration model (`SimulationConfig`, runtime/device/waypoint structs).
- Describe strict parser validation and normalized waypoint ordering.
- Describe seeded pseudo-random controls used for deterministic stochastic models.
- Evidence: `simulation/include/simulation_config.h`, `simulation/include/config_loader.h`, `simulation/src/config_loader.cpp`, `simulation/src/simulated_network.cpp`.

## 3. Channel and Mobility Modeling
### 3.1 Channel Propagation and Reception Decisions
- Describe free-space path-loss RSSI model and SNR derivation.
- Explain receiver sensitivity gating and per-recipient delivery decisions.
- Explain destination filtering for unicast/broadcast fan-out.
- Evidence: `simulation/include/simulated_network.h`, `simulation/src/simulated_network.cpp` (`computeRssiDbm`, `computeSnrDb`, `executeDeliveryEvent`).

### 3.2 Timed MAC Behavior, Collisions, and Retransmission
- Describe event-driven transmit lifecycle (`TxStart`, `TxEnd`, `RetryBackoff`, `RxDelivery`).
- Explain busy-channel detection, contention window evolution, and retry scheduling.
- Explain collision marking across overlapping active transmissions and resulting failure accounting.
- Evidence: `simulation/include/simulation_event_queue.h`, `simulation/src/simulated_network.cpp` (`executeTxStartEvent`, `executeRetryBackoffEvent`, `executeTxEndEvent`).

### 3.3 Impairment and Congestion Extensions
- Describe PER model modes (disabled/threshold/logistic) and decision points.
- Describe deterministic fading/noise-jitter sampling keyed by seed and event identity.
- Describe utilization-threshold congestion-drop mechanism and applicability window.
- Evidence: `simulation/include/simulation_config.h`, `simulation/src/simulated_network.cpp` (`evaluatePerSuccess`, `sampleFadingDb`, `sampleNoiseJitterDb`, `shouldDropForCongestion`).

### 3.4 Mobility and Topology Evolution
- Describe deterministic kinematics integration from speed/heading and timestamp evolution.
- Describe waypoint-triggered state overrides and when they are applied in stepping.
- Explain how these mechanisms generate dynamic connectivity scenarios for tests.
- Evidence: `simulation/include/simulated_location_provider.h`, `simulation/src/simulated_location_provider.cpp`, `simulation/src/simulation_builder.cpp` (`applyDueWaypoints`).

## 4. Methodology for Network-Layer Validation
### 4.1 Test Harness and Measurement Strategy
- Describe callback-based capture of delivered headers/payloads per node.
- Describe stepped wait predicates (`waitForMessageCount`, `stepUntil`) used for deterministic temporal assertions.
- Describe use of metrics snapshots alongside content-level assertions.
- Evidence: `simulation/tests/include/simulation_test_base.h`, `simulation/tests/src/simulation_test_base.cpp`, `simulation/include/simulation_metrics.h`.

### 4.2 Scenario Classes and Coverage Matrix
- Group tests by purpose: channel/runtime mechanics, forwarding semantics, scalability/repeatability.
- Explain parameterized setups (power/sensitivity, hops, TTL, directional mode, PER/congestion flags).
- Link each class to specific runtime mechanisms and network-layer features exercised.
- Evidence: `simulation/tests/src/test_simulation_phase3.cpp`, `simulation/tests/src/test_simulation_phase4.cpp`, `simulation/tests/src/test_simulation_phase5.cpp`.

## 5. Results by Validation Theme
### 5.1 Baseline Delivery and Reachability Results
- Summarize successful baseline fan-out and payload integrity in close-range scenarios.
- Summarize distance/sensitivity filtering outcomes for unreachable receivers.
- Summarize waypoint-driven transition from non-connectivity to connectivity.
- Evidence: phase 3 tests `testBroadcastDelivery`, `testDistanceBasedReachability`, `testWaypointMovementChangesConnectivity`.

### 5.2 Temporal and Channel-Access Behavior Results
- Summarize requirement for virtual-time progression before delivery in timed mode.
- Summarize deferred second transmission behavior on busy channel and retransmission metric increments.
- Summarize collision-case outcomes and collision-failure metrics.
- Evidence: phase 3 tests `testTimedDeliveryRequiresProgressedVirtualTime`, `testBusyChannelDefersSecondTransmission`, `testCollisionModelDropsSimultaneousTransmissions`.

### 5.3 Propagation/Impairment/Congestion Results
- Summarize near-vs-far propagation delay ordering behavior.
- Summarize PER threshold pass/fail behavior and `tx_fail_per` updates.
- Summarize congestion-drop forced-drop and light-load no-drop behavior under threshold gating.
- Evidence: phase 3 tests `testPropagationDelayDeliversNearBeforeFar`, `testPerThresholdModelDropAndAllow`, `testCongestionDropModeCanForceDrops`; phase 5 `testCongestionThresholdPreventsDropsUnderLightLoad`.

### 5.4 Network-Layer Forwarding Semantics Results
- Summarize TTL expiry effect on delayed forwarded frames.
- Summarize directional forwarding path behavior and relay transmitter metadata stamping.
- Summarize multi-hop forwarding reach extension and duplicate suppression outcomes.
- Evidence: phase 4 tests `testTtlExpiryDropsDelayedForward`, `testDirectionalConeForwardingPath`, `testMultiHopRelayToUnreachableNode`, `testDuplicateSuppressionAtReceiver`.

### 5.5 Determinism and Scalability Results
- Summarize repeatability under identical deterministic workloads via stable message-count/hash snapshots.
- Summarize seeded stochastic PER reproducibility (same seed stable, different seed diverges).
- Summarize large-scale delivery and runtime budget behavior (80-node burst, 100-node step budget).
- Evidence: phase 5 tests `testRepeatabilitySnapshotStableAcrossRuns`, `testStochasticPerSameSeedIsRepeatable`, `testStochasticPerDifferentSeedsDiverge`, `testLargeScaleBroadcastBurstNoLoss`, `testHundredNodeStepBudget`.

## 6. Interpretation and Limitations
- Interpret how observed outcomes support correctness of forwarding logic under varied channel and mobility conditions.
- Distinguish deterministic guarantees from intentionally stochastic behavior controlled by seeds.
- Note model simplifications and compatibility mode constraints that frame result interpretation.
- Evidence: `simulation/src/simulated_network.cpp`, `simulation/include/simulated_network.h`, phase 3/4/5 tests.

## 7. Summary of Validation Confidence
- Conclude with consolidated confidence statement tied to breadth of scenario classes.
- Highlight that tests verify both functional semantics and runtime properties (timing, metrics, repeatability, scale).
- State remaining validation frontier as future extensions of channel realism or additional adversarial scenarios.
- Evidence: aggregate of all simulation test sources and runtime components listed above.
