#include "main_window.hpp"

#include "creation_options.hpp"
#include "formatting.hpp"
#include "terminal.hpp"  // UserInterrupt
#include "version.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

/// Log colors for append_log(). Chosen to stay readable on both light and
/// dark platform themes.
constexpr auto kLogInfoColor = "";        ///< Default palette color.
constexpr auto kLogVerboseColor = "#888888";
constexpr auto kLogErrorColor = "#d9534f";
constexpr auto kLogSuccessColor = "#2e7d32";

QString line_edit_text(const QLineEdit* edit) {
    return edit->text().trimmed();
}

std::vector<std::string> plain_edit_lines(const QPlainTextEdit* edit) {
    std::vector<std::string> lines;
    const auto text = edit->toPlainText();
    for (const auto& line : text.split(QLatin1Char('\n'))) {
        auto trimmed = line.trimmed();
        if (!trimmed.isEmpty()) lines.push_back(trimmed.toStdString());
    }
    return lines;
}

/// Converts UI text to a filesystem path without losing non-ASCII characters.
/// On Windows, fs::path must be built from wide chars (narrow = ANSI); on
/// POSIX, QString::toStdString() is UTF-8 and matches the native narrow form.
fs::path to_fs_path(const QString& text) {
#ifdef _WIN32
    return fs::path(text.toStdWString());
#else
    return fs::path(text.toStdString());
#endif
}

/**
 * @brief Line edit accepting dragged files/folders (drops the first URL).
 */
class DropLineEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        const auto urls = event->mimeData()->urls();
        if (urls.isEmpty() || !urls.first().isLocalFile()) {
            event->ignore();
            return;
        }
        setText(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
};

/**
 * @brief Trampoline run on the worker thread. Owns the TorrentCreator lifetime.
 *
 * Outcomes are marshaled back exclusively through the sink's lifecycle signals:
 * success/cancel → finished(), unexpected error → failed(what). Catching here
 * is mandatory: an uncaught exception in a std::thread calls std::terminate
 * and crashes the app. The UI tells cancel apart from success via its own
 * run state (Cancelling), not via message contents.
 */
