# Network Layer Memory

## Purpose and Scope

This repository implements the **Network Layer** for a LoRa-based V2V system. The network layer sits between:

- **Application Layer (above)**: provides application payload bytes and receives delivered payloads via callback.
- **Link Layer (below)**: transmits/receives raw frames over LoRa (real radio on ESP32 or simulated channel on host).

Primary responsibilities implemented here:

- Construct and parse network frames.
- Perform duplicate suppression, TTL checks, propagation limits, and directional filtering.
- Deliver valid messages to local application callback.
- Relay eligible messages with hold-back timing and implicit relay cancellation.

---

## Core Network-Layer Data Model

### `NetworkHeader` (`components/lora_network_layer/include/network_header.h`)

- Packed over-the-air header, fixed at **45 bytes**.
- Contains:
  - Message identity: `message_id`.
  - Priority and timestamp.
  - Origin kinematics (`origin_lat/lon/speed/heading`).
  - Last transmitter kinematics (`tx_lat/lon/speed/heading`) updated at each hop.
  - Propagation controls:
    - `prop_mode` (`OMNI`/`DIRECTIONAL`)
    - `target_heading`
    - `hops_remaining`
    - `max_distance_m`
    - `lifetime_s`
- Payload limit:
  - LoRa max payload = 247 bytes.
  - App payload max = `247 - sizeof(NetworkHeader)` = `NET_MAX_APP_PAYLOAD`.

### Geographic Utilities (`geo_utils`)

- Great-circle distance (`haversine_m`).
- Bearing computation (`bearing_deg`).
- Directional cone check (`isInsideCone`).
- Betweenness test (`isBetween`) used by forwarding queue implicit cancellation.

---

## Core Network Runtime Components

### 1) `NetworkManager`

Central orchestrator that wires all subcomponents:

- `ILinkLayer` (send/receive abstraction)
- `ILocationProvider`
- `DuplicateFilter`
- `RoutingEngine`
- `ForwardingQueue`

Runtime behavior:

- Starts two threads:
  - RX processing thread (`rxTaskLoop`)
  - Forwarding queue ticking thread (`fwdTaskLoop`, every ~10 ms)
- Registers link RX callback; callback only enqueues `RxEvent` into bounded RX queue.
- RX thread dequeues events and applies routing pipeline.

Application interface:

- `sendMessage(...)` to originate message.
- `setAppRxCallback(...)` to deliver accepted payloads upward.

Message origin path:

1. Validate payload size.
2. Create `NetworkHeader`.
3. Construct `message_id = (node_id << 16) | seq`.
4. Populate origin + tx fields from `ILocationProvider`.
5. Set propagation controls from arguments.
6. Mark `message_id` as seen in duplicate filter.
7. Serialize header + payload and send broadcast through `ILinkLayer`.

RX path:

1. Copy incoming raw frame into `RxEvent`.
2. Parse `NetworkHeader`.
3. Notify `ForwardingQueue::onDuplicateHeard` (implicit cancellation check).
4. Evaluate header with `RoutingEngine::evaluate`.
5. If verdict is not `DROP`, deliver to app callback.
6. If verdict is `DELIVER_AND_FORWARD`, enqueue relay entry with computed hold-back.

### 2) `DuplicateFilter`

- Fixed-size, mutex-protected cache keyed by `message_id`.
- LRU-like eviction via monotonically increasing tick.
- `isDuplicate(id)`:
  - Returns true if already present.
  - Inserts if absent.
- `markSeen(id)` for locally originated messages to avoid self-reprocessing.

### 3) `RoutingEngine`

Evaluation pipeline for incoming headers:

1. TTL check (`timestamp + lifetime_s < now`) if both timestamps are known.
2. Duplicate check via `DuplicateFilter`.
3. Hop exhaustion check (`hops_remaining == 0`) -> deliver only.
4. Max-distance check against origin (`max_distance_m`) -> deliver only if exceeded.
5. Directional cone check for `DIRECTIONAL` mode -> deliver only if outside cone.
6. Otherwise: deliver and forward with computed hold-back.

Hold-back computation:

- Uses distance from current node to previous transmitter (`hdr.txPoint()`), plus normalized RSSI/SNR quality blend.
- Configurable min/max hold-back and estimated range.
- Priority multiplier shortens hold-back for urgent messages.

