# Simulation Testing Guide

This guide explains how to use the host simulation to write reliable network-layer tests on a PC (no ESP-IDF runtime required).

## 1. What You Get

The simulation project builds and runs the real network-layer core against host-side adapters:

- Core under test: `NetworkManager`, `RoutingEngine`, `DuplicateFilter`, `ForwardingQueue`.
- Host adapters:
  - `SimulatedLinkLayer` for radio transmission/reception.
  - `SimulatedNetwork` for channel propagation (RSSI/SNR + sensitivity filtering).
  - `SimulatedLocationProvider` for position, heading, speed, and timestamp.
  - `SimulationClock` for deterministic virtual time.
- Test utilities:
  - `SimulationBuilder` and `SimulationScenario` for composing multi-node scenarios.
  - `SimulationTestBase` for callback capture, stepping, and waiting assertions.

## 2. Build and Run

From repository root.

### 2.1 Configure + build tests (MinGW)

```powershell
cmake -S simulation -B simulation/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DSIM_BUILD_TESTS=ON
cmake --build simulation/build --target simulation_phase3_tests simulation_phase4_tests simulation_phase5_tests -j
```

### 2.2 Run tests

```powershell
ctest --test-dir simulation/build --output-on-failure
```

### 2.3 Run one phase only

```powershell
ctest --test-dir simulation/build --output-on-failure -R simulation_phase4_tests
```

## 3. Test Project Layout

- `simulation/include/`:
  - simulation runtime APIs (`SimulationBuilder`, config loader, link/location/network abstractions)
- `simulation/tests/include/simulation_test_base.h`:
  - reusable helpers for writing test cases
- `simulation/tests/src/test_simulation_phase3.cpp`:
  - baseline delivery/reachability/movement examples
- `simulation/tests/src/test_simulation_phase4.cpp`:
  - advanced routing behaviors (TTL, directional, relay, duplicate suppression)
- `simulation/tests/src/test_simulation_phase5.cpp`:
  - stress/performance/repeatability patterns

## 4. Two Ways to Define Scenarios

You can define multi-device scenarios in two styles.

### 4.1 Fluent builder API (best for unit tests)

Use `SimulationBuilder` directly in C++ for compact and explicit test setup.

```cpp
SimulationConfig cfg;
cfg.runtime.carrier_freq_mhz = 868.0f;
cfg.runtime.start_time_s = 1000;
cfg.runtime.network_config = NetworkConfig{64, 8, 16};

SimulationDeviceConfig a;
a.node_id = 0x2001;
a.initial_position = GeoPoint{125000000, 773000000};
a.radio = SimulatedNetwork::RadioConfig{14.0f, -110.0f, -118.0f};

SimulationDeviceConfig b = a;
b.node_id = 0x2002;
b.initial_position = GeoPoint{125005000, 773005000};

SimulationBuilder builder;
builder.setConfig(cfg).addDevice(a).addDevice(b);

std::unique_ptr<SimulationScenario> scenario = builder.build();
```

### 4.2 File-based config (best for reusable scenarios)

Load scenario from YAML-like config file:

```cpp
SimulationBuilder builder;
builder.loadConfigFile("simulation/config/example_network.yml");
auto scenario = builder.build();
```

Schema reference: `simulation/config/simulation_config.schema.yml`

Example: `simulation/config/example_network.yml`

## 5. Config Format Rules (Important)

The config parser is intentionally strict.

- Indentation must be multiples of 2 spaces.
- Comments start with `#`.
- Top-level sections must be exactly:
  - `runtime:`
  - `devices:`
- List items must use `-` syntax.
- Unknown keys are rejected with parse errors.

### 5.1 Runtime keys

- `carrier_freq_mhz` (float)
- `start_time_s` (uint32)
- `duplicate_cache_size` (size_t)
- `forwarding_queue_size` (size_t)
- `rx_queue_depth` (size_t)

### 5.2 Device keys

- `id` or `node_id` (uint16)
- `lat` / `lon` (int32 fixed-point, degrees × 1e7)
- `speed_cm_s` (uint16)
- `heading_cdeg` (uint16, 0.01 degrees)
- `tx_power_dbm` (float)
- `noise_floor_dbm` (float)
- `sensitivity_dbm` (float)
- optional `waypoints:` list

### 5.3 Waypoint keys

- `at_s` (uint32 timestamp seconds)
- `lat` / `lon`
- `speed_cm_s`
- `heading_cdeg`

Waypoints are sorted by `at_s` and applied when simulation time reaches that second.

## 6. Writing a New Test File

Use this workflow.

1. Create test source in `simulation/tests/src/`, for example `test_simulation_custom.cpp`.
2. Add a small assertion helper set (`expectTrue`, `expectEqInt`, etc.).
3. Build a `SimulationConfig` with nodes and radios.
4. Construct `SimulationTestBase` from `builder.build()`.
5. Call `start()`.
6. Trigger traffic using `sendFromDevice(...)`.
7. Advance/wait with `waitForMessageCount(...)` or `stepUntil(...)`.
8. Verify payload/header fields.
9. Call `stop()`.
10. Register executable + CTest entry in `simulation/CMakeLists.txt`.

Minimal skeleton:

```cpp
void testScenario()
{
    SimulationBuilder builder;
    builder.setConfig(makeConfig());

    SimulationTestBase test(builder.build());
    test.start();

    const std::vector<uint8_t> payload = {1,2,3};
    int rc = test.sendFromDevice(0x2001, payload);
    expectEqInt(rc, static_cast<int>(NetworkError::Ok), "send failed");

    expectTrue(test.waitForMessageCount(0x2002, 1, 1500), "receiver timeout");
    expectTrue(test.hasPayload(0x2002, payload), "payload mismatch");

    test.stop();
}
```

