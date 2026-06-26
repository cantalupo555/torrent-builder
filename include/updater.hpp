#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <string>
#include <optional>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

/**
 * @brief Result of perform_update().
 *
 * Success       - new binary replaced the current one synchronously.
 * PendingRestart - replacement was scheduled via async helper (e.g. Windows
 *                  batch script when the running .exe is locked). The caller
 *                  must NOT delete new_binary_path; the helper owns it now.
 */
enum class UpdateResult {
    Success,
    PendingRestart,
};

/**
 * @brief Information about the latest GitHub release.
 */
struct UpdateInfo
{
    std::string version;
    std::string changelog;
    int64_t asset_size = 0;
    nlohmann::json release_json;
};

/**
 * @brief Outcome of check_for_update().
 *
 * info            - has a value when a newer release is available (or the
 *                   current build is a dev build, which always compares older).
 * fetch_succeeded - true when the network round-trip completed (even if no
 *                   update was found); false on offline/parse/rate-limit errors.
 *                   Callers should only record the throttle timestamp when true.
 */
struct UpdateCheckResult
{
    std::optional<UpdateInfo> info;
    bool fetch_succeeded = false;
};

/**
 * @brief Detected OS and CPU architecture for asset matching.
 */
struct PlatformInfo
{
    std::string os;
    std::string arch;
};

/**
 * @brief Self-update functionality for torrent-builder.
 *
 * Checks GitHub Releases for newer versions, downloads the matching
 * platform binary, and replaces the current binary. On Linux and macOS
 * the replacement is atomic (rename). On Windows a sidecar batch script
 * performs the swap after the process exits.
 * Supports rollback via the --rollback flag.
 */
class Updater
{
public:
    /** @brief Function type override for curl execution in tests. */
    using CurlExecutor = std::function<std::string(const std::string &url, const std::vector<std::string> &extra_args)>;
    /** @brief Function type override for file download in tests. */
    using FileDownloader = std::function<bool(const std::string &url, const std::string &dest_path, int64_t expected_size)>;
    /** @brief Function type override for executable path in tests. */
    using ExePathProvider = std::function<std::string()>;

    /**
     * @brief Detect the current OS and CPU architecture.
     * @return PlatformInfo with os (linux/macos/windows) and arch (amd64/arm64).
     */
    static PlatformInfo get_platform_info();

    /**
     * @brief Get the current application version string.
     * @return Version string (e.g. "1.2.3" or "dev").
     */
    static std::string get_current_version();

    /**
     * @brief Fetch the latest release information from GitHub.
     * @return UpdateInfo with version, changelog, and release JSON.
     * @throws std::runtime_error on network/parse errors or API rate limits.
     */
    static UpdateInfo fetch_latest_release();

    /**
     * @brief Fetch the latest release information with custom curl timeouts.
     *
     * Used by the startup update check, which needs short timeouts so it never
     * noticeably blocks the user. curl honors the last occurrence of a timeout
     * flag, so the supplied values override the defaults (30s/300s) baked into
     * execute_curl() for this call only.
     *
     * @param connect_timeout_s connect timeout in seconds (curl --connect-timeout).
     * @param max_time_s        overall request timeout in seconds (curl --max-time).
     * @return UpdateInfo with version, changelog, and release JSON.
     * @throws std::runtime_error on network/parse errors or API rate limits.
     */
    static UpdateInfo fetch_latest_release(int connect_timeout_s, int max_time_s);

    /**
     * @brief Perform a best-effort update check (used on startup).
     *
     * Fetches the latest release (with short timeouts) and compares it against
     * the current version. The returned UpdateCheckResult.info has a value when
     * a newer version exists, or when the current build is a dev build (dev
     * builds always compare as older than any release). fetch_succeeded is true
     * when the network round-trip completed (regardless of whether an update
     * was found) — callers should only persist the throttle timestamp when this
     * is true, so offline failures are retried sooner. This NEVER throws and
     * never crashes the app on offline.
     *
     * @param connect_timeout_s connect timeout in seconds (default 3).
     * @param max_time_s        overall request timeout in seconds (default 8).
     * @return UpdateCheckResult with optional UpdateInfo and a fetch_succeeded flag.
     */
    static UpdateCheckResult check_for_update(int connect_timeout_s = 3, int max_time_s = 8);

