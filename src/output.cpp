#include "output.hpp"
#include "utils.hpp"
#include <iostream>
#include <cmath>

static Verbosity g_verbosity = Verbosity::NORMAL;
static bool g_json_mode = false;

void set_verbosity(Verbosity level) { g_verbosity = level; }
Verbosity get_verbosity() { return g_verbosity; }
void set_json_mode(bool enabled) { g_json_mode = enabled; }
bool is_json_mode() { return g_json_mode; }

void StdoutOutputSink::info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (g_verbosity == Verbosity::QUIET || g_json_mode) return;
    std::cout << msg;
}

void StdoutOutputSink::verbose(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (g_verbosity != Verbosity::VERBOSE) return;
    std::cout << msg;
}

void StdoutOutputSink::error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << msg;
}

void StdoutOutputSink::progress(const ProgressInfo& p) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (g_verbosity == Verbosity::QUIET || g_json_mode) return;

    const int bar_width = 50;
    float pct = p.pieces_total > 0
        ? static_cast<float>(p.pieces_done) / p.pieces_total
        : 0.0f;
    int filled = static_cast<int>(std::round(bar_width * pct));

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        std::cout << (i < filled ? "=" : " ");
    }
    std::cout << "] " << static_cast<int>(pct * 100) << "% ";
    std::cout << utils::format_size(p.bytes_done) << " / " << utils::format_size(p.bytes_total) << " ";
    std::cout << "Speed: " << utils::format_speed(p.speed_bps) << " ";
    std::cout << "ETA: " << utils::format_eta(p.eta_s) << "\r";
    std::cout.flush();
}

void StdoutOutputSink::progress_line_end() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (g_verbosity == Verbosity::QUIET || g_json_mode) return;
    std::cout << '\n';
}

OutputSink& default_stdout_sink() {
    static StdoutOutputSink instance;
    return instance;
}

// Thin wrappers preserving the pre-refactor free-function API.
// Every existing CLI call site goes through these, so no callers change.
void print_info(const std::string& msg) {
    default_stdout_sink().info(msg);
}

void print_verbose(const std::string& msg) {
    default_stdout_sink().verbose(msg);
}

void print_error(const std::string& msg) {
    default_stdout_sink().error(msg);
}

void print_progress(int progress, int total, double speed, double eta,
                    int64_t processed, int64_t total_size) {
    default_stdout_sink().progress(ProgressInfo{
        progress, total, speed, eta, processed, total_size, ""
    });
}
