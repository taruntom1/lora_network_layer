# Host Simulation

This folder contains a standalone CMake project for host-side network-layer simulation.

## What is included

- `lora_network_core` static library built from the portable network-layer sources.
- `network_simulation` static library with:
  - `SimulationClock`
  - `SimulatedLocationProvider`
  - `SimulatedLinkLayer`
  - `SimulatedNetwork`
   - `ConfigLoader` (custom YAML subset parser)
   - `SimulationBuilder` / `SimulationScenario`
- `simulation_demo` executable that spins up three virtual nodes and sends a sample message.
- `simulation_phase3_tests` executable with reusable simulation test harness checks.
- `simulation_phase4_tests` executable with advanced routing and relay scenario checks.
- `simulation_phase5_tests` executable with scale, repeatability, and timing budget checks.

## Detailed guide

For a full guide on writing your own simulation tests, see:

- `simulation/TESTING_GUIDE.md`
- `simulation/docs/simulation_architecture.md` (Doxygen architecture and API/internal behavior docs)

## Build with MSVC

From repository root:

1. Configure

   cmake -S simulation -B simulation/build-msvc -G "Visual Studio 17 2022" -A x64

2. Build

   cmake --build simulation/build-msvc --config Debug --target simulation_demo

3. Run

   simulation/build-msvc/Debug/simulation_demo.exe

4. Run with a config file

   simulation/build-msvc/Debug/simulation_demo.exe simulation/config/example_network.yml

## Build with Ninja (fallback)

1. Configure

   cmake -S simulation -B simulation/build -G Ninja

2. Build

   cmake --build simulation/build -j

3. Run

   simulation/build/simulation_demo.exe

4. Run with a config file

   simulation/build/simulation_demo.exe simulation/config/example_network.yml

## Run Phase 3 Tests

1. Configure

   cmake -S simulation -B simulation/build -G Ninja -DSIM_BUILD_TESTS=ON

2. Build tests

   cmake --build simulation/build --target simulation_phase3_tests -j

3. Run via CTest

   ctest --test-dir simulation/build --output-on-failure

4. Run test binary directly

   simulation/build/simulation_phase3_tests.exe

## Run Phase 4 Tests

1. Configure

   cmake -S simulation -B simulation/build -G Ninja -DSIM_BUILD_TESTS=ON

2. Build tests

   cmake --build simulation/build --target simulation_phase4_tests -j

3. Run via CTest

   ctest --test-dir simulation/build --output-on-failure -R simulation_phase4_tests

4. Run test binary directly

   simulation/build/simulation_phase4_tests.exe

## Run Phase 5 Tests

1. Configure

   cmake -S simulation -B simulation/build -G Ninja -DSIM_BUILD_TESTS=ON

2. Build tests

   cmake --build simulation/build --target simulation_phase5_tests -j

3. Run via CTest

   ctest --test-dir simulation/build --output-on-failure -R simulation_phase5_tests

4. Run test binary directly

   simulation/build/simulation_phase5_tests.exe

## Notes

- This simulation project intentionally does not use ESP-IDF build tooling.
- The current channel model uses free-space path loss and a simple receiver sensitivity threshold.
- Config files are parsed from a strict YAML-like subset (2-space indentation, list items with `-`).
- Use `simulation/config/simulation_config.schema.yml` as the field reference.
