#include "batch.hpp"
#include "creation_planner.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include <yaml-cpp/yaml.h>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <sstream>

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

std::optional<fs::path> resolve_explicit_output(const BatchJob& job,
                                               const std::optional<fs::path>& output_dir)
{
    if (!job.output) {
        return std::nullopt;
    }

    fs::path output(*job.output);
    if (output.is_relative() && output_dir && !output_dir->empty()) {
        output = *output_dir / output;
    }

    return output;
}

void ensure_output_parent(const fs::path& output)
{
    if (output.parent_path().empty()) {
        return;
    }

    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    if (ec) {
        throw std::runtime_error("Failed to create output directory: " + output.parent_path().string() + " (" + ec.message() + ")");
    }
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

        if (job_node["fail_on_season_warning"]) {
            job.fail_on_season_warning = job_node["fail_on_season_warning"].as<bool>();
        }

        job.path = *job.values.path;
        config.jobs.push_back(std::move(job));
    }

    log_message("Parsed batch config: " + std::to_string(config.jobs.size())
        + " jobs, " + std::to_string(config.workers) + " workers", LogLevel::INFO);

    return config;
}

BatchResult BatchProcessor::execute_job(int job_index, const PlannerResources& resources, OutputSink& sink)
{
    const BatchJob& job = config_.jobs[job_index];
    BatchResult result;
    result.job_index = job_index;
    result.job_name = job.output.value_or(job.path);
    result.success = false;

    auto start = std::chrono::steady_clock::now();

    try {
        CreationRequest request;
        request.input = fs::path(job.path);
        request.values = job.values;

        // Validate before creating output directories so a typo in `path`
        // does not leave empty directories behind.
        if (!fs::exists(request.input)) {
            throw std::runtime_error("Path does not exist: " + request.input.string());
        }

        request.preset = job.preset;
        request.output = resolve_explicit_output(job, config_.output_dir);
        request.output_dir = config_.output_dir;
        request.fail_on_season_warning = job.fail_on_season_warning;
        request.silent = true;

        CreationPlanner planner(resources);
        CreationPlan plan = planner.plan(request, sink);
        ensure_output_parent(plan.config.output);

        TorrentCreator creator(std::move(plan.config), sink);
        creator.create_torrent();

        result.success = true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    return result;
}

std::vector<BatchResult> BatchProcessor::run(OutputSink& sink)
{
    auto presets = std::make_shared<PresetLoader>();
    try {
        auto preset_path = PresetLoader::find_preset_file(config_.preset_file);
        presets->load(preset_path);
    } catch (const std::runtime_error& e) {
        log_message("Preset file not available: " + std::string(e.what()), LogLevel::WARNING);
        // load() may have populated part of the file before failing; jobs must
        // not run against a partially loaded preset set.
        presets = std::make_shared<PresetLoader>();
    }

    auto rules = std::make_shared<TrackerRulesDatabase>();
    try {
        auto rules_path = TrackerRulesDatabase::find_rules_file(config_.rules_file);
        rules->load(rules_path);
    } catch (const std::runtime_error& e) {
        log_message("Rules file not available: " + std::string(e.what()), LogLevel::WARNING);
        // Same rationale as presets: never use a partially loaded rules DB.
        rules = std::make_shared<TrackerRulesDatabase>();
    }

    // Empty-on-failure loaders are injected so the planner does not rediscover
    // presets.yaml/rules.yaml per job and batch keeps its warn-and-continue
    // behavior when those files are missing.
    const PlannerResources resources{presets, rules};

    std::vector<BatchResult> results(config_.jobs.size());
    std::atomic<int> next_job{0};

    auto worker = [&]() {
        while (true) {
            int idx = next_job.fetch_add(1);
            if (idx >= static_cast<int>(config_.jobs.size())) break;

            // Tag every planner/creator log line with the job number so
            // concurrent workers stay distinguishable in the log file.
            ScopedLogContext context("Job " + std::to_string(idx + 1));

            log_message("started: " + sanitize_for_terminal(config_.jobs[idx].path), LogLevel::INFO);

            results[idx] = execute_job(idx, resources, sink);

            if (results[idx].success) {
                log_message("completed (" + std::to_string(results[idx].elapsed_seconds) + "s)",
                    LogLevel::INFO);
            } else {
                log_message("failed: " + sanitize_for_terminal(results[idx].error_message),
                    LogLevel::ERR);
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
