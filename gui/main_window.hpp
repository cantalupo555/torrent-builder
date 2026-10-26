#ifndef GUI_MAIN_WINDOW_HPP
#define GUI_MAIN_WINDOW_HPP

#include "qt_output_sink.hpp"
#include "torrent_creator.hpp"
#include "torrent_inspector.hpp"

#include <QMainWindow>
#include <QString>
#include <memory>
#include <thread>

class QTabWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;

/**
 * @brief Main application window with two tabs: "Criar" and "Inspecionar".
 *
 * The Create tab hosts a form (path, output, trackers, version, flags) and
 * runs TorrentCreator::create_torrent() on a worker thread. Output is bridged
 * by QtOutputSink into a colored log pane and a progress row (bar + speed +
 * ETA). The Cancel button flips the sink's cancellation flag, which the worker
 * polls via its should_cancel predicate and aborts with UserInterrupt.
 *
 * Run lifecycle is driven exclusively by the RunState state machine below:
 * only the worker's outcome signals (finished/failed) transition out of
 * Running/Cancelling — log messages never do. This replaces the earlier
 * magic-string sentinel and text-comparison heuristics.
 *
 * The Inspect tab loads a .torrent file synchronously and displays the
 * metadata produced by TorrentInspector::format_metadata() — a pure
 * library call with no worker thread needed.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ── Create tab ───────────────────────────────────────────
    void browse_input_file();
    void browse_input_dir();
    void browse_output();
    void start_creation();
    void on_creation_finished();
    void on_creation_failed(const QString& what);
    void cancel_creation();

    void on_progress(const QtProgress& p);
    void on_info(const QString& msg);
    void on_verbose(const QString& msg);
    void on_error(const QString& msg);

    // ── Inspect tab ──────────────────────────────────────────
    void browse_inspect_target();
    void run_inspect();

private:
    /// Lifecycle of one creation run. Single source of truth — status label,
    /// button enablement and cancel-vs-success disambiguation all derive from
    /// this; no widget text is ever compared to decide transitions.
    enum class RunState {
        Idle,       ///< No run yet or reset after a finished one.
        Running,    ///< Worker hashing.
        Cancelling, ///< Cancel requested, waiting for the worker to abort.
        Succeeded,  ///< Worker reported success.
        Failed,     ///< Worker reported an error.
        Cancelled   ///< Worker aborted after user request.
    };

    void build_create_tab();
    void build_inspect_tab();
    void apply_run_state(RunState state, const QString& detail = {});
    void append_log(const QString& color, const QString& msg);
    void join_worker();
    void save_window_settings();

    // ── Create tab widgets ───────────────────────────────────
    QWidget* create_tab_ = nullptr;
    QLineEdit* input_path_edit_ = nullptr;
    QLineEdit* output_path_edit_ = nullptr;
    QComboBox* version_combo_ = nullptr;
    QCheckBox* private_check_ = nullptr;
    QLineEdit* comment_edit_ = nullptr;
    QLineEdit* source_edit_ = nullptr;
    QCheckBox* entropy_check_ = nullptr;
    QPlainTextEdit* trackers_edit_ = nullptr;
    QPlainTextEdit* web_seeds_edit_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLabel* progress_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* create_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;

    // ── Inspect tab widgets ──────────────────────────────────
    QWidget* inspect_tab_ = nullptr;
    QLineEdit* inspect_path_edit_ = nullptr;
    QPlainTextEdit* inspect_view_ = nullptr;

    // ── Worker state ─────────────────────────────────────────
    QtOutputSink* sink_ = nullptr;          ///< Owned by this (QObject parent)
    std::unique_ptr<std::thread> worker_;   ///< Null when idle
    RunState run_state_ = RunState::Idle;   ///< Single source of truth
    bool close_pending_ = false;            ///< A close was deferred until the worker finishes
};

#endif // GUI_MAIN_WINDOW_HPP
