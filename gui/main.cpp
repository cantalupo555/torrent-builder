#include "main_window.hpp"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QIcon>
#include <QMetaType>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("torrent-builder-gui"));
    app.setOrganizationName(QStringLiteral("torrent-builder"));

    // Keep the window icon and desktop integration aligned with the installed
    // .desktop entry (X11 WM_CLASS and Wayland app_id).
    app.setDesktopFileName(QStringLiteral("torrent-builder"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/torrent-builder.png")));

    // Qt 6 registers only types declared with Q_DECLARE_METATYPE for queued
    // connections; register the progress payload before MainWindow connects it.
    qRegisterMetaType<QtProgress>("QtProgress");

    MainWindow window;
    window.show();

    // Headless startup check for CTest gui_smoke
    // (QT_QPA_PLATFORM=offscreen): build and show the window, then quit on
    // the first event-loop tick. The exit code fails the test when the window
    // never reaches a visible state, so dropping show() cannot pass silently.
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        // Fail when the packaged icon resource is missing or the alias drifted:
        // the window would still show, but without the installed app icon.
        // QFile::exists is checked explicitly because QIcon::isNull semantics
        // differ across platform plugins; the resource is the actual contract.
        if (QApplication::windowIcon().isNull()
            || !QFile::exists(QStringLiteral(":/icons/torrent-builder.png"))) {
            qCritical("gui_smoke: window icon resource is missing");
            return 1;
        }
        if (!window.isVisible()) {
            qCritical("gui_smoke: main window is not visible after show()");
            return 1;
        }
        QTimer::singleShot(0, &app, [] { QCoreApplication::quit(); });
    }

    return app.exec();
}
