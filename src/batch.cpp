#include "batch.hpp"
#include "preset.hpp"
#include "logger.hpp"
#include "output.hpp"
#include "utils.hpp"
#include "constants.hpp"
#include <yaml-cpp/yaml.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace
{

std::string sanitize_for_terminal(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 0x20 && c != 0x7f) {
            result += static_cast<char>(c);
        }
    }
    return result;
}

std::optional<int> validate_piece_size(int ps_kb)
{
    static const std::vector<int> allowed(AllowedPieceSizes::values.begin(),
                                          AllowedPieceSizes::values.end());
    if (ps_kb == 0) return std::nullopt;
    if (std::ranges::contains(allowed, ps_kb)) {
        return ps_kb * 1024;
    }
    throw std::runtime_error("Invalid piece size: " + std::to_string(ps_kb) + " KB");
}

TorrentVersion resolve_version(int v)
{
    switch (v) {
        case 1: return TorrentVersion::V1;
        case 2: return TorrentVersion::V2;
        default: return TorrentVersion::HYBRID;
    }
}

std::vector<std::regex> compile_patterns(const std::optional<std::vector<std::string>>& patterns)
{
    std::vector<std::regex> compiled;
    if (patterns) {
        for (const auto& p : *patterns) {
            try {
                compiled.push_back(utils::glob_to_regex(p));
            } catch (const std::regex_error&) {
                throw std::runtime_error("Invalid pattern: " + p);
            }
        }
    }
    return compiled;
}

TorrentConfig build_torrent_config(const ConfigValues& cv, const fs::path& default_output_dir)
{
    fs::path input_path(cv.path.value());
    if (!fs::exists(input_path)) {
        throw std::runtime_error("Path does not exist: " + input_path.string());
    }

    fs::path output;
    if (cv.output) {
        output = *cv.output;
        if (output.is_relative() && !default_output_dir.empty()) {
            output = default_output_dir / output;
        }
        if (!output.parent_path().empty()) {
            std::error_code ec;
            fs::create_directories(output.parent_path(), ec);
            if (ec) {
                throw std::runtime_error("Failed to create output directory: " + output.parent_path().string() + " (" + ec.message() + ")");
            }
        }
    } else {
        auto trackers = cv.trackers.value_or(std::vector<std::string>{});
        fs::path out_dir = default_output_dir.empty() ? fs::current_path() : default_output_dir;
        if (!out_dir.empty() && !fs::exists(out_dir)) {
            std::error_code ec;
            fs::create_directories(out_dir, ec);
            if (ec) {
                throw std::runtime_error("Failed to create output directory: " + out_dir.string() + " (" + ec.message() + ")");
            }
        }
        output = utils::generate_auto_output_path(input_path, trackers, false, 0, out_dir);
    }

    TorrentVersion version = TorrentVersion::HYBRID;
    if (cv.torrent_version) {
        version = resolve_version(*cv.torrent_version);
    }

    std::optional<int> piece_size_bytes;
    if (cv.piece_size && *cv.piece_size > 0) {
        piece_size_bytes = validate_piece_size(*cv.piece_size);
    }

    std::optional<std::string> creator;
    if (cv.creator) {
        creator = *cv.creator;
    }

    return TorrentConfig(
        input_path,
        output,
        cv.trackers.value_or(std::vector<std::string>{}),
        version,
        cv.comment,
        cv.is_private.value_or(false),
        cv.web_seeds.value_or(std::vector<std::string>{}),
        piece_size_bytes,
        creator,
        cv.name,
        cv.creation_date.value_or(false),
        cv.source,
        cv.entropy.value_or(false),
        compile_patterns(cv.exclude_patterns),
        compile_patterns(cv.include_patterns)
    );
}

}

ConfigValues parse_yaml_config(const YAML::Node& node);

namespace
{

ConfigValues parse_job_node(const YAML::Node& node)
{
    ConfigValues cv = parse_yaml_config(node);

    if (node["path"]) cv.path = node["path"].as<std::string>();
    if (node["output"]) cv.output = node["output"].as<std::string>();

    return cv;
}

}

BatchProcessor::BatchProcessor(BatchConfig config)
    : config_(std::move(config))
{
}

