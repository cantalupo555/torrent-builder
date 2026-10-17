#ifndef OUTPUT_SINK_HPP
#define OUTPUT_SINK_HPP

#include <string>
#include <cstdint>

/**
 * @brief Structured progress information emitted during long operations.
 *
 * Carries everything a frontend (CLI bar, TUI, or Qt GUI) needs to render
 * progress. Producers (TorrentCreator, TorrentChecker) populate it;
 * consumers (OutputSink implementations) format and display it.
 */
struct ProgressInfo {
    int pieces_done = 0;        ///< Pieces completed so far.
    int pieces_total = 0;       ///< Total number of pieces.
    double speed_bps = 0.0;     ///< Hashing/verification speed in bytes per second.
    double eta_s = 0.0;         ///< Estimated time remaining in seconds.
    int64_t bytes_done = 0;     ///< Bytes processed so far.
    int64_t bytes_total = 0;    ///< Total bytes to process.
    std::string current_file;   ///< File currently being processed (optional, empty if N/A).
};

/**
 * @brief Abstract output sink decoupling business logic from presentation.
 *
 * The core library (TorrentCreator, BatchProcessor, TorrentChecker) reports
 * progress and messages exclusively through this interface. Concrete sinks:
 *
 * - `StdoutOutputSink` (default): the legacy CLI behavior, honors verbosity
 *   and JSON mode. Lives in output.cpp.
 * - `QtOutputSink` (GUI): emits Qt signals for rendering into widgets.
 *
 * This is the seam that allows multiple frontends to share the same business
 * logic without the core touching std::cout/std::cerr directly.
 *
 * Design note: this deliberately diverges from Updater's static-override DI
 * style (set_*_for_testing). OutputSink is reference-injected at construction,
 * which is a cleaner fit for stateful, multi-instance components like
 * TorrentCreator and matches the GUI's per-worker sink requirement.
 */
class OutputSink {
public:
    virtual ~OutputSink() = default;

    /**
     * @brief Emit an informational message.
     *
     * Suppressed by sinks under QUIET/JSON modes (CLI) — implementation-defined.
     * @param msg The message to display.
     */
    virtual void info(const std::string& msg) = 0;

    /**
     * @brief Emit a verbose diagnostic message.
     *
     * Only shown under VERBOSE verbosity. Implementation-defined for non-CLI sinks.
     * @param msg The diagnostic message.
     */
    virtual void verbose(const std::string& msg) = 0;

    /**
     * @brief Emit an error message.
     *
     * Always surfaced regardless of verbosity or mode (errors must never be swallowed).
     * @param msg The error message.
     */
    virtual void error(const std::string& msg) = 0;

    /**
     * @brief Emit a structured progress update.
     *
     * The sink decides how to render this: ASCII bar to stdout (CLI),
     * QProgressBar update (Qt GUI), or no-op (QUIET/JSON mode).
     * @param progress Populated ProgressInfo struct.
     */
    virtual void progress(const ProgressInfo& progress) = 0;

    /**
     * @brief Optional hook signalling the end of a transient progress line.
     *
     * Producers call this once after the final progress() update. Sinks that
     * render an in-place progress line (e.g. the CLI's '\r'-based bar) use it
     * to terminate that line, while others keep the default no-op. This keeps
     * CLI formatting conventions out of the core producer loop.
     */
    virtual void progress_line_end() {}
};

/// @brief Forward declaration of the default sink accessor.
/// Used as the default argument for sinks injected into TorrentCreator etc.
/// The full definition lives in src/output.cpp; output.hpp declares the
/// StdoutOutputSink class and the public accessor.
OutputSink& default_stdout_sink();

#endif // OUTPUT_SINK_HPP
