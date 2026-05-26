#ifndef TRACKER_RULES_HPP
#define TRACKER_RULES_HPP

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

/** @brief Size-based piece length override entry within a tracker rule. */
struct PieceLengthOverride {
    int64_t size_below;      ///< Apply override when content size is below this (bytes)
    int piece_length_kb;     ///< Piece length to use (KB, must be in AllowedPieceSizes)
};

/** @brief A single tracker rule loaded from the rules YAML database. */
struct TrackerRule {
    std::string name;                                    ///< Rule identifier (YAML key name)
    std::optional<std::string> domain;                   ///< Tracker domain to match (exact or subdomain)
    std::optional<std::string> source;                   ///< Source tag to auto-set
    std::optional<int> max_piece_length;                 ///< Maximum piece length in bytes
    std::optional<int> max_torrent_size;                 ///< Maximum .torrent file size in bytes
    std::vector<PieceLengthOverride> piece_length_overrides; ///< Size-based piece length overrides (sorted descending)
};

/** @brief Result of applying tracker rule enforcement to a torrent configuration. */
struct RulesEnforcementResult {
    bool adjusted = false;                    ///< Whether piece length was changed
    std::optional<int> original_piece_length; ///< Original piece length in KB (before adjustment)
    std::optional<int> adjusted_piece_length; ///< New piece length in KB (after adjustment)
    bool source_applied = false;              ///< Whether source tag was applied
    std::string source_value;                 ///< Source tag value (if applied)
    bool constraint_violation = false;        ///< Whether a constraint (e.g. max_torrent_size) cannot be satisfied
    std::string violation_message;            ///< Human-readable violation description
};

/** @brief Loads and queries a YAML-based tracker rules database.
 *
 * Supports per-tracker and default rules with piece length limits,
 * torrent size constraints, source tag auto-setting, and size-based
 * piece length overrides. Rules are loaded once and shared read-only
 * across batch workers (thread-safe after load).
 */
class TrackerRulesDatabase {
public:
    /** @brief Load rules from a YAML file.
     * @throws std::runtime_error on missing file, bad version, or parse errors.
     */
    void load(const fs::path& path);

    /** @brief Resolve the rules file path.
     *
     * If explicit_path is given, use it directly (throws if not found).
     * Otherwise search: ./rules.yaml, $XDG_CONFIG_HOME/torrent-builder/rules.yaml,
     * ~/.config/torrent-builder/rules.yaml.
     * @throws std::runtime_error if no rules file is found.
     */
    static fs::path find_rules_file(const std::optional<fs::path>& explicit_path);

    /** @brief Find the first rule matching any of the given tracker URLs.
     *
     * Checks tracker domains against rule domains (case-insensitive, supports subdomains).
     * Falls back to the "default" section if no tracker-specific rule matches.
     * @return Matching rule, or std::nullopt if no match and no default.
     */
    std::optional<TrackerRule> find_matching_rule(const std::vector<std::string>& trackers) const;

    /** @brief Apply a matched rule's constraints to a torrent configuration.
     *
     * Enforces max_piece_length caps, piece_length_overrides based on content size,
     * and max_torrent_size limits. Computes estimated torrent size internally.
     * @param rule             The matched tracker rule.
     * @param total_size       Content size in bytes.
     * @param current_piece_length_kb  Current piece length in KB (std::nullopt if auto).
     * @return Enforcement result with adjustments and violation info.
     */
    RulesEnforcementResult enforce(const TrackerRule& rule, int64_t total_size,
                                    std::optional<int> current_piece_length_kb) const;

    /** @brief Whether the rules file contained a "default" section. */
    bool has_default_rules() const { return default_rules_.has_value(); }

    /** @brief Access the default rules section. */
    const TrackerRule& default_rules() const { return *default_rules_; }

    /** @brief Access all tracker-specific rules. */
    const std::vector<TrackerRule>& trackers() const { return trackers_; }

private:
    std::optional<TrackerRule> default_rules_;
    std::vector<TrackerRule> trackers_;
};

#endif