BatchConfig BatchProcessor::parse(const fs::path& yaml_path)
{
    if (!fs::exists(yaml_path)) {
        throw std::runtime_error("Batch file not found: " + yaml_path.string());
    }

    static constexpr std::uintmax_t max_yaml_size = 1 * 1024 * 1024;
    if (fs::file_size(yaml_path) > max_yaml_size) {
        throw std::runtime_error("Batch file too large (max 1 MB): " + yaml_path.string());
    }

    YAML::Node root = YAML::LoadFile(yaml_path.string());

    if (!root["version"] || root["version"].as<int>() != 1) {
        throw std::runtime_error("Unsupported batch file version (expected: 1)");
    }

    BatchConfig config;

    if (root["workers"]) {
        config.workers = root["workers"].as<int>();
        if (config.workers < 1) {
            throw std::runtime_error("Workers must be >= 1");
        }
    }

    if (root["preset_file"]) {
        config.preset_file = root["preset_file"].as<std::string>();
    }

    if (root["rules_file"]) {
        config.rules_file = root["rules_file"].as<std::string>();
    }

    if (root["output_dir"]) {
        config.output_dir = root["output_dir"].as<std::string>();
    }

    if (!root["jobs"] || !root["jobs"].IsSequence()) {
        throw std::runtime_error("Batch file must contain a 'jobs' list");
    }

    for (const auto& job_node : root["jobs"]) {
        BatchJob job;
        job.values = parse_job_node(job_node);

        if (!job.values.path) {
            throw std::runtime_error("Each batch job must have a 'path' field");
        }

        if (job_node["output"]) {
            std::string out = job_node["output"].as<std::string>();
            if (out.size() < 8 || utils::to_lower(out.substr(out.size() - 8)) != ".torrent") {
                out += ".torrent";
            }
            job.output = std::move(out);
        }

        if (job_node["preset"]) {
            job.preset = job_node["preset"].as<std::string>();
        }

        job.path = *job.values.path;
        config.jobs.push_back(std::move(job));
    }

    log_message("Parsed batch config: " + std::to_string(config.jobs.size())
        + " jobs, " + std::to_string(config.workers) + " workers", LogLevel::INFO);

    return config;
}

