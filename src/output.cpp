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

void print_info(const std::string& msg) {
    if (g_verbosity == Verbosity::QUIET || g_json_mode) return;
    std::cout << msg;
}

void print_verbose(const std::string& msg) {
    if (g_verbosity != Verbosity::VERBOSE) return;
    std::cout << msg;
}

void print_error(const std::string& msg) {
    std::cerr << msg;
}

void print_progress(int progress, int total, double speed, double eta,
                    int64_t processed, int64_t total_size) {
    if (g_verbosity == Verbosity::QUIET || g_json_mode) return;

    const int bar_width = 50;
    float pct = static_cast<float>(progress) / total;
    int filled = static_cast<int>(std::round(bar_width * pct));

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        std::cout << (i < filled ? "=" : " ");
    }
    std::cout << "] " << static_cast<int>(pct * 100) << "% ";
    std::cout << utils::format_size(processed) << " / " << utils::format_size(total_size) << " ";
    std::cout << "Speed: " << utils::format_speed(speed) << " ";
    std::cout << "ETA: " << utils::format_eta(eta) << "\r";
    std::cout.flush();
}
