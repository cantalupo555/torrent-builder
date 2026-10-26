#include "main_window.hpp"

#include <QApplication>
#include <QMetaType>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("torrent-builder-gui"));
    app.setOrganizationName(QStringLiteral("torrent-builder"));

    // Qt 6 registers only types declared with Q_DECLARE_METATYPE for queued
    // connections; register the progress payload before MainWindow connects it.
    qRegisterMetaType<QtProgress>("QtProgress");

    MainWindow window;
    window.show();

    return app.exec();
}
