#include "qt_output_sink.hpp"

QtOutputSink::QtOutputSink(QObject* parent)
    : QObject(parent), OutputSink() {}

void QtOutputSink::info(const std::string& msg) {
    // emit across thread boundary; Qt marshals via the connected slot's
    // connection type (Qt::QueuedConnection when receiver lives on the UI thread).
    emit infoMessage(QString::fromStdString(msg));
}

void QtOutputSink::verbose(const std::string& msg) {
    emit verboseMessage(QString::fromStdString(msg));
}

void QtOutputSink::error(const std::string& msg) {
    emit errorMessage(QString::fromStdString(msg));
}

void QtOutputSink::progress(const ProgressInfo& p) {
    QtProgress qt{
        p.pieces_done,
        p.pieces_total,
        p.speed_bps,
        p.eta_s,
        p.bytes_done,
        p.bytes_total,
        QString::fromStdString(p.current_file)
    };
    emit progressUpdated(qt);
}
