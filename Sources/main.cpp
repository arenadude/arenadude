#include "mainwindow.h"
#include "utility.h"
#include "Widgets/splashwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QTimer>


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    //Every QSettings uses these. A test run (log replay) gets its own settings with AT_SETTINGS_APP, as on macOS
    //they don't follow HOME: e.g. AT_SETTINGS_APP="Arena Dude Test" -> com.arena-dude.Arena Dude Test
    QCoreApplication::setOrganizationName("Arena Dude");
    QCoreApplication::setApplicationName(qEnvironmentVariable("AT_SETTINGS_APP", "Arena Dude"));
    Utility::migrateFromArenaTracker();

    //Deleted when closed
    SplashWindow *splash = new SplashWindow();
    splash->show();
    app.processEvents();

    MainWindow window;
    window.setSplashOpen();
    QObject::connect(splash, &QObject::destroyed, &window, &MainWindow::splashClosed);
    QObject::connect(&window, &MainWindow::startupProgress, splash, &SplashWindow::setProgress);
    QObject::connect(&window, &MainWindow::startupReady, splash, &SplashWindow::ready);

    return app.exec();
}