    /**
     * @brief Resolve the per-OS path to the update-check state file.
     *
     * Linux:   $XDG_CONFIG_HOME/torrent-builder/update_check.state
     *          (fallback ~/.config/torrent-builder/update_check.state)
     * macOS:   ~/Library/Application Support/torrent-builder/update_check.state
     * Windows: %LOCALAPPDATA%\torrent-builder\update_check.state
     *
     * @return Absolute path, or empty string if no home/config dir is resolvable.
     */
    static std::string get_update_check_state_path();

    /**
     * @brief Decide whether a network update check should run now.
     *
     * Reads the last_check timestamp from the state file. Returns true when
     * more than @p interval_h hours have elapsed (or the file is missing /
     * corrupt / unreadable, treated as "never checked").
     *
     * @param interval_h minimum hours between checks (default 24).
     * @return true if a check should be performed now.
     */
    static bool should_check_for_updates(int interval_h = 24);

    /**
     * @brief Persist the current time as the last-check timestamp.
     *
     * Writes atomically (temp file + rename). Should be called after any
     * successful (non-throwing) network round-trip — including when the build
     * is already up-to-date — so the throttle works for the common case.
     * No-op when get_update_check_state_path() returns empty.
     */
    static void record_update_check();

    /**
     * @brief Find the matching platform asset URL from a JSON string.
     * @param release_json JSON string from GitHub API.
     * @param platform Target platform info.
     * @return Browser download URL for the matching asset, or std::nullopt.
     */
    static std::optional<std::string> find_matching_asset(const std::string &release_json, const PlatformInfo &platform);

    /**
     * @brief Find the matching platform asset URL from a parsed JSON object.
     * @param release_json Parsed JSON from GitHub API.
     * @param platform Target platform info.
     * @return Browser download URL for the matching asset, or std::nullopt.
     */
    static std::optional<std::string> find_matching_asset_from_json(const nlohmann::json &release_json, const PlatformInfo &platform);

    /**
     * @brief Get the size of a specific asset from the release JSON.
     * @param release_json Parsed JSON from GitHub API.
     * @param asset_url Browser download URL of the asset.
     * @return Asset size in bytes, or 0 if not found.
     */
    static int64_t get_asset_size(const nlohmann::json &release_json, const std::string &asset_url);

    /**
     * @brief Download an asset to a local path using curl.
     * @param url Browser download URL.
     * @param dest_path Local filesystem path for the downloaded file.
     * @param expected_size Expected file size in bytes (0 to skip size limit).
     * @return true if download succeeded, false otherwise.
     */
    static bool download_asset(const std::string &url, const std::string &dest_path, int64_t expected_size = 0);

    /**
     * @brief Perform the binary swap (current → .old backup, new → current).
     *
     * The .old backup is retained on success so that perform_rollback() can
     * restore the previous version. A stale .old from an earlier update is
     * replaced atomically at the start of the next call.
     *
     * @param new_binary_path Path to the downloaded new binary. Ownership
     *                        transfers on PendingRestart (the async helper
     *                        will move and delete it); caller retains
     *                        ownership on Success.
     * @param current_binary_path Path to the currently running binary.
     * @return UpdateResult::Success on synchronous completion,
     *         UpdateResult::PendingRestart when an async helper is scheduled.
     * @throws std::runtime_error on filesystem errors.
     */
    static UpdateResult perform_update(const std::string &new_binary_path, const std::string &current_binary_path);

    /**
     * @brief Roll back to the previous version (.old backup).
     * @param current_binary_path Path to the currently running binary.
     * @return UpdateResult::Success on synchronous completion,
     *         UpdateResult::PendingRestart when an async helper is scheduled (Windows).
     * @throws std::runtime_error if no backup exists or swap fails.
     */
    static UpdateResult perform_rollback(const std::string &current_binary_path);

