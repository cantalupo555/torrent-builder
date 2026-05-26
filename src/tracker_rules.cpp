#include "tracker_rules.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "constants.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>

namespace
{
static constexpr std::uintmax_t max_yaml_size = 1 * 1024 * 1024;

PieceLengthOverride parse_override(const YAML::Node& node)
{
    PieceLengthOverride ov;
    ov.size_below = node["size_below"].as<int64_t>();
    ov.piece_length_kb = node["piece_length"].as<int>();
    return ov;
}

TrackerRule parse_rule(const YAML::Node& node, const std::string& name)
{
    TrackerRule rule;
    rule.name = name;

    if (node["domain"]) rule.domain = node["domain"].as<std::string>();
    if (node["source"]) rule.source = node["source"].as<std::string>();
    if (node["max_piece_length"]) rule.max_piece_length = node["max_piece_length"].as<int>();
    if (node["max_torrent_size"]) rule.max_torrent_size = node["max_torrent_size"].as<int>();

    if (node["piece_length_overrides"]) {
        for (const auto& ov : node["piece_length_overrides"]) {
            rule.piece_length_overrides.push_back(parse_override(ov));
        }
        std::sort(rule.piece_length_overrides.begin(), rule.piece_length_overrides.end(),
                  [](const PieceLengthOverride& a, const PieceLengthOverride& b) {
                      return a.size_below > b.size_below;
                  });
    }

    return rule;
}

std::string format_bytes(int64_t bytes)
{
    if (bytes >= 1024 * 1024 * 1024) {
        return std::to_string(bytes / (1024 * 1024 * 1024)) + " GiB";
    } else if (bytes >= 1024 * 1024) {
        return std::to_string(bytes / (1024 * 1024)) + " MiB";
    } else if (bytes >= 1024) {
        return std::to_string(bytes / 1024) + " KiB";
    }
    return std::to_string(bytes) + " B";
}
}

fs::path TrackerRulesDatabase::find_rules_file(const std::optional<fs::path>& explicit_path)
{
    if (explicit_path) {
        if (!fs::exists(*explicit_path)) {
            throw std::runtime_error("Rules file not found: " + explicit_path->string());
        }
        return *explicit_path;
    }

    fs::path cwd_rules = fs::current_path() / "rules.yaml";
    if (fs::exists(cwd_rules)) {
        return cwd_rules;
    }

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    fs::path config_dir;
    if (xdg_config && xdg_config[0] != '\0') {
        config_dir = fs::path(xdg_config) / "torrent-builder" / "rules.yaml";
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            config_dir = fs::path(home) / ".config" / "torrent-builder" / "rules.yaml";
        }
    }

    if (!config_dir.empty() && fs::exists(config_dir)) {
        return config_dir;
    }

    throw std::runtime_error("No rules file found. Searched: ./rules.yaml"
        + (config_dir.empty() ? std::string() : ", " + config_dir.string())
        + ". Use --rules-file to specify a path.");
}

void TrackerRulesDatabase::load(const fs::path& path)
{
    if (fs::file_size(path) > max_yaml_size) {
        throw std::runtime_error("Rules file too large (max 1 MB): " + path.string());
    }

    YAML::Node root = YAML::LoadFile(path.string());

    if (!root["version"] || root["version"].as<int>() != 1) {
        throw std::runtime_error("Unsupported rules file version (expected: 1)");
    }

    if (root["default"]) {
        default_rules_ = parse_rule(root["default"], "default");
    }

    if (root["trackers"]) {
        for (const auto& entry : root["trackers"]) {
            std::string name = entry.first.as<std::string>();
            trackers_.push_back(parse_rule(entry.second, name));
        }
    }

    log_message("Loaded tracker rules from: " + path.string()
        + " (" + std::to_string(trackers_.size()) + " trackers)", LogLevel::INFO);
}

std::optional<TrackerRule> TrackerRulesDatabase::find_matching_rule(const std::vector<std::string>& tracker_urls) const
{
    for (const auto& url : tracker_urls) {
        std::string domain = utils::extract_domain(url);
        if (domain.empty()) continue;

        std::string tracker_domain = utils::to_lower(domain);

        for (const auto& rule : trackers_) {
            if (!rule.domain) continue;

            std::string rule_domain = utils::to_lower(*rule.domain);

            if (tracker_domain == rule_domain) {
                return rule;
            }

            if (tracker_domain.size() > rule_domain.size() + 1
                && tracker_domain.compare(tracker_domain.size() - rule_domain.size(), rule_domain.size(), rule_domain) == 0
                && tracker_domain[tracker_domain.size() - rule_domain.size() - 1] == '.') {
                return rule;
            }
        }
    }

    return default_rules_;
}