## 7. SimulationTestBase API Cheatsheet

From `simulation/tests/include/simulation_test_base.h`:

- `start()` / `stop()`:
  - starts and stops all managers in scenario.
- `sendFromDevice(...)`:
  - sends an app payload from a specific node with full routing controls.
  - overload accepts raw pointer or `std::vector<uint8_t>`.
- `receivedCount(node_id)`:
  - returns number of app-delivered messages captured for a node.
- `receivedMessages(node_id)`:
  - returns captured message list (`header` + `payload`).
- `hasPayload(node_id, payload)`:
  - convenience payload check.
- `waitForMessageCount(node_id, min_count, timeout_ms, step_ms, idle_sleep_ms)`:
  - repeatedly steps simulation and waits until count reached.
- `stepUntil(predicate, timeout_ms, step_ms, idle_sleep_ms)`:
  - generic loop for arbitrary stop conditions.
- `clearCaptured()`:
  - resets captured callback state for next phase in same test.
- `scenario()`:
  - direct access to `SimulationScenario` for low-level stepping and provider inspection.

## 8. Choosing Good Timing Values

Internals to keep in mind:

- `NetworkManager` uses background threads for RX processing and forwarding queue processing.
- Forwarding queue processing ticks every ~10 ms in host runtime loop.
- Because callback delivery is asynchronous, avoid immediate strict assertions right after send.

Recommended defaults:

- `step_ms = 50`
- `idle_sleep_ms = 1..2`
- receive timeout for normal tests: `1000..2000 ms`
- stress tests: `3000..5000 ms`

## 9. Routing Controls in sendFromDevice

`sendFromDevice(...)` supports:

- `priority`: `EMERGENCY`, `HIGH`, `NORMAL`, `LOW`
- `mode`: `OMNI` or `DIRECTIONAL`
- `target_heading` (centi-degrees)
- `max_hops`
- `max_distance_m`
- `lifetime_s`

Use these to target specific behaviors:

- TTL tests: set small `lifetime_s`, then step virtual time forward.
- Relay tests: set `max_hops >= 1` and place nodes to force multi-hop.
- Directional tests: set `mode = DIRECTIONAL` and `target_heading`.
- Duplicate tests: place a direct path and at least one relay path.

## 10. Inspecting Header-Level Outcomes

Captured messages include `NetworkHeader`, so you can verify:

- `message_id` uniqueness/preservation
- `hops_remaining` decremented by relays
- `timestamp` and `lifetime_s`
- `txPoint()` updated at each relay
- `originPoint()` unchanged from source

Example checks used in phase tests:

- relay hop decrement on destination
- destination seeing relay location as transmit point

## 11. Movement and Waypoints

Device movement is kinematics-driven.

- Position advances with each `scenario.step(delta_ms)` using heading + speed.
- Waypoints are applied when clock reaches `at_s`.
- If a waypoint is due, location/speed/heading can jump to the waypoint state.

Pattern for movement tests:

1. Send while target is out of range and assert no receive.
2. Step time beyond waypoint.
3. Clear captures.
4. Send again and assert receive.

## 12. Channel Model Summary

`SimulatedNetwork` currently models:

- free-space path loss (FSPL)
- receiver sensitivity threshold
- SNR from RSSI - noise floor
- broadcast and unicast fan-out

It does not currently model multipath fading, packet collisions, or random loss.

## 13. Common Pitfalls

1. Queue capacity mismatch

`NetworkConfig.forwarding_queue_size` should not exceed compile-time max (`CONFIG_NET_FORWARDING_QUEUE_SIZE`, default 8 in this repo).

2. Payload too large

App payload must be <= `NET_MAX_APP_PAYLOAD`.

3. Not starting scenario

Callbacks and runtime threads are active only after `start()`.

4. Immediate assertions

Use wait/step helpers instead of asserting delivery immediately after send.

5. Config indentation issues

Parser rejects odd indentation and unknown fields.

## 14. Add Your Test to CMake and CTest

In `simulation/CMakeLists.txt`:

1. Add executable:

```cmake
add_executable(simulation_custom_tests
    tests/src/test_simulation_custom.cpp
)

target_link_libraries(simulation_custom_tests PRIVATE
    simulation_test_support
)
```

2. Register in CTest:

```cmake
add_test(
    NAME simulation_custom_tests
    COMMAND simulation_custom_tests
)
```

3. Build and run:

```powershell
cmake --build simulation/build --target simulation_custom_tests -j
ctest --test-dir simulation/build --output-on-failure -R simulation_custom_tests
```

## 15. Suggested Test Matrix

When adding new features, cover at least:

- delivery success/failure boundaries (range, sensitivity)
- routing constraints (TTL, hops, max distance)
- directional behavior
- relay and duplicate suppression
- movement transitions via waypoints
- determinism (same scenario => same result)
- stress envelope (node count, step count, runtime budget)

## 16. Fast Start Checklist

- Build simulation with `SIM_BUILD_TESTS=ON`.
- Copy one existing phase test file as a template.
- Use `SimulationTestBase` helpers (do not reinvent callback capture).
- Keep scenario config explicit and small first.
- Add one behavior per test function.
- Run with CTest filter for fast iteration.
- Scale up only after deterministic pass.
