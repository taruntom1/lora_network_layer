#include "config_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @file config_loader.cpp
 * @ingroup sim_internal
 * @brief Internal parser pipeline for the simulation YAML-like configuration format.
 *
 * Parsing is intentionally strict and deterministic:
 * 1. Preprocess strips comments/blank lines and validates indentation multiples.
 * 2. The top-level parser validates section structure (`runtime`, `devices`).
 * 3. Field-level converters perform type/range checking and report line errors.
 * 4. Device waypoint lists are normalized and sorted by activation timestamp.
 */

namespace {

struct ParsedLine {
    size_t line_no;
    size_t indent;
    std::string text;
};

std::string trim(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

[[noreturn]] void parseError(size_t line_no, const std::string& message)
{
    throw std::runtime_error("Config parse error at line " +
                             std::to_string(static_cast<unsigned long long>(line_no)) +
                             ": " + message);
}

std::pair<std::string, std::string> splitKeyValue(const ParsedLine& line)
{
    const size_t pos = line.text.find(':');
    if (pos == std::string::npos) {
        parseError(line.line_no, "expected key: value pair");
    }

    const std::string key = trim(line.text.substr(0, pos));
    const std::string value = trim(line.text.substr(pos + 1));
    if (key.empty()) {
        parseError(line.line_no, "empty key is not allowed");
    }

    return {key, value};
}

bool isSectionLine(const ParsedLine& line)
{
    return !line.text.empty() && line.text.back() == ':' && line.text.find(':') == line.text.size() - 1;
}

long long parseInteger(const ParsedLine& line, const std::string& raw)
{
    if (raw.empty()) {
        parseError(line.line_no, "missing integer value");
    }

    size_t consumed = 0;
    long long v = 0;
    try {
        v = std::stoll(raw, &consumed, 10);
    } catch (const std::exception&) {
        parseError(line.line_no, "invalid integer value '" + raw + "'");
    }

    if (consumed != raw.size()) {
        parseError(line.line_no, "invalid trailing characters in integer value '" + raw + "'");
    }

    return v;
}

unsigned long long parseUnsignedInteger(const ParsedLine& line, const std::string& raw)
{
    if (raw.empty()) {
        parseError(line.line_no, "missing integer value");
    }

    size_t consumed = 0;
    unsigned long long v = 0;
    try {
        v = std::stoull(raw, &consumed, 10);
    } catch (const std::exception&) {
        parseError(line.line_no, "invalid unsigned integer value '" + raw + "'");
    }

    if (consumed != raw.size()) {
        parseError(line.line_no,
                   "invalid trailing characters in unsigned integer value '" + raw + "'");
    }

    return v;
}

float parseFloat(const ParsedLine& line, const std::string& raw)
{
    if (raw.empty()) {
        parseError(line.line_no, "missing float value");
    }

    size_t consumed = 0;
    float v = 0.0f;
    try {
        v = std::stof(raw, &consumed);
    } catch (const std::exception&) {
        parseError(line.line_no, "invalid float value '" + raw + "'");
    }

    if (consumed != raw.size()) {
        parseError(line.line_no, "invalid trailing characters in float value '" + raw + "'");
    }

    return v;
}

template <typename TInt>
TInt parseUnsigned(const ParsedLine& line, const std::string& raw, const char* label)
{
    const unsigned long long v = parseUnsignedInteger(line, raw);
    if (v > static_cast<unsigned long long>(std::numeric_limits<TInt>::max())) {
        parseError(line.line_no,
                   std::string(label) + " is out of range: " + raw);
    }
    return static_cast<TInt>(v);
}

template <typename TInt>
TInt parseSigned(const ParsedLine& line, const std::string& raw, const char* label)
{
    const long long v = parseInteger(line, raw);
    if (v < static_cast<long long>(std::numeric_limits<TInt>::min()) ||
        v > static_cast<long long>(std::numeric_limits<TInt>::max())) {
        parseError(line.line_no,
                   std::string(label) + " is out of range: " + raw);
    }
    return static_cast<TInt>(v);
}

std::vector<ParsedLine> preprocess(const std::string& text)
{
    std::vector<ParsedLine> lines;

    std::istringstream input(text);
    std::string raw;
    size_t line_no = 0;

    while (std::getline(input, raw)) {
        ++line_no;

        const size_t comment_pos = raw.find('#');
        if (comment_pos != std::string::npos) {
            raw = raw.substr(0, comment_pos);
        }

        const std::string stripped = trim(raw);
        if (stripped.empty()) {
            continue;
        }

        size_t indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') {
            ++indent;
        }

        if (indent > 0 && indent % 2 != 0) {
            parseError(line_no, "indentation must use multiples of 2 spaces");
        }

        lines.push_back(ParsedLine{line_no, indent, stripped});
    }

    return lines;
}

void setRuntimeField(SimulationRuntimeConfig& runtime, const ParsedLine& line)
{
    const auto [key, value] = splitKeyValue(line);
    if (isSectionLine(line) || value.empty()) {
        parseError(line.line_no, "runtime field must be 'key: value'");
    }

    if (key == "carrier_freq_mhz") {
        runtime.carrier_freq_mhz = parseFloat(line, value);
    } else if (key == "start_time_s") {
        runtime.start_time_s = parseUnsigned<uint32_t>(line, value, "start_time_s");
    } else if (key == "duplicate_cache_size") {
        runtime.network_config.duplicate_cache_size =
            parseUnsigned<size_t>(line, value, "duplicate_cache_size");
    } else if (key == "forwarding_queue_size") {
        runtime.network_config.forwarding_queue_size =
            parseUnsigned<size_t>(line, value, "forwarding_queue_size");
    } else if (key == "rx_queue_depth") {
        runtime.network_config.rx_queue_depth =
            parseUnsigned<size_t>(line, value, "rx_queue_depth");
    } else {
        parseError(line.line_no, "unknown runtime field '" + key + "'");
    }
}

void setDeviceField(SimulationDeviceConfig& device,
                    bool& has_node_id,
                    const ParsedLine& line,
                    const std::string& key,
                    const std::string& value)
{
    if (key == "id" || key == "node_id") {
        device.node_id = parseUnsigned<uint16_t>(line, value, "device id");
        has_node_id = true;
    } else if (key == "lat" || key == "initial_lat") {
        device.initial_position.lat = parseSigned<int32_t>(line, value, "lat");
    } else if (key == "lon" || key == "initial_lon") {
        device.initial_position.lon = parseSigned<int32_t>(line, value, "lon");
    } else if (key == "speed_cm_s") {
        device.speed_cm_s = parseUnsigned<uint16_t>(line, value, "speed_cm_s");
    } else if (key == "heading_cdeg") {
        device.heading_cdeg = parseUnsigned<uint16_t>(line, value, "heading_cdeg");
    } else if (key == "tx_power_dbm") {
        device.radio.tx_power_dbm = parseFloat(line, value);
    } else if (key == "noise_floor_dbm") {
        device.radio.noise_floor_dbm = parseFloat(line, value);
    } else if (key == "sensitivity_dbm") {
        device.radio.sensitivity_dbm = parseFloat(line, value);
    } else {
        parseError(line.line_no, "unknown device field '" + key + "'");
    }
}

void setWaypointField(SimulationWaypointConfig& waypoint,
                      const ParsedLine& line,
                      const std::string& key,
                      const std::string& value)
{
    if (key == "at_s") {
        waypoint.at_s = parseUnsigned<uint32_t>(line, value, "at_s");
    } else if (key == "lat") {
        waypoint.position.lat = parseSigned<int32_t>(line, value, "lat");
    } else if (key == "lon") {
        waypoint.position.lon = parseSigned<int32_t>(line, value, "lon");
    } else if (key == "speed_cm_s") {
        waypoint.speed_cm_s = parseUnsigned<uint16_t>(line, value, "speed_cm_s");
    } else if (key == "heading_cdeg") {
        waypoint.heading_cdeg = parseUnsigned<uint16_t>(line, value, "heading_cdeg");
    } else {
        parseError(line.line_no, "unknown waypoint field '" + key + "'");
    }
}

size_t parseWaypointsBlock(const std::vector<ParsedLine>& lines,
                           size_t index,
                           size_t list_indent,
                           SimulationDeviceConfig& device)
{
    while (index < lines.size()) {
        const ParsedLine& line = lines[index];
        if (line.indent < list_indent) {
            break;
        }

        if (line.indent != list_indent || !startsWith(line.text, "-")) {
            parseError(line.line_no, "expected waypoint list item '- ...'");
        }

        SimulationWaypointConfig waypoint;
        std::string inline_content = trim(line.text.substr(1));
        if (!inline_content.empty()) {
            ParsedLine inline_line{line.line_no, line.indent, inline_content};
            const auto [k, v] = splitKeyValue(inline_line);
            if (v.empty()) {
                parseError(line.line_no, "waypoint list item must use '- key: value'");
            }
            setWaypointField(waypoint, inline_line, k, v);
        }

        ++index;
        while (index < lines.size() && lines[index].indent > list_indent) {
            const ParsedLine& child = lines[index];
            if (child.indent != list_indent + 2) {
                parseError(child.line_no, "unexpected waypoint indentation");
            }

            const auto [k, v] = splitKeyValue(child);
            if (v.empty()) {
                parseError(child.line_no, "waypoint field must be 'key: value'");
            }
            setWaypointField(waypoint, child, k, v);
            ++index;
        }

        device.waypoints.push_back(waypoint);
    }

    return index;
}

size_t parseDeviceBlock(const std::vector<ParsedLine>& lines,
                        size_t index,
                        size_t item_indent,
                        SimulationDeviceConfig& device)
{
    bool has_node_id = false;

    const ParsedLine& item_line = lines[index];
    std::string inline_content = trim(item_line.text.substr(1));
    if (!inline_content.empty()) {
        ParsedLine inline_line{item_line.line_no, item_line.indent, inline_content};
        const auto [k, v] = splitKeyValue(inline_line);
        if (v.empty()) {
            parseError(item_line.line_no, "device list item must use '- key: value'");
        }
        setDeviceField(device, has_node_id, inline_line, k, v);
    }

    ++index;
    while (index < lines.size() && lines[index].indent > item_indent) {
        const ParsedLine& line = lines[index];
        if (line.indent != item_indent + 2) {
            parseError(line.line_no, "unexpected device indentation");
        }

        if (line.text == "waypoints:") {
            index = parseWaypointsBlock(lines, index + 1, item_indent + 4, device);
            continue;
        }

        const auto [k, v] = splitKeyValue(line);
        if (v.empty()) {
            parseError(line.line_no, "device field must be 'key: value'");
        }
        setDeviceField(device, has_node_id, line, k, v);
        ++index;
    }

    if (!has_node_id) {
        parseError(item_line.line_no, "device id is required");
    }

    std::sort(device.waypoints.begin(), device.waypoints.end(),
              [](const SimulationWaypointConfig& a, const SimulationWaypointConfig& b) {
                  return a.at_s < b.at_s;
              });

    return index;
}

SimulationConfig parseLines(const std::vector<ParsedLine>& lines)
{
    SimulationConfig cfg;

    bool saw_runtime = false;
    bool saw_devices = false;

    size_t index = 0;
    while (index < lines.size()) {
        const ParsedLine& line = lines[index];
        if (line.indent != 0) {
            parseError(line.line_no, "top-level keys must not be indented");
        }

        if (line.text == "runtime:") {
            saw_runtime = true;
            ++index;
            while (index < lines.size() && lines[index].indent > 0) {
                if (lines[index].indent != 2) {
                    parseError(lines[index].line_no, "unexpected runtime indentation");
                }
                setRuntimeField(cfg.runtime, lines[index]);
                ++index;
            }
            continue;
        }

        if (line.text == "devices:") {
            saw_devices = true;
            ++index;
            while (index < lines.size() && lines[index].indent > 0) {
                if (lines[index].indent != 2 || !startsWith(lines[index].text, "-")) {
                    parseError(lines[index].line_no, "expected device list item '- ...'");
                }

                SimulationDeviceConfig device;
                index = parseDeviceBlock(lines, index, 2, device);
                cfg.devices.push_back(device);
            }
            continue;
        }

        parseError(line.line_no, "unknown top-level key '" + line.text + "'");
    }

    if (!saw_runtime) {
        throw std::runtime_error("Config parse error: missing top-level 'runtime:' section");
    }
    if (!saw_devices) {
        throw std::runtime_error("Config parse error: missing top-level 'devices:' section");
    }

    return cfg;
}

} // namespace

SimulationConfig ConfigLoader::loadFromFile(const std::string& file_path)
{
    std::ifstream in(file_path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open config file: " + file_path);
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseText(buffer.str());
}

SimulationConfig ConfigLoader::parseText(const std::string& text)
{
    const std::vector<ParsedLine> lines = preprocess(text);
    return parseLines(lines);
}