RulesEnforcementResult TrackerRulesDatabase::enforce(
    const TrackerRule& rule,
    int64_t total_size,
    std::optional<int> current_piece_length_kb) const
{
    RulesEnforcementResult result;

    if (rule.source) {
        result.source_applied = true;
        result.source_value = *rule.source;
    }

    if (!rule.max_piece_length && !rule.max_torrent_size && rule.piece_length_overrides.empty()) {
        return result;
    }

    static const std::vector<int> allowed(AllowedPieceSizes::values.begin(),
                                           AllowedPieceSizes::values.end());

    int effective_kb;
    if (current_piece_length_kb && *current_piece_length_kb > 0) {
        effective_kb = *current_piece_length_kb;
    } else {
        effective_kb = utils::auto_piece_size(total_size) / 1024;
    }

    if (!std::ranges::contains(allowed, effective_kb)) {
        auto it = std::lower_bound(allowed.begin(), allowed.end(), effective_kb);
        effective_kb = (it != allowed.end()) ? *it : allowed.back();
    }

    for (const auto& ov : rule.piece_length_overrides) {
        if (total_size < ov.size_below) {
            if (std::ranges::contains(allowed, ov.piece_length_kb)) {
                if (effective_kb != ov.piece_length_kb) {
                    result.adjusted = true;
                    result.original_piece_length = current_piece_length_kb.value_or(effective_kb);
                    effective_kb = ov.piece_length_kb;
                }
            }
            break;
        }
    }

    if (rule.max_piece_length) {
        int max_kb = *rule.max_piece_length / 1024;
        if (effective_kb > max_kb) {
            result.adjusted = true;
            result.original_piece_length = current_piece_length_kb.value_or(effective_kb);
            effective_kb = max_kb;

            auto it = std::lower_bound(allowed.begin(), allowed.end(), effective_kb);
            if (it != allowed.end() && *it > effective_kb) {
                if (it != allowed.begin()) --it;
            }
            if (it != allowed.end()) {
                effective_kb = *it;
            }
        }
    }

    if (rule.max_torrent_size && total_size > 0) {
        int64_t max_size = *rule.max_torrent_size;
        int64_t piece_size_bytes = static_cast<int64_t>(effective_kb) * 1024;
        int64_t num_pieces = (piece_size_bytes > 0) ? (total_size + piece_size_bytes - 1) / piece_size_bytes : 0;
        int64_t estimated = num_pieces * 20 + piece_size_bytes;

        if (estimated > max_size && effective_kb > 0) {
            int64_t torrent_size = estimated;
            int original_kb = effective_kb;

            auto it = std::lower_bound(allowed.begin(), allowed.end(), effective_kb);
            if (it == allowed.end()) it = allowed.end() - 1;

            while (it != allowed.end()) {
                int try_kb = *it;
                int64_t piece_size_bytes = static_cast<int64_t>(try_kb) * 1024;
                int64_t num_pieces = (total_size + piece_size_bytes - 1) / piece_size_bytes;
                int64_t estimated = num_pieces * 20 + piece_size_bytes;

                if (rule.max_piece_length && try_kb * 1024 > *rule.max_piece_length) {
                    break;
                }

                if (estimated <= max_size) {
                    effective_kb = try_kb;
                    torrent_size = estimated;
                    break;
                }
                ++it;
            }

            if (torrent_size > max_size) {
                result.constraint_violation = true;
                result.violation_message = "Tracker '" + rule.name + "' requires torrent size <= "
                    + format_bytes(max_size) + ". Estimated: " + format_bytes(torrent_size)
                    + ". Even at maximum allowed piece size, the torrent may exceed the limit.";
            }

            if (effective_kb != original_kb) {
                result.adjusted = true;
                if (!result.original_piece_length) {
                    result.original_piece_length = current_piece_length_kb.value_or(original_kb);
                }
            }
        }
    }

    if (result.adjusted) {
        result.adjusted_piece_length = effective_kb;
        log_message("Tracker rule '" + rule.name + "': piece size adjusted from "
            + std::to_string(*result.original_piece_length) + " KB to "
            + std::to_string(effective_kb) + " KB", LogLevel::INFO);
    }

    return result;
}
