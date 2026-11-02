#ifndef GUI_CREATION_OPTIONS_HPP
#define GUI_CREATION_OPTIONS_HPP

#include "creation_planner.hpp"
#include "utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gui {

/**
 * @brief Creation options collected from the GUI form.
 *
 * Only maps widgets into a CreationRequest. Defaults, validation, presets,
 * tracker rules and auto-naming are resolved by CreationPlanner, exactly like
 * the CLI and the interactive wizard. Deliberately free of Qt types so it can
 * be unit tested in the plain gtest binary (no QApplication required).
 */
struct CreationOptions {
    fs::path input_path;
    fs::path output_path;
    std::vector<std::string> trackers;
    std::vector<std::string> web_seeds;
    TorrentVersion version = TorrentVersion::HYBRID;
    std::string comment;
    std::string source;
    bool is_private = false;
    bool entropy = false;

    /**
     * @brief Required-field and URL check for immediate UI feedback.
     *
     * Only covers what can be answered without filesystem access. The checks use
     * the same validator as the planner, which remains the single source of
     * truth for every other rule and runs on the worker thread.
     * @return One human-readable message per problem; empty when valid.
     */
    [[nodiscard]] std::vector<std::string> validate() const {
        std::vector<std::string> errors;
        if (input_path.empty()) errors.push_back("input path is empty");
        if (output_path.empty()) errors.push_back("output path is empty");
        for (const auto& url : trackers) {
            if (!utils::is_valid_url(url)) errors.push_back("invalid tracker URL: " + url);
        }
        for (const auto& url : web_seeds) {
            if (!utils::is_valid_web_seed_url(url)) errors.push_back("invalid web seed URL: " + url);
        }
        return errors;
    }

    /**
     * @brief Map the form into a CreationRequest for the planner.
     */
    [[nodiscard]] CreationRequest to_creation_request() const {
        CreationRequest request;
        request.input = input_path;
        request.output = output_path;
        // No rules picker yet: keep `discover_rules` false so a default rules
        // file cannot change GUI output behind the user's back.
        // The form is fully explicit: an empty list means "no trackers/seeds",
        // not "inherit from a preset".
        request.values.trackers = trackers;
        request.values.web_seeds = web_seeds;
        request.values.torrent_version = version_number(version);
        // The GUI has no excludes toggle yet; keep the pre-planner behavior of
        // including every file until #74 exposes the toggle.
        request.values.builtin_excludes = false;
        request.values.comment =
            comment.empty() ? std::nullopt : std::optional<std::string>(comment);
        request.values.source =
            source.empty() ? std::nullopt : std::optional<std::string>(source);
        request.values.is_private = is_private;
        request.values.entropy = entropy;
        return request;
    }

  private:
    [[nodiscard]] static int version_number(TorrentVersion version) {
        switch (version) {
            case TorrentVersion::V1:
                return 1;
            case TorrentVersion::V2:
                return 2;
            case TorrentVersion::HYBRID:
                return 3;
        }
        return 3;
    }
};

} // namespace gui

#endif // GUI_CREATION_OPTIONS_HPP
