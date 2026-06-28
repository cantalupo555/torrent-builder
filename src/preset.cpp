#include "preset.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "constants.hpp"
#include "torrent_creator.hpp"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <regex>
#include <ranges>

ConfigValues parse_yaml_config(const YAML::Node& node)
{
    ConfigValues cv;

    if (node["trackers"]) {
        std::vector<std::string> trackers;
        for (const auto& t : node["trackers"]) {
            auto url = t.as<std::string>();
            if (!utils::is_valid_url(url)) {
                throw std::runtime_error("Invalid tracker URL in config: " + url);
            }
            trackers.push_back(url);
        }
        cv.trackers = std::move(trackers);
    }

    if (node["web_seeds"]) {
        std::vector<std::string> seeds;
        for (const auto& s : node["web_seeds"]) {
            auto url = s.as<std::string>();
            if (!utils::is_valid_url(url)) {
                throw std::runtime_error("Invalid web seed URL in config: " + url);
            }
            seeds.push_back(url);
        }
        cv.web_seeds = std::move(seeds);
    }

    if (node["private"]) cv.is_private = node["private"].as<bool>();
    if (node["source"]) cv.source = node["source"].as<std::string>();
    if (node["piece_size"]) cv.piece_size = node["piece_size"].as<int>();
    if (node["target_piece_count"]) cv.target_piece_count = node["target_piece_count"].as<int>();
    if (node["comment"]) cv.comment = node["comment"].as<std::string>();
    if (node["creator"]) cv.creator = node["creator"].as<std::string>();
    if (node["name"]) cv.name = node["name"].as<std::string>();
    if (node["creation_date"]) cv.creation_date = node["creation_date"].as<bool>();
    if (node["torrent_version"]) cv.torrent_version = node["torrent_version"].as<int>();
    if (node["entropy"]) cv.entropy = node["entropy"].as<bool>();

    if (node["exclude_patterns"]) {
        std::vector<std::string> patterns;
        for (const auto& p : node["exclude_patterns"]) {
            patterns.push_back(p.as<std::string>());
        }
        cv.exclude_patterns = std::move(patterns);
    }

    if (node["include_patterns"]) {
        std::vector<std::string> patterns;
        for (const auto& p : node["include_patterns"]) {
            patterns.push_back(p.as<std::string>());
        }
        cv.include_patterns = std::move(patterns);
    }

    return cv;
}

ConfigValues merge_config_values(const ConfigValues& base, const ConfigValues& overlay)
{
    ConfigValues result = base;

    if (overlay.path) result.path = overlay.path;
    if (overlay.output) result.output = overlay.output;
    if (overlay.trackers) result.trackers = overlay.trackers;
    if (overlay.web_seeds) result.web_seeds = overlay.web_seeds;
    if (overlay.is_private.has_value()) result.is_private = overlay.is_private;
    if (overlay.source) result.source = overlay.source;
    if (overlay.piece_size) result.piece_size = overlay.piece_size;
    if (overlay.target_piece_count) result.target_piece_count = overlay.target_piece_count;
    if (overlay.comment) result.comment = overlay.comment;
    if (overlay.creator) result.creator = overlay.creator;
    if (overlay.name) result.name = overlay.name;
    if (overlay.creation_date.has_value()) result.creation_date = overlay.creation_date;
    if (overlay.torrent_version) result.torrent_version = overlay.torrent_version;
    if (overlay.entropy.has_value()) result.entropy = overlay.entropy;
    if (overlay.exclude_patterns) result.exclude_patterns = overlay.exclude_patterns;
    if (overlay.include_patterns) result.include_patterns = overlay.include_patterns;

    return result;
}

fs::path PresetLoader::find_preset_file(const std::optional<fs::path>& explicit_path)
{
    if (explicit_path) {
        if (!fs::exists(*explicit_path)) {
            throw std::runtime_error("Preset file not found: " + explicit_path->string());
        }
        return *explicit_path;
    }

    fs::path cwd_preset = fs::current_path() / "presets.yaml";
    if (fs::exists(cwd_preset)) {
        return cwd_preset;
    }

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    fs::path config_dir;
    if (xdg_config && xdg_config[0] != '\0') {
        config_dir = fs::path(xdg_config) / "torrent-builder" / "presets.yaml";
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            config_dir = fs::path(home) / ".config" / "torrent-builder" / "presets.yaml";
        }
    }

    if (!config_dir.empty() && fs::exists(config_dir)) {
        return config_dir;
    }

    throw std::runtime_error("No preset file found. Searched: ./presets.yaml"
        + (config_dir.empty() ? std::string() : ", " + config_dir.string())
        + ". Use --preset-file to specify a path.");
}

void PresetLoader::load(const fs::path& path)
{
    static constexpr std::uintmax_t max_yaml_size = 1 * 1024 * 1024;
    if (fs::file_size(path) > max_yaml_size) {
        throw std::runtime_error("Preset file too large (max 1 MB): " + path.string());
    }

    YAML::Node root = YAML::LoadFile(path.string());

    if (!root["version"] || root["version"].as<int>() != 1) {
        throw std::runtime_error("Unsupported preset file version (expected: 1)");
    }

    if (root["default"]) {
        defaults_ = parse_yaml_config(root["default"]);
    }

    if (root["presets"]) {
        for (const auto& entry : root["presets"]) {
            std::string name = entry.first.as<std::string>();
            presets_[name] = parse_yaml_config(entry.second);
        }
    }

    log_message("Loaded presets from: " + path.string()
        + " (" + std::to_string(presets_.size()) + " presets)", LogLevel::INFO);
}

ConfigValues PresetLoader::resolve(const std::string& name) const
{
    auto it = presets_.find(name);
    if (it == presets_.end()) {
        std::string available;
        bool first = true;
        for (const auto& [k, _] : presets_) {
            if (!first) available += ", ";
            available += k;
            first = false;
        }
        throw std::runtime_error("Unknown preset: '" + name + "'. Available: "
            + (presets_.empty() ? std::string("(none)") : available));
    }

    return merge_config_values(defaults_, it->second);
}

bool PresetLoader::has_preset(const std::string& name) const
{
    return presets_.count(name) > 0;
}
