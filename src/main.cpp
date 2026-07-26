#include "core/ProbeScheduler.h"
#include "core/SystemInfo.h"
#include "core/Types.h"
#include "storage/Storage.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using namespace netpulse;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NetPulse"));
    QCoreApplication::setApplicationName(QStringLiteral("NetPulse"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    qRegisterMetaType<TargetSpec>();
    qRegisterMetaType<ProbeSample>();
    qRegisterMetaType<ProbeConfig>();
    qRegisterMetaType<NetworkSnapshot>();
    qRegisterMetaType<Diagnosis>();
    qRegisterMetaType<QList<TargetSpec>>();
    qRegisterMetaType<QList<ProbeSample>>();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("NetPulse — Local Network Health Monitor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dataDirOpt(
        QStringLiteral("data-dir"),
        QStringLiteral("Store history/settings in this directory (developer use)."),
        QStringLiteral("path"));
    const QCommandLineOption screenshotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a window screenshot to <file> and quit (developer use)."),
        QStringLiteral("file"));
    const QCommandLineOption screenshotDelayOpt(
        QStringLiteral("screenshot-delay"),
        QStringLiteral("Seconds to wait before the screenshot (default 30)."),
        QStringLiteral("seconds"), QStringLiteral("30"));
    const QCommandLineOption quitAfterOpt(
        QStringLiteral("quit-after"),
        QStringLiteral("Quit automatically after N seconds (developer use)."),
        QStringLiteral("seconds"));
    parser.addOption(dataDirOpt);
    parser.addOption(screenshotOpt);
    parser.addOption(screenshotDelayOpt);
    parser.addOption(quitAfterOpt);
    parser.process(app);

    QString dataDir = parser.value(dataDirOpt);
    if (!dataDir.isEmpty()) {
        // Developer profiles keep their settings next to their database so
        // test runs never touch the real preferences.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir);
    } else {
        dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(dataDir);
    const QString dbPath = QDir(dataDir).filePath(QStringLiteral("netpulse.sqlite"));

    // Construction order matters: services are destroyed in reverse order, so
    // the window goes first and storage flushes last.
    Storage storage;
    ProbeService probes;
    SystemInfoService sysinfo;

    // Persist samples even if the UI is busy or being torn down.
    QObject::connect(&probes, &ProbeService::sampleReady, &storage,
                     [&storage](const ProbeSample &s) { storage.saveSample(s); });

    MainWindow window(&storage, &probes, &sysinfo, dbPath);
    storage.open(dbPath);
    window.show();

    if (parser.isSet(screenshotOpt)) {
        const QString file = parser.value(screenshotOpt);
        const int delay = qMax(1, parser.value(screenshotDelayOpt).toInt());
        QTimer::singleShot(delay * 1000, &window, [&window, file] {
            window.grab().save(file);
            QTimer::singleShot(300, qApp, &QCoreApplication::quit);
        });
    }
    if (parser.isSet(quitAfterOpt)) {
        const int secs = qMax(1, parser.value(quitAfterOpt).toInt());
        QTimer::singleShot(secs * 1000, qApp, &QCoreApplication::quit);
    }

    return app.exec();
}