BatchResult BatchProcessor::execute_job(int job_index, const PresetLoader& presets, const TrackerRulesDatabase& rules)
{
    const BatchJob& job = config_.jobs[job_index];
    BatchResult result;
    result.job_index = job_index;
    result.job_name = job.output.value_or(job.path);
    result.success = false;

    auto start = std::chrono::steady_clock::now();

    try {
        ConfigValues resolved;

        if (job.preset) {
            resolved = presets.resolve(*job.preset);
        }

        resolved = merge_config_values(resolved, job.values);

        if (!resolved.path) {
            resolved.path = job.path;
        }
        if (!resolved.output && job.output) {
            resolved.output = *job.output;
        }

        auto trackers = resolved.trackers.value_or(std::vector<std::string>{});
        if (!trackers.empty()) {
            auto matched_rule = rules.find_matching_rule(trackers);
            if (matched_rule) {
                if (matched_rule->source && !resolved.source) {
                    resolved.source = *matched_rule->source;
                    log_message("Job " + std::to_string(job_index + 1) + ": rule '"
                        + matched_rule->name + "' auto-set source to '" + *resolved.source + "'", LogLevel::INFO);
                }

                if (matched_rule->max_piece_length || matched_rule->max_torrent_size || !matched_rule->piece_length_overrides.empty()) {
                    int64_t total_size = 0;
                    try {
                        fs::path content_path(*resolved.path);
                        if (fs::is_directory(content_path)) {
                            for (const auto& entry : fs::recursive_directory_iterator(content_path)) {
                                if (entry.is_regular_file()) total_size += entry.file_size();
                            }
                        } else {
                            total_size = fs::file_size(content_path);
                        }
                    } catch (const fs::filesystem_error& e) {
                        log_message("Job " + std::to_string(job_index + 1) + ": could not compute content size: " + std::string(e.what()), LogLevel::WARNING);
                    }

                    std::optional<int> current_kb;
                    if (resolved.piece_size && *resolved.piece_size > 0) {
                        current_kb = *resolved.piece_size;
                    }

                    auto enforcement = rules.enforce(*matched_rule, total_size, current_kb);

                    if (enforcement.adjusted && enforcement.adjusted_piece_length) {
                        if (current_kb) {
                            std::string limit_info;
                            if (matched_rule->max_piece_length) {
                                limit_info = "max_piece_length (" + std::to_string(*matched_rule->max_piece_length / 1024) + " KB)";
                            } else {
                                limit_info = "rule constraint";
                            }
                            log_message("Job " + std::to_string(job_index + 1) + ": rule '"
                                + matched_rule->name + "': user-specified piece size ("
                                + std::to_string(*current_kb) + " KB) adjusted by " + limit_info, LogLevel::WARNING);
                        } else {
                            resolved.piece_size = *enforcement.adjusted_piece_length;
                        }
                    }

                    if (enforcement.constraint_violation) {
                        log_message("Job " + std::to_string(job_index + 1) + ": " + enforcement.violation_message, LogLevel::WARNING);
                    }
                }
            } else {
                log_message("Job " + std::to_string(job_index + 1) + ": no matching rule found for configured trackers", LogLevel::INFO);
            }
        }

        fs::path output_dir;
        if (config_.output_dir) {
            output_dir = *config_.output_dir;
        }

        TorrentConfig tc = build_torrent_config(resolved, output_dir);
        tc.silent = true;
        TorrentCreator creator(std::move(tc));
        creator.create_torrent();

        result.success = true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    return result;
}

std::vector<BatchResult> BatchProcessor::run()
{
    PresetLoader presets;
    try {
        auto preset_path = PresetLoader::find_preset_file(config_.preset_file);
        presets.load(preset_path);
    } catch (const std::runtime_error& e) {
        log_message("Preset file not available: " + std::string(e.what()), LogLevel::WARNING);
    }

    TrackerRulesDatabase rules;
    try {
        auto rules_path = TrackerRulesDatabase::find_rules_file(config_.rules_file);
        rules.load(rules_path);
    } catch (const std::runtime_error& e) {
        log_message("Rules file not available: " + std::string(e.what()), LogLevel::WARNING);
    }

    std::vector<BatchResult> results(config_.jobs.size());
    std::atomic<int> next_job{0};

    auto worker = [&]() {
        while (true) {
            int idx = next_job.fetch_add(1);
            if (idx >= static_cast<int>(config_.jobs.size())) break;

            log_message("Job " + std::to_string(idx + 1) + " started: "
                + sanitize_for_terminal(config_.jobs[idx].path), LogLevel::INFO);

            results[idx] = execute_job(idx, presets, rules);

            if (results[idx].success) {
                log_message("Job " + std::to_string(idx + 1) + " completed ("
                    + std::to_string(results[idx].elapsed_seconds) + "s)", LogLevel::INFO);
            } else {
                log_message("Job " + std::to_string(idx + 1) + " failed: "
                    + sanitize_for_terminal(results[idx].error_message), LogLevel::ERR);
            }
        }
    };

    int actual_workers = std::min(config_.workers, static_cast<int>(config_.jobs.size()));
    const int max_workers = static_cast<int>(std::thread::hardware_concurrency()) * 2;
    if (actual_workers > max_workers && max_workers > 0) {
        actual_workers = max_workers;
    }
    log_message("Starting batch execution: " + std::to_string(config_.jobs.size())
        + " jobs, " + std::to_string(actual_workers) + " workers", LogLevel::INFO);
    std::vector<std::thread> threads;
    threads.reserve(actual_workers);

    for (int i = 0; i < actual_workers; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    return results;
}

void BatchProcessor::print_summary(const std::vector<BatchResult>& results)
{
    int succeeded = 0;
    int failed = 0;

    for (const auto& r : results) {
        if (r.success) ++succeeded;
        else ++failed;
    }

    std::ostringstream oss;
    oss << "\nBatch Summary (" << results.size() << " jobs):\n";

    for (const auto& r : results) {
        std::ostringstream time_str;
        time_str << std::fixed << std::setprecision(1) << r.elapsed_seconds << "s";

        if (r.success) {
            oss << "  \u2713 " << sanitize_for_terminal(r.job_name);
            oss << "  completed (" << time_str.str() << ")\n";
        } else {
            oss << "  \u2717 " << sanitize_for_terminal(r.job_name);
            oss << "  FAILED: " << sanitize_for_terminal(r.error_message) << "\n";
        }
    }

    oss << "\n  Succeeded: " << succeeded << "    Failed: " << failed << "\n";

    std::cout << oss.str();
    if (failed > 0) {
        log_message("Batch completed with " + std::to_string(failed) + " failed job(s)", LogLevel::ERR);
    }
    log_message(oss.str(), LogLevel::INFO);
}
