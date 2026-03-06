# Network Layer Architecture

This page summarizes the architecture of the `lora_network_layer` component and
how packets move through the system.

## High-Level Components

- **`NetworkManager`**: top-level orchestrator that wires all modules together
  and owns the RX + forwarding FreeRTOS tasks.
- **`LoraLinkAdapter` (`ILinkLayer`)**: link-layer adapter that transmits and
  receives LoRa frames via `lora_radio`.
- **`RoutingEngine`**: stateless decision pipeline for incoming packets
  (duplicate check, TTL/hops, distance, directional propagation).
- **`DuplicateFilter`**: fixed-size LRU cache keyed by `message_id`.
- **`ForwardingQueue`**: hold-back relay queue with implicit cancellation logic.
- **`ILocationProvider`**: abstraction used for local position/speed/heading.

## Data Model

The over-the-air network metadata is represented by `NetworkHeader`
(`components/lora_network_layer/include/network_header.h`), which carries:

- message identity (`message_id`)
- forwarding controls (`hops_remaining`, `lifetime_s`, `max_distance_m`)
- propagation mode (`OMNI` / `DIRECTIONAL`)
- origin and transmitter geo-state (position, speed, heading)

## RX Path

1. The link layer invokes the registered RX handler.
2. `NetworkManager` enqueues the frame to its RX task queue.
3. RX task parses `NetworkHeader` and runs `RoutingEngine::evaluate(...)`.
4. Based on verdict:
   - `DROP`: discard
   - `DELIVER_ONLY`: deliver payload to app callback
   - `DELIVER_AND_FORWARD`: deliver + schedule in `ForwardingQueue`

## TX / Origination Path

1. Application calls `NetworkManager::sendMessage(...)`.
2. Manager creates a new `NetworkHeader` from local state and config.
3. Frame is sent immediately through `ILinkLayer::send(...)`.
4. `message_id` is marked in `DuplicateFilter` to avoid self re-processing.

## Forwarding Path

1. Forwarding task periodically calls `ForwardingQueue::processTick()`.
2. Entries whose hold-back timer expired are relayed.
3. If a duplicate is heard from a node that already progressed propagation,
   queued entries can be implicitly canceled (`onDuplicateHeard`).
