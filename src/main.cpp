#include "PetWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MikuPet"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("MikuPet"));
    app.setWindowIcon(QIcon(QStringLiteral(":/sprites/def01.png")));

    // Keep the process alive even when all windows are hidden
    // (tray icon keeps the app running)
    app.setQuitOnLastWindowClosed(false);

    PetWindow pet;
    pet.show();
    pet.setWindowState((pet.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    pet.raise();
    pet.activateWindow();

    return app.exec();
}