    /**
     * @brief Locate the curl executable on the system.
     * @return Absolute path to curl, or "curl" as fallback.
     */
    static std::string find_curl_executable();

    /**
     * @brief Execute a curl request and return the response body.
     * @param url URL to fetch.
     * @param extra_args Additional curl CLI arguments.
     * @return Response body string.
     * @throws std::runtime_error on curl execution failure or unsafe URL.
     */
    static std::string execute_curl(const std::string &url, const std::vector<std::string> &extra_args = {});

    /**
     * @brief Fetch checksums.txt content from the latest GitHub release.
     * @return Checksums file content, or std::nullopt if not available.
     */
    static std::optional<std::string> fetch_checksums();

    /**
     * @brief Fetch checksums.txt content from a parsed release JSON (avoids redundant API call).
     * @param release_json Already-fetched release JSON.
     * @return Checksums file content, or std::nullopt if not available.
     */
    static std::optional<std::string> fetch_checksums(const nlohmann::json &release_json);

    /**
     * @brief Look up the SHA-256 hash for a specific asset in checksums content.
     * @param checksums_content Content of checksums.txt.
     * @param asset_name Filename to search for.
     * @return Lowercase hex hash string, or std::nullopt if not found.
     */
    static std::optional<std::string> lookup_checksum(const std::string &checksums_content, const std::string &asset_name);

    /**
     * @brief Verify a file's SHA-256 checksum against an expected hash.
     * @param filepath Path to the file to verify.
     * @param expected_hash Expected lowercase hex SHA-256 hash.
     * @return true if the checksum matches, false otherwise.
     */
    static bool verify_checksum(const std::string &filepath, const std::string &expected_hash);

    /**
     * @brief Get the path to the currently running executable.
     * @return Absolute path to the executable, or "torrent_builder" as fallback.
     */
    static std::string get_exe_path();

    /** @brief Function type override for binary validation in tests. */
    using BinaryValidator = std::function<bool(const std::string &filepath)>;

    /** @brief Override curl execution for testing. */
    static void set_curl_executor_for_testing(CurlExecutor executor);
    /** @brief Override file download for testing. */
    static void set_file_downloader_for_testing(FileDownloader downloader);
    /** @brief Override executable path for testing. */
    static void set_exe_path_provider_for_testing(ExePathProvider provider);
    /** @brief Override current version string for testing. */
    static void set_current_version_for_testing(const std::string &version);
    /** @brief Override binary validation for testing. */
    static void set_binary_validator_for_testing(BinaryValidator validator);
    /** @brief Override the update-check state file path for testing (empty optional = real path). */
    static void set_update_check_state_path_for_testing(std::optional<std::string> path);
    /** @brief Reset all test overrides to defaults. */
    static void reset_test_overrides();

    /**
     * @brief Validate that a URL is safe to pass to a shell command.
     * @param url URL to validate.
     * @return true if the URL starts with https:// and contains only safe characters.
     */
    static bool is_safe_url(const std::string &url);

    /**
     * @brief Validate that a file starts with a known binary format magic signature.
     * @param filepath Path to the file to validate.
     * @return true if the file starts with ELF, Mach-O, PE, or ZIP (macOS) magic bytes.
     */
    static bool is_valid_binary(const std::string &filepath);

    /**
     * @brief Verify a downloaded file exists, is non-empty, and matches expected size.
     * @param path Path to the downloaded file.
     * @param expected_size Expected size in bytes (0 to skip size check).
     * @return true if file is valid, false otherwise.
     */
    static bool verify_downloaded_file(const std::string &path, int64_t expected_size);

    /**
     * @brief Build the command string used to launch a batch script asynchronously on Windows.
     * The script path is passed via the TB_LAUNCH_SCRIPT environment variable to avoid
     * cmd.exe expanding %VAR% patterns that may appear in the path.
     * @return The command string for cmd.exe.
     */
    static std::string build_script_launch_cmd();

private:
    static std::string get_old_binary_path(const std::string &current_path);
    static bool extract_macos_zip(const std::string &zip_path, const std::string &dest_dir);
    static bool make_executable(const std::string &path);
};

#endif
