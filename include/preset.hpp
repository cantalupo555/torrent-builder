#ifndef PRESET_HPP
#define PRESET_HPP

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <unordered_map>

namespace YAML { class Node; }

namespace fs = std::filesystem;

/** @brief Intermediate config layer with all-optional fields for merge tracking.
 *
 * Used as the glue between YAML presets, batch config, and CLI flags.
 * Merge hierarchy: CLI flags > preset values > default section > TorrentConfig built-in defaults.
 */
struct ConfigValues {
    std::optional<std::string> path;                ///< Input file or directory path
    std::optional<std::string> output;              ///< Output .torrent file path
    std::optional<std::vector<std::string>> trackers;///< Tracker announce URLs
    std::optional<std::vector<std::string>> web_seeds;///< Web seed URLs
    std::optional<bool> is_private;                  ///< Whether torrent is private
    std::optional<std::string> source;              ///< Source field embedding
    std::optional<int> piece_size;                  ///< Piece size in KB (e.g. 4096 = 4MB)
    std::optional<int> target_piece_count;           ///< Desired number of pieces (calculates piece size)
    std::optional<std::string> comment;             ///< Torrent comment
    std::optional<std::string> creator;             ///< Created-by field
    std::optional<std::string> name;                ///< Torrent name override
    std::optional<bool> creation_date;              ///< Whether to embed creation date
    std::optional<int> torrent_version;             ///< 1=V1, 2=V2, 3=Hybrid
    std::optional<bool> entropy;                    ///< Whether to include entropy data
    std::optional<std::vector<std::string>> exclude_patterns;///< Glob patterns to exclude
    std::optional<std::vector<std::string>> include_patterns;///< Glob patterns to include
};

/** @brief Merge two ConfigValues: overlay fields win over base. */
ConfigValues merge_config_values(const ConfigValues& base, const ConfigValues& overlay);

/** @brief Parse torrent config fields from a YAML node.
 *
 * Extracts all optional fields (trackers, web_seeds, private, source, piece_size,
 * comment, creator, name, creation_date, torrent_version, entropy, exclude/include_patterns)
 * from the given YAML node. Validates tracker and web seed URLs.
 *
 * @param node YAML node to parse (a preset entry, batch job, or default section).
 * @return ConfigValues with any recognized fields populated.
 * @throws std::runtime_error on invalid tracker/web seed URLs.
 */
ConfigValues parse_yaml_config(const YAML::Node& node);

/** @brief Discovers and loads YAML preset files for torrent configuration.
 *
 * Search order for preset files:
 *   1. Explicit path (if provided)
 *   2. ./presets.yaml (current working directory)
 *   3. $XDG_CONFIG_HOME/torrent-builder/presets.yaml
 *   4. ~/.config/torrent-builder/presets.yaml
 */
class PresetLoader {
public:
    /** @brief Locate a preset file on disk.
     * @param explicit_path If provided, use this path directly.
     * @throws std::runtime_error if no file is found.
     */
    static fs::path find_preset_file(const std::optional<fs::path>& explicit_path);

    /** @brief Load and parse a preset YAML file.
     * @throws std::runtime_error on unsupported version or parse errors.
     */
    void load(const fs::path& path);

    /** @brief Resolve a named preset merged with defaults.
     * @throws std::runtime_error if the preset name does not exist.
     */
    ConfigValues resolve(const std::string& name) const;

    /** @brief Check whether a named preset exists. */
    bool has_preset(const std::string& name) const;

private:
    ConfigValues defaults_;
    std::unordered_map<std::string, ConfigValues> presets_;
};

#endif