void run_creation(TorrentConfig config, QtOutputSink* sink) {
    try {
        TorrentCreator creator(std::move(config), *sink,
                               [sink]() { return sink->is_cancelled(); });
        creator.create_torrent();
        QMetaObject::invokeMethod(sink, &QtOutputSink::finished, Qt::QueuedConnection);
    } catch (const UserInterrupt&) {
        // No message here: the UI derives "cancelled" from its own run state
        // and logs a translatable message in on_creation_finished().
        QMetaObject::invokeMethod(sink, &QtOutputSink::finished, Qt::QueuedConnection);
    } catch (const std::exception& e) {
        QMetaObject::invokeMethod(sink, &QtOutputSink::failed,
            Qt::QueuedConnection, QString::fromUtf8(e.what()));
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    sink_ = new QtOutputSink(this);

    build_create_tab();
    build_inspect_tab();

    auto* tabs = new QTabWidget(this);
    tabs->addTab(create_tab_, tr("Criar"));
    tabs->addTab(inspect_tab_, tr("Inspecionar"));
    setCentralWidget(tabs);

    setWindowTitle(tr("torrent-builder %1").arg(QStringLiteral(TORRENT_BUILDER_VERSION)));

    // Wire sink signals to UI slots. Queued delivery is automatic because the
    // sink emits from a non-UI thread; main.cpp registers QtProgress via
    // qRegisterMetaType before this connection so the payload can cross threads.
    connect(sink_, &QtOutputSink::progressUpdated, this, &MainWindow::on_progress);
    connect(sink_, &QtOutputSink::infoMessage, this, &MainWindow::on_info);
    connect(sink_, &QtOutputSink::verboseMessage, this, &MainWindow::on_verbose);
    connect(sink_, &QtOutputSink::errorMessage, this, &MainWindow::on_error);
    connect(sink_, &QtOutputSink::finished, this, &MainWindow::on_creation_finished);
    connect(sink_, &QtOutputSink::failed, this, &MainWindow::on_creation_failed);

    apply_run_state(RunState::Idle);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainwindow/geometry")).toByteArray());
    input_path_edit_->setText(
        settings.value(QStringLiteral("paths/input")).toString());
    output_path_edit_->setText(
        settings.value(QStringLiteral("paths/output")).toString());
    inspect_path_edit_->setText(
        settings.value(QStringLiteral("paths/inspect")).toString());
}

MainWindow::~MainWindow() {
    if (worker_ && worker_->joinable()) {
        // Last-resort join for teardown paths that bypass closeEvent, such as
        // QApplication::quit() while a creation is running. Blocking the UI
        // thread here is deliberate: detaching the worker would let it call
        // back into this window's sink after destruction (use-after-free).
        // The app is already shutting down, so a brief freeze is preferable
        // to undefined behavior.
        sink_->request_cancel();
        worker_->join();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const bool running = (run_state_ == RunState::Running ||
                          run_state_ == RunState::Cancelling);
    if (running) {
        // Deferred close: hide immediately so the window never sits frozen
        // ("Not Responding") while the UI thread would block in join().
        // Settings are persisted now; the actual join happens on the UI
        // thread when the worker's outcome signal arrives, which re-enters
        // closeEvent through the deferred close() below.
        event->ignore();
        save_window_settings();
        hide();
        sink_->request_cancel();
        apply_run_state(RunState::Cancelling);
        close_pending_ = true;
        return;
    }
    join_worker();  // no-op when idle; safety net for stray states
    save_window_settings();
    QMainWindow::closeEvent(event);
}

void MainWindow::save_window_settings() {
    QSettings settings;
    settings.setValue(QStringLiteral("mainwindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("paths/input"), line_edit_text(input_path_edit_));
    settings.setValue(QStringLiteral("paths/output"), line_edit_text(output_path_edit_));
    settings.setValue(QStringLiteral("paths/inspect"), line_edit_text(inspect_path_edit_));
}

void MainWindow::build_create_tab() {
    create_tab_ = new QWidget(this);
    auto* root = new QVBoxLayout(create_tab_);

    // ── Form ────────────────────────────────────────────────
    auto* form = new QFormLayout();

    input_path_edit_ = new DropLineEdit(create_tab_);
    input_path_edit_->setAcceptDrops(true);
    input_path_edit_->setPlaceholderText(tr("Arquivo ou pasta (ou arraste aqui)"));
    auto* input_file_btn = new QPushButton(tr("Arquivo…"), create_tab_);
    auto* input_dir_btn = new QPushButton(tr("Pasta…"), create_tab_);
    auto* input_row = new QHBoxLayout();
    input_row->addWidget(input_path_edit_, /*stretch=*/1);
    input_row->addWidget(input_file_btn);
    input_row->addWidget(input_dir_btn);
    auto* input_wrapper = new QWidget(create_tab_);
    input_wrapper->setLayout(input_row);
    form->addRow(tr("Entrada:"), input_wrapper);
    connect(input_file_btn, &QPushButton::clicked, this, &MainWindow::browse_input_file);
    connect(input_dir_btn, &QPushButton::clicked, this, &MainWindow::browse_input_dir);

    output_path_edit_ = new QLineEdit(create_tab_);
    output_path_edit_->setPlaceholderText(tr("Caminho do .torrent de saída"));
    auto* output_browse = new QPushButton(tr("Salvar como…"), create_tab_);
    auto* output_row = new QHBoxLayout();
    output_row->addWidget(output_path_edit_, /*stretch=*/1);
    output_row->addWidget(output_browse);
    auto* output_wrapper = new QWidget(create_tab_);
    output_wrapper->setLayout(output_row);
    form->addRow(tr("Saída .torrent:"), output_wrapper);
    connect(output_browse, &QPushButton::clicked, this, &MainWindow::browse_output);

    version_combo_ = new QComboBox(create_tab_);
    version_combo_->addItem(tr("v1"), static_cast<int>(TorrentVersion::V1));
    version_combo_->addItem(tr("v2"), static_cast<int>(TorrentVersion::V2));
    version_combo_->addItem(tr("hybrid"), static_cast<int>(TorrentVersion::HYBRID));
    form->addRow(tr("Versão:"), version_combo_);

    private_check_ = new QCheckBox(tr("Privado"), create_tab_);
    form->addRow(QString(), private_check_);

    comment_edit_ = new QLineEdit(create_tab_);
    form->addRow(tr("Comentário:"), comment_edit_);

    source_edit_ = new QLineEdit(create_tab_);
    form->addRow(tr("Source (cross-seed):"), source_edit_);

    entropy_check_ = new QCheckBox(tr("Entropia (randomiza info hash)"), create_tab_);
    form->addRow(QString(), entropy_check_);

    root->addLayout(form);

    // ── Announce lists ──────────────────────────────────────
    trackers_edit_ = new QPlainTextEdit(create_tab_);
    trackers_edit_->setPlaceholderText(tr("https://tracker.exemplo.com/announce"));
    trackers_edit_->setFixedHeight(64);
    form->addRow(tr("Trackers (um por linha):"), trackers_edit_);

    web_seeds_edit_ = new QPlainTextEdit(create_tab_);
    web_seeds_edit_->setPlaceholderText(tr("https://espelho.exemplo.com/arquivos/"));
    web_seeds_edit_->setFixedHeight(64);
    form->addRow(tr("Web seeds (um por linha):"), web_seeds_edit_);

    // ── Actions ─────────────────────────────────────────────
    auto* actions_row = new QHBoxLayout();
    create_button_ = new QPushButton(tr("Criar torrent"), create_tab_);
    create_button_->setDefault(true);
    cancel_button_ = new QPushButton(tr("Cancelar"), create_tab_);
    cancel_button_->setEnabled(false);
    actions_row->addWidget(create_button_);
    actions_row->addWidget(cancel_button_);
    actions_row->addStretch();
    root->addLayout(actions_row);
    connect(create_button_, &QPushButton::clicked, this, &MainWindow::start_creation);
    connect(cancel_button_, &QPushButton::clicked, this, &MainWindow::cancel_creation);

    // ── Progress + status ───────────────────────────────────
    progress_bar_ = new QProgressBar(create_tab_);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    root->addWidget(progress_bar_);

    progress_label_ = new QLabel(create_tab_);
    progress_label_->setTextFormat(Qt::PlainText);
    progress_label_->setVisible(false);
    root->addWidget(progress_label_);

    status_label_ = new QLabel(create_tab_);
    status_label_->setTextFormat(Qt::PlainText);
    root->addWidget(status_label_);

    // ── Log ──────────────────────────────────────────────────
    log_view_ = new QPlainTextEdit(create_tab_);
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(5000);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    log_view_->setFont(mono);
    root->addWidget(log_view_, /*stretch=*/1);
}

void MainWindow::build_inspect_tab() {
    inspect_tab_ = new QWidget(this);
    auto* root = new QVBoxLayout(inspect_tab_);

    auto* row = new QHBoxLayout();
    inspect_path_edit_ = new QLineEdit(inspect_tab_);
    auto* browse = new QPushButton(tr("Abrir…"), inspect_tab_);
    auto* run = new QPushButton(tr("Inspecionar"), inspect_tab_);
    row->addWidget(new QLabel(tr("Arquivo .torrent:"), inspect_tab_));
    row->addWidget(inspect_path_edit_, /*stretch=*/1);
    row->addWidget(browse);
    row->addWidget(run);
    root->addLayout(row);
    connect(browse, &QPushButton::clicked, this, &MainWindow::browse_inspect_target);
    connect(run, &QPushButton::clicked, this, &MainWindow::run_inspect);

    inspect_view_ = new QPlainTextEdit(inspect_tab_);
    inspect_view_->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    inspect_view_->setFont(mono);
    root->addWidget(inspect_view_, /*stretch=*/1);
}

// ── State machine ──────────────────────────────────────────────

void MainWindow::apply_run_state(RunState state, const QString& detail) {
    run_state_ = state;
    switch (state) {
    case RunState::Idle:
        status_label_->setText(tr("Pronto."));
        break;
    case RunState::Running:
        status_label_->setText(tr("Criando…"));
        break;
    case RunState::Cancelling:
        status_label_->setText(tr("Cancelando…"));
        break;
    case RunState::Succeeded:
        status_label_->setText(tr("Concluído."));
        break;
    case RunState::Failed:
        status_label_->setText(tr("Falhou: %1").arg(detail));
        break;
    case RunState::Cancelled:
        status_label_->setText(tr("Cancelado."));
        break;
    }
    const bool busy = (state == RunState::Running || state == RunState::Cancelling);
    create_button_->setEnabled(!busy);
    cancel_button_->setEnabled(state == RunState::Running);
}

void MainWindow::join_worker() {
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
}

// ── Create tab slots ───────────────────────────────────────────

void MainWindow::browse_input_file() {
    auto path = QFileDialog::getOpenFileName(this, tr("Selecionar arquivo"));
    if (!path.isEmpty()) input_path_edit_->setText(path);
}

void MainWindow::browse_input_dir() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Selecionar pasta"));
    if (!path.isEmpty()) input_path_edit_->setText(path);
}

void MainWindow::browse_output() {
    auto path = QFileDialog::getSaveFileName(this, tr("Salvar .torrent como"),
                                             QString(), tr("Torrent files (*.torrent)"));
    if (!path.isEmpty()) output_path_edit_->setText(path);
}

void MainWindow::start_creation() {
    if (run_state_ == RunState::Running || run_state_ == RunState::Cancelling) return;

    gui::CreationOptions options{
        .input_path = to_fs_path(line_edit_text(input_path_edit_)),
        .output_path = to_fs_path(line_edit_text(output_path_edit_)),
        .trackers = plain_edit_lines(trackers_edit_),
        .web_seeds = plain_edit_lines(web_seeds_edit_),
        .version = static_cast<TorrentVersion>(version_combo_->currentData().toInt()),
        .comment = line_edit_text(comment_edit_).toStdString(),
        .source = line_edit_text(source_edit_).toStdString(),
        .is_private = private_check_->isChecked(),
        .entropy = entropy_check_->isChecked(),
    };

    if (auto errors = options.validate(); !errors.empty()) {
        QStringList messages;
        for (const auto& error : errors) {
            messages.append(QString::fromStdString(error));
        }
        QMessageBox::warning(this, tr("Campos inválidos"),
                             messages.join(QLatin1Char('\n')));
        return;
    }

    std::error_code ec;
    if (!fs::exists(options.input_path, ec)) {
        QMessageBox::warning(this, tr("Caminho inválido"),
                             tr("O caminho de entrada não existe."));
        return;
    }

    // Reset UI for a new run.
    sink_->reset();
    progress_bar_->setValue(0);
    progress_label_->clear();
    progress_label_->setVisible(false);
    log_view_->clear();

    apply_run_state(RunState::Running);
    try {
        TorrentConfig config = options.to_torrent_config();
        worker_ = std::make_unique<std::thread>(run_creation, std::move(config), sink_);
    } catch (const std::exception& e) {
        // TorrentConfig validates path existence; surface it as a failed run.
        append_log(kLogErrorColor, QString::fromUtf8(e.what()));
        apply_run_state(RunState::Failed, QString::fromUtf8(e.what()));
    }
}

void MainWindow::cancel_creation() {
    if (run_state_ != RunState::Running) return;
    sink_->request_cancel();
    apply_run_state(RunState::Cancelling);
}

void MainWindow::on_progress(const QtProgress& p) {
    const int pct = gui::progress_percent(p.pieces_done, p.pieces_total);
    progress_bar_->setValue(pct);
    progress_label_->setVisible(true);
    QString text = tr("%L1 / %L2 peças · %3% · %4/s · ETA %5 · %6 / %7")
        .arg(p.pieces_done)
        .arg(p.pieces_total)
        .arg(pct)
        .arg(QString::fromStdString(gui::format_bytes(static_cast<uint64_t>(
             std::max(0.0, p.speed_bps)))))
        .arg(QString::fromStdString(gui::format_eta(p.eta_s)))
        .arg(QString::fromStdString(gui::format_bytes(
             static_cast<uint64_t>(std::max(0LL, p.bytes_done)))))
        .arg(QString::fromStdString(gui::format_bytes(
             static_cast<uint64_t>(std::max(0LL, p.bytes_total)))));
    if (!p.current_file.isEmpty()) text += tr("\nHashing: %1").arg(p.current_file);
    progress_label_->setText(text);
}

void MainWindow::on_info(const QString& msg) {
    append_log(kLogInfoColor, msg);
}

void MainWindow::on_verbose(const QString& msg) {
    append_log(kLogVerboseColor, msg);
}

void MainWindow::on_error(const QString& msg) {
    // Log-only by design: core error() messages may be non-fatal diagnostics.
    // Only the worker's failed() signal transitions the run to Failed.
    append_log(kLogErrorColor, msg);
}

void MainWindow::append_log(const QString& color, const QString& msg) {
    if (color.isEmpty()) {
        log_view_->appendHtml(msg.toHtmlEscaped());
    } else {
        log_view_->appendHtml(QStringLiteral("<span style=\"color:%1\">%2</span>")
                                  .arg(color, msg.toHtmlEscaped()));
    }
}

void MainWindow::on_creation_finished() {
    if (run_state_ != RunState::Running && run_state_ != RunState::Cancelling) return;
    const bool was_cancelling = (run_state_ == RunState::Cancelling);
    join_worker();
    apply_run_state(was_cancelling ? RunState::Cancelled : RunState::Succeeded);
    if (was_cancelling) {
        append_log(kLogInfoColor, tr("Cancelado pelo usuário."));
    } else {
        append_log(kLogSuccessColor, tr("Torrent criado com sucesso."));
    }
    if (close_pending_) {
        close_pending_ = false;
        close();  // re-enters closeEvent on the idle path: saves and accepts
    }
}

void MainWindow::on_creation_failed(const QString& what) {
    if (run_state_ != RunState::Running && run_state_ != RunState::Cancelling) return;
    join_worker();
    append_log(kLogErrorColor, what);
    apply_run_state(RunState::Failed, what);
    if (close_pending_) {
        close_pending_ = false;
        close();
    }
}

// ── Inspect tab slots ──────────────────────────────────────────

void MainWindow::browse_inspect_target() {
    auto path = QFileDialog::getOpenFileName(this, tr("Selecionar .torrent"),
                                             QString(), tr("Torrent files (*.torrent)"));
    if (!path.isEmpty()) inspect_path_edit_->setText(path);
}

void MainWindow::run_inspect() {
    const QString path_q = line_edit_text(inspect_path_edit_);
    if (path_q.isEmpty()) {
        QMessageBox::warning(this, tr("Sem arquivo"),
                             tr("Selecione um arquivo .torrent para inspecionar."));
        return;
    }
    try {
        TorrentInspector inspector(to_fs_path(path_q));
        auto meta = inspector.inspect();
        inspect_view_->setPlainText(
            QString::fromStdString(TorrentInspector::format_metadata(meta, false)));
    } catch (const std::exception& e) {
        inspect_view_->setPlainText(QString::fromUtf8(e.what()));
    }
}
