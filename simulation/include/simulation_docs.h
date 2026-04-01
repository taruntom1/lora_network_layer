#pragma once

/**
 * @file simulation_docs.h
 * @brief Doxygen group definitions for the host simulation subsystem.
 *
 * This header is documentation-only and is not required by runtime code.
 */

/**
 * @defgroup sim Host Simulation
 * @brief Deterministic host runtime used to exercise the network layer without ESP-IDF.
 *
 * The simulation subsystem composes portable network-layer components with host adapters:
 * - @ref SimulationClock for virtual time.
 * - @ref SimulatedLocationProvider for deterministic node kinematics.
 * - @ref SimulatedNetwork for radio propagation and fan-out.
 * - @ref SimulatedLinkLayer for link-layer bridging to @ref NetworkManager.
 * - @ref SimulationBuilder and @ref SimulationScenario for scenario composition.
 * - @ref ConfigLoader for strict file-driven scenario loading.
 */

/**
 * @defgroup sim_api Simulation Public API
 * @ingroup sim
 * @brief Public API used by simulation applications and tests.
 */

/**
 * @defgroup sim_config Simulation Configuration Model
 * @ingroup sim
 * @brief Data structures and parser used to define devices, runtime, and waypoints.
 */

/**
 * @defgroup sim_runtime Simulation Runtime Orchestration
 * @ingroup sim
 * @brief Scenario lifecycle, stepping, node runtime composition, and time progression.
 */

/**
 * @defgroup sim_channel Simulated Radio Channel
 * @ingroup sim
 * @brief Host radio model implementing path loss, sensitivity checks, and SNR computation.
 */

/**
 * @defgroup sim_internal Simulation Internal Notes
 * @ingroup sim
 * @brief Internal behavior notes, processing stages, and implementation details.
 */

/**
 * @defgroup sim_test Simulation Test Utilities
 * @ingroup sim
 * @brief Test harness APIs used by phase 3/4/5 simulation test binaries.
 */
