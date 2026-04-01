# V2X Network Layer

Welcome to the API documentation for the **V2X LoRa multi-hop network layer**.

This ESP-IDF component provides the core network-layer logic for forwarding and
delivering V2X messages over LoRa, including:

- network packet/header definitions
- routing and forwarding decisions
- duplicate suppression
- integration points to link-layer radio and location providers

## Getting Started

Start by browsing the classes in the `components/lora_network_layer/include`
directory:

- `NetworkManager`
- `RoutingEngine`
- `NetworkHeader`
- `LoraLinkAdapter`

Also see the [Network Layer Architecture](architecture.md) page for a
high-level module and data-flow overview.

For host-side simulation internals and APIs, see the
[Host Simulation Architecture](../simulation/docs/simulation_architecture.md) page.

## Notes

This page is configured as the Doxygen main page via
`USE_MDFILE_AS_MAINPAGE = mainpage.md`.