### 4) `ForwardingQueue`

- Fixed slot pool (`CONFIG_NET_FORWARDING_QUEUE_SIZE`, default 8).
- Enqueues relay candidates with fire timestamp.
- On full queue:
  - Finds worst (lowest priority) queued entry.
  - Replaces only if incoming message is higher priority.
- `processTick()` fires due entries:
  - Decrement `hops_remaining`.
  - Refresh `tx_*` fields from current location.
  - Broadcast serialized frame through link layer.

Implicit cancellation (`onDuplicateHeard`):

- For queued entry with same `message_id`, if current node is geometrically “between” origin and newly heard transmitter, queued relay is canceled as redundant.

---

## Layer Boundary Abstractions

### Upward boundary

- Application registers callback in `NetworkManager`.
- Callback receives parsed `NetworkHeader` and app payload bytes.

### Downward boundary

- `ILinkLayer` abstracts TX/RX and node ID.
- `ILocationProvider` abstracts location, speed, heading, timestamp source.

### Concrete ESP32 adapter

- `LoraLinkAdapter` adapts external `LoraRadio` to `ILinkLayer`.
- Filters by reserved `NET_MSG_TYPE` before passing frames to network layer.

---

## Concurrency and Threading Model

- Link RX callback is lightweight: copy frame -> enqueue -> notify.
- RX processing occurs on dedicated thread.
- Forwarding timer processing occurs on separate periodic thread.
- Synchronization:
  - RX queue mutex/condition variable.
  - App callback mutex.
  - Duplicate filter internal mutex.
  - Forwarding queue internal mutex.
- Stop behavior:
  - Sets `running_ = false`, wakes RX wait, joins both threads.

---

## Host Simulation Subsystem (same repository)

The repository also contains a host simulation project that exercises the real portable network-layer core.

### Simulation architecture

- `SimulationScenario` owns:
  - `SimulationClock` (deterministic virtual time)
  - `SimulatedNetwork` (in-memory channel model)
  - Per node:
    - `SimulatedLocationProvider`
    - `SimulatedLinkLayer`
    - real `NetworkManager`
- `SimulationBuilder` builds scenario from in-memory config or `ConfigLoader` file parse.

### Simulated channel (`SimulatedNetwork`)

Supports:

- Node registry and per-node radio config.
- RSSI via FSPL model and SNR calculation.
- Broadcast/unicast fan-out.
- Two operating modes:
  - Compatibility immediate delivery.
  - Event-driven MAC timing with event queue:
    - TxStart, RetryBackoff, TxEnd, RxDelivery.
- Optional models/controls:
  - Collision model
  - PER threshold/logistic model
  - Deterministic fading/noise jitter
  - Congestion-based probabilistic drops
- Metrics collection (`SimulationMetricsCollector`) for tx/rx outcomes, latency, utilization.

### Scenario stepping flow

`SimulationScenario::step(delta_ms)`:

1. Process due channel events at step start.
2. Advance virtual clock.
3. Advance each node’s kinematics.
4. Apply due waypoints.
5. Process due channel events at step end.

---

## Configuration and Limits

Runtime network capacities are configurable through `NetworkConfig`:

- duplicate cache size
- forwarding queue size
- RX queue depth

Compile-time defaults/constants in core:

- `CONFIG_NET_DUPLICATE_CACHE_SIZE` default 64
- `CONFIG_NET_FORWARDING_QUEUE_SIZE` default 8
- `CONFIG_NET_RX_QUEUE_DEPTH` default 8
- hold-back range defaults (`CONFIG_NET_HOLDBACK_MIN_MS` 50, `MAX_MS` 500)
- directional half-angle default `4500` centi-degrees (45°)
- betweenness threshold default 100 m

---

## Important Behavioral Observations

- Duplicate suppression is global per node by `message_id`; originated messages are pre-marked seen.
- Delivery can occur even when forwarding is blocked (hop exhausted, outside max distance, outside directional cone).
- Forwarding queue cancellation is opportunistic and geometric, reducing redundant relays.
- RX queue overload causes frame drops with warning log.
- Forwarding queue can preferentially preserve higher-priority traffic under congestion.
- Simulation includes significantly richer channel behavior than bare core assumptions, but still runs the same network-layer code.
