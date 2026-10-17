#ifndef GUI_QT_OUTPUT_SINK_HPP
#define GUI_QT_OUTPUT_SINK_HPP

#include "output_sink.hpp"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>

/// Progress payload emitted to the UI thread. Mirrors ProgressInfo but with
/// Qt-friendly types so it can travel across a Qt::QueuedConnection without
/// touching std::string lifetime rules.
struct QtProgress {
    int pieces_done = 0;
    int pieces_total = 0;
    double speed_bps = 0.0;
    double eta_s = 0.0;
    long long bytes_done = 0;
    long long bytes_total = 0;
    QString current_file;
};

// Queued connections copy arguments through the metatype system. Qt 6 does not
// infer custom types at connect time: without this declaration the connection
// silently degrades and progress updates are dropped. main.cpp also registers
// the type at runtime before the first connection.
Q_DECLARE_METATYPE(QtProgress)

/**
 * @brief OutputSink implementation that bridges the core library to Qt widgets.
 *
 * Methods on this class are called from the worker thread (where
 * TorrentCreator::create_torrent() runs). They marshal their payloads onto Qt
 * signals, which Qt delivers to connected slots on the UI thread via
 * Qt::QueuedConnection. This is the standard thread-safe bridge and is the
 * ONLY sanctioned way to touch widgets from a non-UI thread.
 *
 * The sink also owns the cancellation flag the worker polls via its
 * should_cancel predicate. request_cancel() flips the flag from the UI thread;
 * the worker observes it on its next progress check and aborts.
 */
class QtOutputSink : public QObject, public OutputSink {
    Q_OBJECT

public:
    explicit QtOutputSink(QObject* parent = nullptr);

    // ── OutputSink interface (called from worker thread) ──────────────
    void info(const std::string& msg) override;
    void verbose(const std::string& msg) override;
    void error(const std::string& msg) override;
    void progress(const ProgressInfo& p) override;

    // ── Cancellation control (called from UI thread) ──────────────────
    /// Returns true once the user has clicked Cancel. Polled by the worker's
    /// should_cancel predicate.
    bool is_cancelled() const { return cancel_flag_.load(std::memory_order_relaxed); }

    /// Flips the cancellation flag. Idempotent.
    void request_cancel() { cancel_flag_.store(true, std::memory_order_relaxed); }

    /// Resets the cancellation flag so the sink can be reused for a new run.
    void reset() { cancel_flag_.store(false, std::memory_order_relaxed); }

signals:
    /// Emitted for every info() call. Connect to a QPlainTextEdit append.
    void infoMessage(const QString& msg);
    /// Emitted for every verbose() call. Usually shown only in a debug pane.
    void verboseMessage(const QString& msg);
    /// Emitted for every error() call. Rendered in red on the log pane; does
    /// NOT change run state — only the worker outcome below does.
    void errorMessage(const QString& msg);
    /// Emitted for every progress() call. Connect to QProgressBar + labels.
    void progressUpdated(const QtProgress& p);
    /// Emitted once when the worker finishes successfully or after a user
    /// cancellation (the UI tells the two apart via its own run state).
    void finished();
    /// Emitted once when the worker aborts with an unexpected error.
    void failed(const QString& what);

private:
    std::atomic<bool> cancel_flag_{false};
};

#endif // GUI_QT_OUTPUT_SINK_HPP
