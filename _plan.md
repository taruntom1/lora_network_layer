# Architecture Write-up Plan

## 1) Planned Sections

1. **Architectural Scope and Layer Boundaries**  
   Covers repository scope, role of this network layer, and interfaces to application/link layers.  
   Draws from: `network_manager.h`, `link_layer_interface.h`, `location_provider.h`, `lora_link_adapter.h`.

2. **Network Frame Model and Message Semantics**  
   Describes `NetworkHeader`, payload constraints, propagation controls, and field semantics across hops.  
   Draws from: `network_header.h`, `network_manager.cpp`, `forwarding_queue.cpp`.

3. **Core Runtime Components and Responsibilities**  
   Defines responsibilities and interactions of `NetworkManager`, `RoutingEngine`, `DuplicateFilter`, `ForwardingQueue`, and geo utilities.  
   Draws from: component headers and source files under `components/lora_network_layer/include` and `components/lora_network_layer/src`.

4. **End-to-End Data Flow**  
   Explains originating TX flow, RX processing flow, delivery path, and forwarding scheduling path.  
   Draws from: `network_manager.cpp`, `routing_engine.cpp`, `forwarding_queue.cpp`.

5. **Routing Decision Pipeline**  
   Details evaluation order: TTL, duplicate, hop budget, max distance, directional cone, hold-back computation.  
   Draws from: `routing_engine.cpp`, `routing_engine.h`, `geo_utils.cpp`.

6. **Forwarding Strategy and Redundancy Control**  
   Covers hold-back queues, priority-aware eviction, timer firing, hop decrement, and implicit cancellation via betweenness.  
   Draws from: `forwarding_queue.cpp`, `forwarding_queue.h`, `geo_utils.cpp`.

7. **Concurrency, Threading, and Synchronization**  
   Documents RX callback decoupling, worker threads, queueing, mutex/CV use, and lifecycle behavior (`start`/`stop`).  
   Draws from: `network_manager.h`, `network_manager.cpp`, `duplicate_filter.cpp`, `forwarding_queue.cpp`.

8. **Configuration and Operational Constraints**  
   Summarizes runtime configuration (`NetworkConfig`), compile-time defaults/macros, and size limits impacting behavior.  
   Draws from: `network_manager.h`, `network_manager.cpp`, `forwarding_queue.h/.cpp`, `routing_engine.cpp`, `network_header.h`.

9. **Simulation Integration Architecture (Host-Side Validation Runtime)**  
   Describes how the same network core is embedded into deterministic host simulation: scenario builder, virtual time, simulated link/network/location, event queue, and metrics.  
   Draws from: `simulation/include/*.h`, `simulation/src/*.cpp`, `simulation/CMakeLists.txt`.

10. **Simulation Configuration and Channel/Event Models**  
    Explains strict config parsing, node/radio/waypoint model, and channel behavior options (immediate delivery, MAC events, PER, collision, congestion).  
    Draws from: `config_loader.h/.cpp`, `simulation_config.h`, `simulated_network.h/.cpp`, `simulation_event_queue.h/.cpp`.

## 2) Planned Diagrams

- **Diagram A — Layer Boundary and Dependency View** (Section 1)  
  Mermaid component/class-style diagram showing Application ↔ NetworkManager ↔ ILinkLayer/ILocationProvider plus concrete adapters.  
  Sources: `network_manager.h`, `link_layer_interface.h`, `location_provider.h`, `lora_link_adapter.h`, simulation adapters.

- **Diagram B — Outbound Message Sequence** (Section 4)  
  Mermaid sequence diagram for `sendMessage` path from application call to broadcast link send.  
  Sources: `network_manager.cpp`.

- **Diagram C — Inbound RX + Routing + Forwarding Sequence** (Section 4)  
  Mermaid sequence diagram for RX callback enqueue, routing evaluation, app delivery, and optional forwarding queue enqueue.  
  Sources: `network_manager.cpp`, `routing_engine.cpp`, `forwarding_queue.cpp`.

- **Diagram D — Routing Decision Flowchart** (Section 5)  
  Mermaid flowchart showing exact evaluation gates and verdict outputs.  
  Sources: `routing_engine.cpp`.

- **Diagram E — Forwarding Queue Lifecycle** (Section 6)  
  Mermaid flow/state diagram for enqueue, full-queue eviction policy, timer firing, and implicit cancellation on duplicate heard.  
  Sources: `forwarding_queue.cpp`.

- **Diagram F — Threading and Synchronization View** (Section 7)  
  Mermaid diagram showing RX callback context, RX worker thread, forwarding worker thread, shared queues/locks.  
  Sources: `network_manager.cpp`, `network_manager.h`.

- **Diagram G — Simulation Runtime Composition** (Section 9)  
  Mermaid diagram showing `SimulationScenario` ownership and per-node runtime objects around shared `SimulatedNetwork`.  
  Sources: `simulation_builder.h/.cpp`, `simulated_network.h`.

- **Diagram H — Simulation Event Processing Pipeline** (Section 10)  
  Mermaid flow/sequence diagram for `transmit` enqueue and event handling (`TxStart`, `RetryBackoff`, `TxEnd`, `RxDelivery`).  
  Sources: `simulated_network.cpp`, `simulation_event_queue.cpp`.
