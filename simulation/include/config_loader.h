#pragma once

#include <string>

#include "simulation_config.h"

/**
 * @file config_loader.h
 * @ingroup sim_config
 * @brief Strict parser utilities for file-based simulation scenarios.
 */

/**
 * @ingroup sim_api
 * @brief Loads @ref SimulationConfig from disk or in-memory text.
 *
 * The parser accepts a constrained YAML-like format intended for deterministic tests.
 * Validation is strict: unknown keys, malformed indentation, or out-of-range values
 * produce @c std::runtime_error with line-level diagnostics.
 */
class ConfigLoader {
public:
    /**
     * @brief Load and parse a simulation config file.
     * @param file_path Path to the config file.
     * @return Parsed and validated simulation configuration.
     * @throws std::runtime_error If the file cannot be opened or parsing fails.
     */
    static SimulationConfig loadFromFile(const std::string& file_path);

    /**
     * @brief Parse simulation config content from text.
     * @param text Full config text in the supported YAML-like grammar.
     * @return Parsed and validated simulation configuration.
     * @throws std::runtime_error If syntax or semantic validation fails.
     */
    static SimulationConfig parseText(const std::string& text);
};
