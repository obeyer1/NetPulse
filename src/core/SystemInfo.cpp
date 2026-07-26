#include "core/SystemInfo.h"

#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace netpulse {
namespace {

constexpr int kRefreshIntervalMs = 15000;

// Runs a short read-only system command with a hard timeout.
QString runCommand(const QString &program, const QStringList &args, int timeoutMs = 2500)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

// Parses `route -n get [-inet6] default` output.
void parseDefaultRoute(const QString &out, QString *gateway, QString *iface)
{
    const QStringList lines = out.split('\n');
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("gateway:")) && gateway->isEmpty())
            *gateway = t.mid(8).trimmed();
        else if (t.startsWith(QLatin1String("interface:")) && iface->isEmpty())
            *iface = t.mid(10).trimmed();
    }
}

QStringList readDnsServers()
{
    QStringList servers;

    QFile resolv(QStringLiteral("/etc/resolv.conf"));
    if (resolv.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(resolv.readAll()).split('\n');
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1String("nameserver"))) {
                const QString addr = t.mid(10).trimmed();
                if (!addr.isEmpty() && !servers.contains(addr))
                    servers << addr;
            }
        }
    }

    if (servers.isEmpty()) {
        const QString out =
            runCommand(QStringLiteral("/usr/sbin/scutil"), {QStringLiteral("--dns")});
        static const QRegularExpression re(
            QStringLiteral("nameserver\\[\\d+\\]\\s*:\\s*(\\S+)"));
        auto it = re.globalMatch(out);
        while (it.hasNext() && servers.size() < 4) {
            const QString addr = it.next().captured(1);
            if (!servers.contains(addr))
                servers << addr;
        }
    }
    return servers;
}

NetworkSnapshot collectSnapshot()
{
    NetworkSnapshot s;
    s.capturedAt = QDateTime::currentDateTime();

    QString gateway, iface;
    parseDefaultRoute(runCommand(QStringLiteral("/sbin/route"),
                                 {QStringLiteral("-n"), QStringLiteral("get"),
                                  QStringLiteral("default")}),
                      &gateway, &iface);
    if (iface.isEmpty()) {
        parseDefaultRoute(runCommand(QStringLiteral("/sbin/route"),
                                     {QStringLiteral("-n"), QStringLiteral("get"),
                                      QStringLiteral("-inet6"), QStringLiteral("default")}),
                          &gateway, &iface);
    }
    s.gateway = gateway;
    s.interfaceName = iface;

    if (!iface.isEmpty()) {
        const QNetworkInterface qif = QNetworkInterface::interfaceFromName(iface);
        if (qif.isValid()) {
            const auto flags = qif.flags();
            s.linkUp = flags.testFlag(QNetworkInterface::IsUp)
                && flags.testFlag(QNetworkInterface::IsRunning);
            s.hardwareAddress = qif.hardwareAddress();
            const auto entries = qif.addressEntries();
            for (const QNetworkAddressEntry &e : entries) {
                const QHostAddress a = e.ip();
                if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                    s.ipv4 << a.toString();
                } else if (a.protocol() == QAbstractSocket::IPv6Protocol) {
                    if (!a.isLinkLocal())
                        s.ipv6 << a.toString();
                }
            }
        }
    }

    s.dnsServers = readDnsServers();
    s.valid = true;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// SystemInfoWorker
// ---------------------------------------------------------------------------

SystemInfoWorker::SystemInfoWorker(QObject *parent)
    : QObject(parent)
{
}

void SystemInfoWorker::start()
{
    if (timer_)
        return;
    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshIntervalMs);
    connect(timer_, &QTimer::timeout, this, &SystemInfoWorker::refreshNow);
    timer_->start();
    refreshNow();
}

void SystemInfoWorker::refreshNow()
{
    emit snapshotReady(collectSnapshot());
}

void SystemInfoWorker::shutdown()
{
    if (timer_)
        timer_->stop();
}

// ---------------------------------------------------------------------------
// SystemInfoService
// ---------------------------------------------------------------------------

SystemInfoService::SystemInfoService(QObject *parent)
    : QObject(parent)
{
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("netpulse-sysinfo"));
    worker_ = new SystemInfoWorker;
    worker_->moveToThread(thread_);

    connect(worker_, &SystemInfoWorker::snapshotReady,
            this, &SystemInfoService::snapshotReady);

    thread_->start();
    QMetaObject::invokeMethod(worker_, &SystemInfoWorker::start, Qt::QueuedConnection);
}

SystemInfoService::~SystemInfoService()
{
    shutdownAndWait();
}

void SystemInfoService::refreshNow()
{
    QMetaObject::invokeMethod(worker_, &SystemInfoWorker::refreshNow, Qt::QueuedConnection);
}

void SystemInfoService::shutdownAndWait()
{
    if (stopped_)
        return;
    stopped_ = true;
    QMetaObject::invokeMethod(worker_, &SystemInfoWorker::shutdown,
                              Qt::BlockingQueuedConnection);
    thread_->quit();
    thread_->wait(4000);
    delete worker_;
    worker_ = nullptr;
}

} // namespace netpulse
