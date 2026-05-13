#ifndef TORRENT_MODIFIER_HPP
#define TORRENT_MODIFIER_HPP

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <libtorrent/entry.hpp>

namespace fs = std::filesystem;

namespace lt = libtorrent;

/**
 * @brief Configuration for torrent metadata modification operations.
 */
struct ModifyConfig
{
    fs::path input;
    fs::path output;                                           // Empty = in-place modification
    std::optional<std::vector<std::string>> trackers;          // nullopt=unchanged, empty=clear all
    std::vector<std::string> add_trackers;
    std::vector<std::string> remove_trackers;
    std::optional<bool> is_private;                            // true=private, false=public, nullopt=unchanged
    std::optional<std::string> source;                         // Empty string removes the field
    std::optional<std::string> comment;                        // Empty string removes the field
    std::optional<std::string> name;
    bool entropy = false;                                      // Randomize info hash via entropy field
    bool dry_run = false;                                      // Preview changes without writing
};

/**
 * @brief Modify torrent metadata without re-hashing file content.
 *
 * Reads an existing .torrent file, applies metadata-only modifications
 * (trackers, private flag, source, comment, name, entropy), and writes
 * the result. Supports dry-run mode and atomic in-place writes.
 */
class TorrentModifier
{
  public:
    /**
     * @brief Construct a modifier with the given configuration.
     * @param config Modification parameters (input file, options, output).
     */
    explicit TorrentModifier(const ModifyConfig &config);

    /**
     * @brief Load, apply modifications, print hash diff, and save.
     * @throws std::runtime_error on I/O or parse failures.
     */
    void modify();

  private:
    ModifyConfig config_;
    std::vector<char> raw_buffer_;
    std::string old_hash_v1_;
    std::string old_hash_v2_;

    void load();
    void apply_modifications(lt::entry &root);
    void save(const std::vector<char> &buffer);
    std::pair<std::string, std::string> compute_hashes(const std::vector<char> &buffer) const;
    void rebuild_trackers(lt::entry &root, const std::vector<std::string> &urls);
    void add_trackers(lt::entry &root, const std::vector<std::string> &urls);
    void remove_trackers(lt::entry &root, const std::vector<std::string> &urls);
    std::vector<std::string> remove_url_from_tiers(lt::entry::list_type &tiers, const std::vector<std::string> &urls);
    void update_announce_from_remaining(lt::entry &root, const std::vector<std::string> &remaining);
    void print_diff(const std::string &new_hash_v1, const std::string &new_hash_v2) const;
};

#endif // TORRENT_MODIFIER_HPP
