#pragma once

#include "core/Types.h"

#include <QObject>
#include <QThread>

class QTimer;

namespace netpulse {

// ---------------------------------------------------------------------------
// SystemInfoWorker — collects read-only facts about the local network:
// default-route interface and gateway, local addresses, configured DNS
// servers. Never changes any system state. Lives on its own thread because
// it shells out to `route`/`scutil`, which can block briefly.
// ---------------------------------------------------------------------------
class SystemInfoWorker : public QObject {
    Q_OBJECT
public:
    explicit SystemInfoWorker(QObject *parent = nullptr);

public slots:
    void start();
    void refreshNow();
    void shutdown();

signals:
    void snapshotReady(const netpulse::NetworkSnapshot &snapshot);

private:
    QTimer *timer_ = nullptr;
};

// ---------------------------------------------------------------------------
// SystemInfoService — UI-thread facade owning the worker thread.
// ---------------------------------------------------------------------------
class SystemInfoService : public QObject {
    Q_OBJECT
public:
    explicit SystemInfoService(QObject *parent = nullptr);
    ~SystemInfoService() override;

    void refreshNow();
    void shutdownAndWait();

signals:
    void snapshotReady(const netpulse::NetworkSnapshot &snapshot);

private:
    QThread *thread_ = nullptr;
    SystemInfoWorker *worker_ = nullptr;
    bool stopped_ = false;
};

} // namespace netpulse
